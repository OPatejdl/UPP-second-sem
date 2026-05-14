#include <mpi.h>
#include <string>
#include <iostream>

#include "crawler.hpp"

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int n = 0, m = 0;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-n" && i + 1 < argc) n = std::stoi(argv[++i]);
        else if (std::string(argv[i]) == "-m" && i + 1 < argc) m = std::stoi(argv[++i]);
    }

    if (n <= 0 || m <= 0) {
        if (rank == 0)
            std::cerr << "Usage: mpirun -np <1+N+N*M> " << argv[0] << " -n <N> -m <M>\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int expected = 1 + n + n * m;
    if (size != expected) {
        if (rank == 0)
            std::cerr << "Expected " << expected << " MPI processes for N=" << n
                      << " M=" << m << ", got " << size << "\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if      (rank == 0) run_master(n, m);
    else if (rank <= n) run_worker_a(rank, n, m);
    else                run_worker_b(rank, n, m);

    MPI_Finalize();
    return EXIT_SUCCESS;
}
