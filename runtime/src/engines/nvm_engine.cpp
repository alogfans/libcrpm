//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include "internal/engines/nvm_engine.h"

namespace crpm {
    NvmEngine *NvmEngine::Open(const char *path, const MemoryPoolOption &option) {
        NvmEngine *impl = new NvmEngine();
        bool ret;
        int flags = 0;
        bool create = false;
        void *hint_addr = nullptr;

        if (option.fixed_base_address) {
            flags |= MAP_FIXED;
            hint_addr = (void *) option.fixed_base_address;
        }

        if (option.create) {
            create = (option.truncate || !FileSystem::Exist(path));
        }

        if (create) {
            ret = impl->fs.create(path, option.capacity, flags, hint_addr);
        } else {
            ret = impl->fs.open(path, flags, hint_addr);
        }

        if (!ret) {
            delete impl;
            return nullptr;
        }

        impl->has_init = true;
        return impl;
    }

    NvmEngine::NvmEngine() : has_init(false) {}

    NvmEngine::~NvmEngine() {
        if (has_init) {
            fs.close();
        }
    }

    void NvmEngine::checkpoint(uint64_t nr_threads) {
        // Do nothing
    }

    bool NvmEngine::exist_snapshot() {
        return false;
    }

    void *NvmEngine::get_address(uint64_t offset) {
        return fs.rel_to_abs(offset);
    }

    size_t NvmEngine::get_capacity() {
        return fs.get_size();
    }
}