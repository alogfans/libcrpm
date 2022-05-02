//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_NVM_ENGINE_H
#define LIBCRPM_NVM_ENGINE_H

#include "crpm.h"
#include "internal/filesystem.h"
#include "internal/engine.h"

namespace crpm {
    class NvmEngine : public Engine {
    public:
        static NvmEngine *Open(const char *path, const MemoryPoolOption &option);

        NvmEngine();

        virtual ~NvmEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

    private:
        bool has_init;
        FileSystem fs;
    };
}

#endif //LIBCRPM_NVM_ENGINE_H
