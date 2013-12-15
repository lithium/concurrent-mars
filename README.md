
Thread-safe version of the [exhaust-ma](http://corewar.co.uk/ankerl/exhaust-ma.htm) embeddable core war simulator.
Supports running rounds concurrently. Each thread runs its own core simulator, running ```rounds/threadcount``` each.

Usage
```
cmars v1.9
usage: cmars [opts] warriors...
opts: -r <rounds>, -c <cycles>, -F <pos>, -s <coresize>,
      -p <maxprocs>, -d <minsep>, -j <threadcount> -bk
```
