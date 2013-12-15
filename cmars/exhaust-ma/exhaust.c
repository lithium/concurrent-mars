/* exhaust.c: crippled pmars-like redcode simulator
 * $Id: exhaust.c,v 1.5 2003/07/13 09:22:32 martinus Exp $
 */

/* This file is part of `exhaust', a memory array redcode simulator.
 * Copyright (C) 2002 M Joonas Pihlaja
 * 
 * This file falls under the GNU Public License, version 2.  See the
 * file COPYING for details.  */

#define VERSION 1
#define REVISION 9

/* Note pmars incompatibility: Minimum warrior separation (minsep) is
   handled wrong by pmars (prior to 0.8.6) when minsep < MAXLENGTH.
   This affects running warriors in small cores since the other
   warriors might be loaded outside of core (it might even crash).
*/
#include <stdio.h>
#ifdef SYSV
#include <strings.h>
#else
#include <string.h>
#endif
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include <pthread.h>

#include "exhaust.h"		/* types */
#include "asm.h"		/* assembler proto */
#include "sim.h"		/* simulator proto */
#include "insn.h"

#define MAX_WARRIORS 10

static unsigned int	NWarriors = 0;
static warrior_t	Warriors[MAX_WARRIORS];

static char *		WarriorNames[MAX_WARRIORS];
/* File names of warriors. */


typedef struct threadctx {
     field_t		Positions[MAX_WARRIORS];
     field_t		StartPositions[MAX_WARRIORS];
                    /* Starting position of each warrior. */


     unsigned int	Deaths[MAX_WARRIORS];

     u32_t		Results[MAX_WARRIORS][MAX_WARRIORS+1];
                    /* Results[war_id][j] is the number of
                       rounds in which warrior war_id
                       survives until the end with exactly
                       j-1 other warriors.  For j=0, then
                       it is the number of rounds where
                       the warrior died. */

     pspace_t *	PSpaces[MAX_WARRIORS];
                    /* p-spaces of warriors. */

     core_t Core;

        pthread_t pthread;
    unsigned int NumRounds;
    unsigned int ThreadNumber;

} threadctx_t;

static threadctx_t *Contexts;

u32_t		OverallResults[MAX_WARRIORS][MAX_WARRIORS+1];

/* Globals to communicate options from readargs() to main() */
int	OPT_cycles = 80000;	/* cycles until tie */
unsigned int OPT_rounds = 1;	/* number of rounds to fight */
unsigned int 	OPT_coresize = 8000; /* default core size */
unsigned int 	OPT_minsep = 0;	/* minimum distance between warriors */
int	OPT_processes = 8000;	/* default max. procs./warrior */
int	OPT_F = 0;		/* initial position of warrior 2 */
int	OPT_k = 0;		/* nothing (`koth' format flag) */
int	OPT_b = 0;		/* nothing (we are always `brief') */
int	OPT_m = 0;		/* multi-warrior output format. */
int OPT_threads = 1;      /* multi-threading */

char *prog_name;
char errmsg[256];

/*---------------------------------------------------------------
 * Utilities
 */

#define min(x,y) ((x)<(y) ? (x) : (y))

/* xbasename(): custom basename(1); returns the filename from a path
 */
static
char *
xbasename(char* s)
{
    char *b;
    b = strrchr(s,'/');
    return b ? 1+b : s;
}

void
panic( char *msg )
{
    fprintf(stderr, "%s: ", prog_name );
    fprintf(stderr, "%s", msg );
    exit(1);
}


/* pmars/rng.c random number generator
 */
s32_t
rng(s32_t seed)
{
    register s32_t temp = seed;
    temp = 16807 * (temp % 127773) - 2836 * (temp / 127773);
    if (temp < 0)
	temp += 2147483647;
    return temp;
}

void
usage()
{
    printf("%s v%d.%d\n", prog_name, VERSION, REVISION );
    printf("usage: %s [opts] warriors...\n", prog_name );
    printf("opts: -r <rounds>, -c <cycles>, -F <pos>, -s <coresize>,\n"
	   "      -p <maxprocs>, -d <minsep>, -j <threadcount> -bk\n");
    exit(1);
}

/*---------------------------------------------------------------
 * Warrior initialisations.
 */

void
import_warriors()
{
    unsigned int i;

    for (i=0; i<NWarriors; i++) {
	asm_fname( WarriorNames[i], &Warriors[i], OPT_coresize );
    }
}

void
check_sanity()
{
    u32_t space_used;
    unsigned int i;

    /* Make sure each warrior has some code. */
    for (i=0; i<NWarriors; i++) {
	if (Warriors[i].len == 0) {
	    sprintf(errmsg,"warrior %d has no code\n", i);
	    panic(errmsg);
	}
    }

    /* Make sure there is some minimum sepation. */
    if ( OPT_minsep == 0 ) {
	OPT_minsep = min( OPT_coresize/NWarriors, MAXLENGTH );
    }

    /* Make sure minsep dominates the lengths of all warriors. */
    for (i=0; i<NWarriors; i++) {
	if ( OPT_minsep < Warriors[i].len ) {
	    panic("minimum separation must be >= longest warrior\n");
	}
    }

    /* Make sure there is space for all warriors to be loaded. */
    space_used = NWarriors*OPT_minsep;
    if ( space_used > OPT_coresize ) {
	panic("warriors too large to fit into core\n");
    }
}


/*---------------------------------------------------------------
 * Warrior positioning algorithms
 *
 * These are pMARS compatible.  Warrior 0 is always positioned at 0.
 * posit() and npos() are transcribed from pmars/pos.c.  */

#define RETRIES1 20                /* how many times to try generating one
				    * position */
#define RETRIES2 4                /* how many times to start backtracking */
int
posit(threadctx_t *threadctx, s32_t *seed)
{
    unsigned int pos = 1, i;
    unsigned int retries1 = RETRIES1, retries2 = RETRIES2;
    int     diff;

    do {
	/* generate */
	*seed = rng(*seed);
	threadctx->Positions[pos] =
	    (*seed % (OPT_coresize - 2*OPT_minsep+1))+OPT_minsep;

	/* test for overlap */
	for (i = 1; i < pos; ++i) {
	    /* calculate positive difference */
	    diff = (int) threadctx->Positions[pos] - threadctx->Positions[i];
	    if (diff < 0)
		diff = -diff;
	    if ((unsigned int)diff < OPT_minsep)
		break;		/* overlap! */
	}

	if (i == pos)		/* no overlap, generate next number */
	    ++pos;
	else {			/* overlap */
	    if (!retries2)
		return 1;	/* exceeded attempts, fail */
	    if (!retries1) {	/* backtrack: generate new sequence starting */
		pos = i;	/* at arbitrary position (last conflict) */
		--retries2;
		retries1 = RETRIES1;
	    } else		/* generate new current number (pos not
                                 * incremented) */
		--retries1;
	}
    } while (pos < NWarriors);
    return 0;
}

void
npos(threadctx_t *threadctx, s32_t *seed)
{
    unsigned int i, j;
    unsigned int temp;
    unsigned int room = OPT_coresize - OPT_minsep*NWarriors+1;

    /* Choose NWarriors-1 positions from the available room. */
    for (i = 1; i < NWarriors; i++) {
	*seed = rng(*seed);
	temp = *seed % room;
	for (j = i - 1; j > 0; j--) {
	    if (temp > threadctx->Positions[j])
		break;
	    threadctx->Positions[j+1] = threadctx->Positions[j];
	}
	threadctx->Positions[j+1] = temp;
    }

    /* Separate the positions by OPT_minsep cells. */
    temp = OPT_minsep;
    for (i = 1; i < NWarriors; i++) {
	threadctx->Positions[i] += temp;
	temp += OPT_minsep;
    }

    /* Random permutation of positions. */
    for (i = 1; i < NWarriors; i++) {
	*seed = rng(*seed);
	j = *seed % (NWarriors - i) + i;
	temp = threadctx->Positions[j];
	threadctx->Positions[j] = threadctx->Positions[i];
	threadctx->Positions[i] = temp;
    }
}

s32_t
compute_positions(threadctx_t *threadctx, s32_t seed)
{
    u32_t avail = OPT_coresize+1 - NWarriors*OPT_minsep;

    threadctx->Positions[0] = 0;

    /* Case single warrior. */
    if (NWarriors == 1) return seed;

    /* Case one on one. */
    if (NWarriors == 2) {
	threadctx->Positions[1] = OPT_minsep + seed % avail;
	seed = rng(seed);
	return seed;
    }

    if (NWarriors>2) {
	if (posit(threadctx, &seed)) {
	    npos(threadctx, &seed);
	}
    }
    return seed;
}


/*---------------------------------------------------------------
 * Misc.
 */

void
save_pspaces(threadctx_t *threadctx)
{
    pspace_t **ps;
    ps = sim_get_pspaces(&(threadctx->Core));
    memcpy( threadctx->PSpaces, ps, sizeof(pspace_t *)*NWarriors );
}

void
amalgamate_pspaces(threadctx_t *threadctx)
{
    unsigned int i, j;

    /* Share p-space according to PINs. */
    for (i=0; i<NWarriors; i++) {
	if ( Warriors[i].have_pin ) {
	    for (j=0; j<i; j++) {
		if ( Warriors[j].have_pin &&
		     Warriors[j].pin == Warriors[i].pin )
		{
		    pspace_share( threadctx->PSpaces[i], threadctx->PSpaces[j] );
		}
	    }
	}
    }
}

void
load_warriors(threadctx_t *threadctx)
{
    unsigned int i;
    for (i=0; i<NWarriors; i++) {
	sim_load_warrior(&(threadctx->Core), threadctx->Positions[i], &Warriors[i].code[0], Warriors[i].len);
    }
}

void
set_starting_order(threadctx_t *threadctx, unsigned int round)
{
    unsigned int i;
    pspace_t **ps;

    /* Copy load positions into starting positions array
       with a cyclic shift of rounds places. */
    for (i=0; i<NWarriors; i++) {
	unsigned int j = (i+round) % NWarriors;
	threadctx->StartPositions[i] =
	    ( threadctx->Positions[j] + Warriors[j].start ) % OPT_coresize;
    }

    /* Copy p-spaces into simulator p-space array with a
       cyclic shift of rounds places. */
    ps = sim_get_pspaces(&(threadctx->Core));
    for (i=0; i<NWarriors; i++) {
	ps[i] = threadctx->PSpaces[(i + round) % NWarriors];
    }
}

void
clear_results(threadctx_t *threadctx)
{
    unsigned int i, j;
    for (i=0; i<NWarriors; i++) {
	for (j=0; j<=NWarriors; j++) {
	    threadctx->Results[i][j] = 0;
	}
    }
}

void
accumulate_results(threadctx_t *threadctx)
{
    unsigned int i;

    /* Fetch the results of the last round from p-space location 0
       that has been updated by the simulator. */
    for (i=0; i<NWarriors; i++) {
	unsigned int result;
	result = pspace_get(threadctx->PSpaces[i], 0);
	threadctx->Results[i][result]++;
    }
}


void
output_results(u32_t (*results)[MAX_WARRIORS+1])
{
    unsigned int i;
    unsigned int j;

    if (NWarriors == 2 && !OPT_m) {
	printf("%ld %ld\n", results[0][1], results[0][2]);
	printf("%ld %ld\n", results[1][1], results[1][2]);
    } else {
	for (i=0; i<NWarriors; i++) {
	    for (j=1; j<=NWarriors; j++) {
		printf("%ld ", results[i][j]);
	    }
	    printf("%ld\n", results[i][0]);
	}
    }
}


/*---------------------------------------------------------------
 * Command line arguments.
 */

/*
 * parse options
 */
void
readargs(int argc, char** argv )
{
  int n;
  char c;
  int cix;
  int tmp;

  n = 1;
  while ( n < argc ) {
    cix = 0;
    c = argv[n][cix++];
    if ( c == '-' && argv[n][1] ) {
      do {
	c = argv[n][cix++];
	if (c)
	  switch (c) {

	  case 'k': OPT_k = 1; break;
	  case 'b': OPT_b = 1; break;
	  case 'm': OPT_m = 1; break;

	  case 'F':
	    if ( n == argc-1 || !isdigit(argv[n+1][0]) )
	      panic( "bad argument for option -F\n");
	    c = 0;
	    OPT_F = atoi( argv[++n] );
	    break;

	  case 's':
	    if ( n == argc-1 || !isdigit(argv[n+1][0]) )
	      panic( "bad argument for option -s\n");
	    c = 0;
	    OPT_coresize = atoi( argv[++n] );
	    if ( OPT_coresize <= 0 )
	      panic( "core size must be > 0\n");
	    break;

	  case 'd':
	    if ( n == argc-1 || !isdigit(argv[n+1][0]) )
	      panic( "bad argument for option -d\n");
	    c = 0;
	    OPT_minsep = atoi( argv[++n] );
	    if ( (int)OPT_minsep <= 0 )
	      panic( "minimum warrior separation must be > 0\n" );
	    break;

	  case 'p':
	    if ( n == argc-1 || !isdigit(argv[n+1][0]) )
	      panic( "bad argument for option -p\n");
	    c = 0;
	    OPT_processes = atoi( argv[++n] );
	    if ( OPT_processes <= 0 )
	      panic( "max processes must be > 0\n" );
	    break;

	  case 'r':
	    if ( n == argc-1 || !isdigit(argv[n+1][0]) )
	      panic( "bad argument for option -r\n");
	    c = 0;
	    tmp = atoi( argv[++n] );
	    if ( tmp < 0 )
	      panic( "can't do a negative number of rounds!\n" );
	    OPT_rounds = tmp;
	    break;
	  case 'c':
	    if ( n == argc-1 || !isdigit(argv[n+1][0]) )
	      panic( "bad argument for option -c\n");
	    c = 0;
	    OPT_cycles = atoi( argv[++n] );
	    if ( OPT_cycles <= 0 )
	      panic( "cycles must be > 0\n" );
	    break;
      case 'j':
          if ( n == argc-1 || !isdigit(argv[n+1][0]) )
              panic( "bad argument for option -j\n");
          c = 0;
          OPT_threads = atoi( argv[++n] );
          if ( OPT_threads <= 0 )
              panic( "threads must be > 0\n" );
          break;

	  default:
	    sprintf(errmsg,"unknown option '%c'\n", c);
	    panic(errmsg);
	  }
      } while (c);

    } else /* it's a file name */ {
	if (NWarriors == MAX_WARRIORS) {
	    panic("too many warriors\n");
	}
	WarriorNames[NWarriors++] = argv[n];
    }
    n++;
  }

  if ( NWarriors == 0 )
    usage();
}


/*-------------------------------------------------------------------------
 * Thread Main
 */
void *thread_main(void *arg)
{
    threadctx_t *threadctx = (threadctx_t*)arg;
    unsigned int n;		/* round counter */
    s32_t seed;			/* rnd seed. */
    
    clear_results(threadctx);
    seed = OPT_F ? OPT_F-OPT_minsep : rng(time(0)*0x1d872b41);
    
    
    /*
     * Allocate simulator buffers and initialise p-spaces.
     */
    if ( sim_create( &(threadctx->Core), NWarriors, OPT_coresize, OPT_processes, OPT_cycles) != 0 )
        panic("can't allocate memory.\n");
    
    save_pspaces(threadctx);
    amalgamate_pspaces(threadctx);	/* Share P-spaces with equal PINs */
    
    /*
     * Fight NumRounds rounds.
     */
    for ( n = 0; n < threadctx->NumRounds; n++ ) {
        int nalive;
        sim_clear_core(&(threadctx->Core));
        
        seed = compute_positions(threadctx, seed);
        load_warriors(threadctx);
        set_starting_order(threadctx, n);
        
        nalive = sim_mw( &(threadctx->Core), threadctx->StartPositions, threadctx->Deaths );
        if (nalive<0)
            panic("simulator panic!\n");
        
        accumulate_results(threadctx);
    }
//    output_results(threadctx->Results);

    return NULL;
}

/*-------------------------------------------------------------------------
 * Main
 */

int
main( int argc, char **argv )
{

    prog_name = xbasename(argv[0]);
    readargs(argc, argv);
    
    import_warriors();
    check_sanity();
    
    Contexts = malloc(OPT_threads * sizeof(threadctx_t));
    memset(Contexts, 0, OPT_threads*sizeof(threadctx_t));
    memset(OverallResults, 0, sizeof(u32_t)*MAX_WARRIORS*(MAX_WARRIORS+1));

    int roundsPerThread = OPT_rounds/OPT_threads;
    int j;
    for (j=0; j < OPT_threads; j++) {
        threadctx_t *threadctx = &Contexts[j];
        threadctx->NumRounds = roundsPerThread;
        threadctx->ThreadNumber = j;
        int retval;
        if ((retval = pthread_create(&(threadctx->pthread), NULL, thread_main, threadctx)) != 0) {
            panic("Failed to create thread!\n");
            exit(retval);
        }
    }
    
    // block until all threads are done
    for (j=0; j < OPT_threads; j++) {
        threadctx_t *threadctx = &Contexts[j];
        pthread_join(threadctx->pthread, NULL);

        for (int x=0; x<NWarriors; x++) {
            for (int y=0; y<=NWarriors; y++) {
                OverallResults[x][y] += threadctx->Results[x][y];
            }
        }
        sim_free_bufs(&(threadctx->Core));
    }

    output_results(OverallResults);
    return 0;
}
