#include <mpi.h>
#include <string>
#include <iostream>

#include "crawler.h"

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int N = 0, M = 0;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-n" && i + 1 < argc) N = std::stoi(argv[++i]);
        else if (std::string(argv[i]) == "-m" && i + 1 < argc) M = std::stoi(argv[++i]);
    }

    if (N <= 0 || M <= 0) {
        if (rank == 0)
            std::cerr << "Pouziti: mpirun -np <1+N+N*M> " << argv[0]
                      << " -n <N> -m <M>\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int expected = 1 + N + N * M;
    if (size != expected) {
        if (rank == 0)
            std::cerr << "Pro N=" << N << " M=" << M << " je treba " << expected
                      << " MPI procesu, spusteno " << size << ".\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (rank == 0) {
        runMaster(N, M);
    } else if (rank <= N) {
        runWorkerA(rank, N, M);
    } else {
        runWorkerB(rank, N, M);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
