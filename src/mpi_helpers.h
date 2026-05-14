#pragma once

#include <mpi.h>
#include <string>

// MPI message tags
static const int TAG_WORK      = 1; // master -> worker A: batch of URLs
static const int TAG_DONE      = 2; // worker A -> master: serialized results
static const int TAG_TASK      = 3; // worker A -> worker B: single URL to analyse
static const int TAG_RESULT    = 4; // worker B -> worker A: page analysis result
static const int TAG_TERMINATE = 5; // shutdown signal

inline void mpiSendStr(const std::string& msg, int dest, int tag) {
    MPI_Send(msg.data(), static_cast<int>(msg.size()), MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

// Blocking receive from `source` (may be MPI_ANY_SOURCE) with `tag` (may be MPI_ANY_TAG).
// Writes actual source rank and tag into outSource / outTag when non-null.
inline std::string mpiRecvStr(int source, int tag,
                               int* outSource = nullptr, int* outTag = nullptr) {
    MPI_Status st;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &st);

    int count = 0;
    MPI_Get_count(&st, MPI_CHAR, &count);

    std::string result(count, '\0');
    char dummy = 0;
    void* buf = (count > 0) ? static_cast<void*>(result.data()) : static_cast<void*>(&dummy);
    MPI_Recv(buf, count, MPI_CHAR, st.MPI_SOURCE, st.MPI_TAG,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (outSource) *outSource = st.MPI_SOURCE;
    if (outTag)    *outTag    = st.MPI_TAG;
    return result;
}
