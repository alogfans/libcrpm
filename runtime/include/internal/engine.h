//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_ENGINE_H
#define LIBCRPM_ENGINE_H

#include "crpm.h"

#ifdef USE_MPI_EXTENSION

#include <mpi/mpi.h>

#endif

namespace crpm {
    class Engine {
    public:
        static Engine *Open(const char *path,
                            const MemoryPoolOption &option);

        Engine() = default;

        virtual ~Engine() = default;

        virtual void checkpoint(uint64_t nr_threads) = 0;

        virtual bool exist_snapshot() = 0;

        void *get_address() { return get_address(0); }

        virtual void *get_address(uint64_t offset) = 0;

        virtual size_t get_capacity() = 0;

        virtual void wait_for_background_task() {}

#ifdef USE_MPI_EXTENSION

        static Engine *OpenForMPI(const char *path, const MemoryPoolOption &option, MPI_Comm comm);

        virtual void checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm) {}

#endif
    };
}

#endif //LIBCRPM_ENGINE_H
