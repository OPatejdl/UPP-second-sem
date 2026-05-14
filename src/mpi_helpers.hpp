#pragma once

#include <mpi.h>
#include <string>

static const int TAG_WORK      = 1;
static const int TAG_DONE      = 2;
static const int TAG_TASK      = 3;
static const int TAG_RESULT    = 4;
static const int TAG_TERMINATE = 5;

// Odešle řetězec jako MPI zprávu.
// msg  - obsah zprávy
// dest - rank cílového procesu
// tag  - MPI tag zprávy
inline void mpi_send_str(const std::string& msg, int dest, int tag) {
    MPI_Send(msg.data(), static_cast<int>(msg.size()), MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

// Blokující příjem řetězce. Podporuje MPI_ANY_SOURCE a MPI_ANY_TAG.
// source     - rank odesílatele nebo MPI_ANY_SOURCE
// tag        - očekávaný tag nebo MPI_ANY_TAG
// out_source - (volitelný) skutečný rank odesílatele
// out_tag    - (volitelný) skutečný tag přijaté zprávy
// vrací      - přijatý řetězec
inline std::string mpi_recv_str(int source, int tag,
                                int* out_source = nullptr,
                                int* out_tag    = nullptr) {
    MPI_Status st;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &st);

    int count = 0;
    MPI_Get_count(&st, MPI_CHAR, &count);

    std::string result(count, '\0');
    char dummy = 0;
    void* buf = (count > 0) ? static_cast<void*>(result.data())
                            : static_cast<void*>(&dummy);
    MPI_Recv(buf, count, MPI_CHAR, st.MPI_SOURCE, st.MPI_TAG,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (out_source) *out_source = st.MPI_SOURCE;
    if (out_tag)    *out_tag    = st.MPI_TAG;
    return result;
}
