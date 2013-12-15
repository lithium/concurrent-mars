/* sim.h: prototype
 * $Id: sim.h,v 1.3 2003/06/06 21:31:15 martinus Exp $
 */
#ifndef SIM_H
#define SIM_H

#include "pspace.h"

/*
void print_counts(void);
void clear_counts(void);
*/

/* internal warrior structure */
typedef struct w_st {
    insn_t **tail;		/* next free location to queue a process */
    insn_t **head;		/* next process to run from queue */
    struct w_st *succ;		/* next warrior alive */
    u32_t nprocs;			/* number of live processes in this warrior */
    struct w_st *pred;		/* previous warrior alive */
    int id;			/* index (or identity) of warrior */
} w_t;


typedef struct core_st {
    unsigned int Coresize;
    unsigned int Processes;
    unsigned int NWarriors;
    unsigned int Cycles;
    
    w_t          *War_Tab;
    insn_t       *Core_Mem;
    insn_t       **Queue_Mem;
    
    /* P-space */
    unsigned int PSpace_size;    /* # p-space slots per warrior. */
    pspace_t	 **PSpaces;	     /* p-spaces of each warrior. */
    
} core_t;


int
sim_create(core_t *core, unsigned int nwars, unsigned int coresize, unsigned int processes, unsigned int cycles);


void sim_free_bufs(core_t *core);

void sim_clear_core(core_t *core);


pspace_t **sim_get_pspaces(core_t *core);

pspace_t *sim_get_pspace(core_t *core, unsigned int war_id);

void sim_clear_pspaces(core_t *core);

void sim_reset_pspaces(core_t *core);

int sim_load_warrior(core_t *core, unsigned int pos, insn_t const *code, unsigned int len);



//int sim( core_t *core, int nwar_arg, field_t w1_start, field_t w2_start,
//	 unsigned int cycles, void **ptr_result );

int sim_mw( core_t *core, const field_t *war_pos_tab,
	    unsigned int *death_tab );

#endif /* SIM_H */
