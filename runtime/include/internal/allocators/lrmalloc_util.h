//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_LRMALLOC_UTIL_H
#define LIBCRPM_LRMALLOC_UTIL_H

#include <cstdint>
#include <cstddef>
#include <cassert>

#include "internal/pptr.h"

#define kSuperBlockShift    (16)
#define kSuperBlockSize     (1ULL << kSuperBlockShift)
#define kMaxSizeClasses     40
#define kMaxSize            ((1 << 13) + (1 << 11) * 3)

namespace crpm {
    struct SizeClassEntry {
        uint32_t block_size;
        uint32_t sb_size;
        uint32_t block_num;
        uint32_t cache_block_num;
    };

    struct SizeClass {
    public:
        static inline SizeClass *Get() {
            static SizeClass instance;
            return &instance;
        }

        inline uint8_t lookup(size_t size) {
            return size_class_lookup[size];
        }

        inline SizeClassEntry *get_entry(uint8_t idx) {
            assert(idx < kMaxSizeClasses);
            return &size_classes[idx];
        }

        SizeClass();

    private:
        SizeClassEntry size_classes[kMaxSizeClasses];
        size_t size_class_lookup[kMaxSize + 1];
    };

    class TCacheBin {
    public:
        TCacheBin() noexcept: block(nullptr), block_num(0) {}

        inline void push_block(char *new_block) {
            *(pptr<char> *) new_block = block;
            block = new_block;
            block_num++;
        }

        inline char *pop_block() {
            assert(block_num > 0);
            char *ret = block;
            char *next = *(pptr<char> *) ret;
            block = next;
            block_num--;
            return ret;
        }

        inline void push_list(char *head, uint32_t count) {
            assert(block_num == 0);
            block = head;
            block_num = count;
        }

        inline void pop_list(char *head, uint32_t count) {
            assert(block_num >= count);
            block = head;
            block_num -= count;
        }

        inline char *peek_block() const { return block; }

        inline uint32_t get_block_num() const { return block_num; }

    private:
        char *block;
        uint32_t block_num;
        uint32_t padding;
    };

    struct TCache {
        TCacheBin bin[kMaxSizeClasses];
    };
}

#endif //LIBCRPM_LRMALLOC_UTIL_H
