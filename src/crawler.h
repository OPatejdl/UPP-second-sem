#pragma once

// Entry points for each node role.
// rank  - MPI rank of this process
// N     - number of Worker A nodes
// M     - number of Worker B nodes per Worker A

void runMaster(int N, int M);
void runWorkerA(int rank, int N, int M);
void runWorkerB(int rank, int N, int M);
