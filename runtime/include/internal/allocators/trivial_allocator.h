//
// Created by Feng Ren on 2021/1/25.
//

#ifndef LIBCRPM_TRIVIAL_ALLOCATOR_H
#define LIBCRPM_TRIVIAL_ALLOCATOR_H

#include <cstdlib>
#include "internal/allocator.h"

namespace crpm {
    class TrivialAllocator : public Allocator {
    public:
        TrivialAllocator() {}

        virtual ~TrivialAllocator() {}

        static TrivialAllocator *Open(Engine *engine, const MemoryPoolOption &option) {
            return new TrivialAllocator();
        }

        virtual void *pmalloc(size_t size) { return malloc(size); }

        virtual void pfree(void *pointer) { free(pointer); }

        virtual void set_root(unsigned int index, const void *object) {}

        virtual void *get_root(unsigned int index) const { return nullptr; }
    };
}

#endif //LIBCRPM_TRIVIAL_ALLOCATOR_H
