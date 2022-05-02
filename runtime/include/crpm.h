//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_CRPM_H
#define LIBCRPM_CRPM_H

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <utility>
#include <pthread.h>
#include <string>
#include <limits>
#include <exception>
#include <cstdlib>

#else
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#endif //__cplusplus

#define DONT_INSTRUMENT                 __attribute__((annotate("__crpm_dont_instrument")))
#define MAX_NAME_LENGTH                 (256)
#define DEFAULT_FIXED_BASE_ADDRESS      (0x10000000000ull)
#define crpm_annotate(addr, length)    AnnotateCheckpointRegion((addr), (length))

#ifdef __cplusplus
namespace crpm {
    struct MemoryPoolOption {
        MemoryPoolOption();

        bool create;
        bool truncate;
        bool verbose_output;
        size_t capacity;
        double shadow_capacity_factor;
        uintptr_t fixed_base_address;
        std::string allocator_name;
        std::string engine_name;
    };

    const static uintptr_t kDefaultFixedBaseAddress = DEFAULT_FIXED_BASE_ADDRESS;

    class Allocator;

    class Engine;

    class MemoryPool {
    public:
        static MemoryPool *Open(const char *path, const MemoryPoolOption &option);

        MemoryPool(const MemoryPool &) = delete;

        MemoryPool &operator=(const MemoryPool &) = delete;

        MemoryPool(Allocator *allocator_, Engine *engine_) :
                has_init(true),
                allocator(allocator_),
                engine(engine_) {}

        ~MemoryPool();

    public:
        template<typename T>
        void set_root(uint8_t index, const T *object) {
            do_set_root(index, object);
        }

        template<typename T>
        T *get_root(uint8_t index) const {
            return reinterpret_cast<T *>(do_get_root(index));
        }

        template<typename T, typename... Args>
        T *pnew(Args &&... args) {
            T *ptr = reinterpret_cast<T *>(pmalloc(sizeof(T)));
            if (ptr) {
                new(ptr) T(std::forward<Args>(args)...);
            }
            return ptr;
        }

        template<typename T>
        T *pnew_array(size_t count) {
            T *ptr = reinterpret_cast<T *>(pmalloc(count * sizeof(T)));
            if (ptr) {
                for (size_t i = 0; i < count; ++i) {
                    new(&ptr[i]) T();
                }
            }
            return ptr;
        }

        template<typename T>
        void pdelete(T *obj) {
            if (!obj)
                return;
            obj->~T();
            pfree(obj);
        }

        template<typename T>
        void pdelete_array(T *obj, size_t count) {
            if (!obj)
                return;
            for (size_t i = 0; i < count; ++i)
                obj[i].~T();
            pfree(obj);
        }

        void *pmalloc(size_t size);

        void pfree(void *pointer);

        void checkpoint(uint64_t nr_threads = 1);

        void wait_for_background_task();

        void set_default_pool();

        Engine *get_engine() { return engine; }

    protected:
        void do_set_root(uint8_t index, const void *object);

        void *do_get_root(uint8_t index) const;

    protected:
        bool has_init;
        Allocator *allocator;
        Engine *engine;
    };

    extern MemoryPool *__crpm_global_pool;

    template<typename T>
    struct Crpm2Allocator {
        typedef T value_type;

        Crpm2Allocator() = default;

        template<class U>
        constexpr Crpm2Allocator(const Crpm2Allocator<U> &) noexcept {}

        value_type *allocate(std::size_t n) {
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(value_type))
                throw std::bad_alloc();
            else if (!__crpm_global_pool)
                throw std::bad_alloc();

            auto p = static_cast<value_type *>(
                    __crpm_global_pool->pmalloc(n * sizeof(value_type)));
            if (!p)
                throw std::bad_alloc();
            return p;
        }

        void deallocate(value_type *p, std::size_t n) noexcept {
            if (__crpm_global_pool) {
                __crpm_global_pool->pfree(p);
            }
        }
    };

    template<class T, class U>
    bool operator==(const Crpm2Allocator<T> &, const Crpm2Allocator<U> &) { return true; }

    template<class T, class U>
    bool operator!=(const Crpm2Allocator<T> &, const Crpm2Allocator<U> &) { return false; }
}
#endif //__cplusplus

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crpm_option {
    unsigned int create;
    unsigned int truncate;
    unsigned int verbose_output;
    size_t capacity;
    double shadow_capacity_factor;
    uintptr_t fixed_base_address;
    char allocator_name[MAX_NAME_LENGTH];
    char engine_name[MAX_NAME_LENGTH];
} crpm_option_t;

typedef void *crpm_t;

void crpm_init_option(crpm_option_t *option);

crpm_t crpm_open(const char *path, crpm_option_t *option);

void crpm_close(crpm_t pool);

void crpm_set_root(crpm_t pool, unsigned int index, void *object);

void *crpm_get_root(crpm_t pool, unsigned int index);

void *crpm_malloc(crpm_t pool, size_t size);

void crpm_free(crpm_t pool, void *ptr);

void *crpm_default_malloc(size_t size);

void crpm_default_free(void *ptr);

void crpm_checkpoint(crpm_t pool, unsigned int nr_threads);

void crpm_wait_for_background_task(crpm_t pool);

void crpm_set_default_pool(crpm_t pool);

__attribute__((noinline)) void AnnotateCheckpointRegion(void *addr, size_t length);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //LIBCRPM_CRPM_H
