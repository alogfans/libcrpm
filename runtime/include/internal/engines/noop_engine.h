//
// Created by Feng Ren on 2021/1/25.
//

#ifndef LIBCRPM_NOOP_ENGINE_H
#define LIBCRPM_NOOP_ENGINE_H

#include "internal/engine.h"
#include <sys/mman.h>

namespace crpm {
    class NoopEngine : public Engine {
    public:
        static NoopEngine *Open(const char *path, const MemoryPoolOption &option) {
            return new NoopEngine(option);
        }

        NoopEngine(const MemoryPoolOption &option) {
            capacity = option.capacity;
            base = (uint8_t *) mmap(0, capacity, PROT_READ | PROT_WRITE,
                                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        }

        virtual ~NoopEngine() {
            munmap(base, capacity);
        }

        virtual void checkpoint(uint64_t nr_threads) {}

        virtual bool exist_snapshot() { return false; }

        virtual void *get_address(uint64_t offset) { return base + offset; }

        virtual size_t get_capacity() { return capacity; }

    private:
        uint8_t *base;
        size_t capacity;
    };
}

#endif //LIBCRPM_NOOP_ENGINE_H
