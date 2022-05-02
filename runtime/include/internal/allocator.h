//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_ALLOCATOR_H
#define LIBCRPM_ALLOCATOR_H

#include <crpm.h>

namespace crpm {
    class Engine;

    class Allocator {
    public:
        static Allocator *Open(Engine *engine,
                               const MemoryPoolOption &option);

        Allocator() = default;

        virtual ~Allocator() = default;

        virtual void *pmalloc(size_t size) = 0;

        virtual void pfree(void *pointer) = 0;

        virtual void set_root(unsigned int index, const void *object) = 0;

        virtual void *get_root(unsigned int index) const = 0;
    };
}

#endif //LIBCRPM_ALLOCATOR_H
