//
// Created by Feng Ren on 2021/1/23.
//

#include <cstdlib>
#include <cassert>
#include <cstring>

#include "crpm.h"
#include "internal/allocator.h"
#include "internal/engine.h"
#include "internal/common.h"

namespace crpm {
    MemoryPool *__crpm_global_pool = nullptr;

    MemoryPoolOption::MemoryPoolOption() :
            create(false),
            truncate(false),
            verbose_output(false),
            capacity(0),
            shadow_capacity_factor(crpm::kShadowMemoryCapacityFactor),
            fixed_base_address(0),
            allocator_name("default"),
            engine_name("default") {}

    MemoryPool *MemoryPool::Open(const char *path, const MemoryPoolOption &option) {
        auto engine = Engine::Open(path, option);
        if (!engine) {
            return nullptr;
        }

        auto allocator = Allocator::Open(engine, option);
        if (!allocator) {
            delete engine;
            return nullptr;
        }

        return new MemoryPool(allocator, engine);
    }

    MemoryPool::~MemoryPool() {
        if (has_init) {
            assert(allocator && engine);
            delete allocator;
            delete engine;
            if (__crpm_global_pool == this) {
                __crpm_global_pool = nullptr;
            }
        }
    }

    void *MemoryPool::pmalloc(size_t size) {
        assert(has_init && allocator);
        return allocator->pmalloc(size);
    }

    void MemoryPool::pfree(void *pointer) {
        assert(has_init && allocator);
        allocator->pfree(pointer);
    }

    void MemoryPool::do_set_root(uint8_t index, const void *object) {
        assert(has_init && allocator);
        allocator->set_root(index, object);
    }

    void *MemoryPool::do_get_root(uint8_t index) const {
        assert(has_init && allocator);
        return allocator->get_root(index);
    }

    void MemoryPool::checkpoint(uint64_t nr_threads) {
        assert(has_init && engine);
        StoreFence();
        engine->checkpoint(nr_threads);
        StoreFence();
    }

    void MemoryPool::wait_for_background_task() {
        assert(has_init && engine);
        engine->wait_for_background_task();
    }

    void MemoryPool::set_default_pool() {
        if (__crpm_global_pool) {
            fprintf(stderr, "default pool has been assigned, force to reassign\n");
        }
        __crpm_global_pool = this;
    }
}

void crpm_init_option(crpm_option_t *option) {
    if (!option) {
        fprintf(stderr, "crpm_init_option: option required\n");
        return;
    }
    memset(option, 0, sizeof(crpm_option_t));
    strcpy(option->allocator_name, "default");
    strcpy(option->engine_name, "default");
    option->shadow_capacity_factor = crpm::kShadowMemoryCapacityFactor;
}

crpm_t crpm_open(const char *path, crpm_option_t *option) {
    if (!option) {
        fprintf(stderr, "crpm_open: option required\n");
        return nullptr;
    }
    crpm::MemoryPoolOption opt;
    opt.create = option->create;
    opt.capacity = option->capacity;
    opt.truncate = option->truncate;
    opt.engine_name = option->engine_name;
    opt.allocator_name = option->allocator_name;
    opt.verbose_output = option->verbose_output;
    opt.fixed_base_address = option->fixed_base_address;
    opt.shadow_capacity_factor = option->shadow_capacity_factor;
    crpm::MemoryPool *pool = crpm::MemoryPool::Open(path, opt);
    return pool;
}

void crpm_close(crpm_t pool) {
    delete (crpm::MemoryPool *) pool;
}

void crpm_set_root(crpm_t pool, unsigned int index, void *object) {
    auto target = pool ? (crpm::MemoryPool *) pool : crpm::__crpm_global_pool;
    if (!target) {
        return;
    }
    target->set_root(index, object);
}

void *crpm_get_root(crpm_t pool, unsigned int index) {
    auto target = pool ? (crpm::MemoryPool *) pool : crpm::__crpm_global_pool;
    if (!target) {
        return nullptr;
    }
    return target->get_root<void>(index);
}

void *crpm_malloc(crpm_t pool, size_t size) {
    auto target = pool ? (crpm::MemoryPool *) pool : crpm::__crpm_global_pool;
    if (!target) {
        return nullptr;
    }
    return target->pmalloc(size);
}

void crpm_free(crpm_t pool, void *ptr) {
    auto target = pool ? (crpm::MemoryPool *) pool : crpm::__crpm_global_pool;
    if (!target) {
        return;
    }
    target->pfree(ptr);
}

void *crpm_default_malloc(size_t size) {
    if (!crpm::__crpm_global_pool) {
        return nullptr;
    }
    return crpm::__crpm_global_pool->pmalloc(size);
}

void crpm_default_free(void *ptr) {
    if (!crpm::__crpm_global_pool) {
        return;
    }
    crpm::__crpm_global_pool->pfree(ptr);
}

void crpm_checkpoint(crpm_t pool, unsigned int nr_threads) {
    auto target = pool ? (crpm::MemoryPool *) pool : crpm::__crpm_global_pool;
    if (!target) {
        return;
    }
    target->checkpoint(nr_threads);
}

void crpm_wait_for_background_task(crpm_t pool) {
    auto target = pool ? (crpm::MemoryPool *) pool : crpm::__crpm_global_pool;
    if (!target) {
        return;
    }
    target->wait_for_background_task();
}

void crpm_set_default_pool(crpm_t pool) {
    crpm::__crpm_global_pool = (crpm::MemoryPool *) pool;
}