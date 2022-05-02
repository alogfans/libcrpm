//
// Created by alogfans on 2/22/21.
//

#include <cstring>
#include "crpm.h"
#include "crpm_mpi.h"
#include "internal/allocator.h"
#include "internal/engine.h"
#include "internal/common.h"

using namespace crpm;

crpm_mpi_t *crpm_mpi_open(const char *path, crpm_option_t *option, MPI_Comm comm) {
    MemoryPoolOption native_option;
    native_option.create = option->create;
    native_option.capacity = option->capacity;
    native_option.truncate = option->truncate;
    native_option.engine_name = option->engine_name;
    native_option.allocator_name = option->allocator_name;
    native_option.verbose_output = option->verbose_output;
    native_option.shadow_capacity_factor = option->shadow_capacity_factor;
    native_option.fixed_base_address = option->fixed_base_address;

    auto engine = Engine::OpenForMPI(path, native_option, comm);
    if (!engine) {
        return nullptr;
    }

    auto allocator = Allocator::Open(engine, native_option);
    if (!allocator) {
        delete engine;
        return nullptr;
    }

    crpm_mpi_t *pool = (crpm_mpi_t *) malloc(sizeof(crpm_mpi_t));
    pool->pool = new MemoryPool(allocator, engine);
    pool->comm = comm;
    pool->desc_list = nullptr;
    return pool;
}

void crpm_mpi_close(crpm_mpi_t *pool) {
    crpm_close(pool->pool);
    auto desc = pool->desc_list;
    while (desc) {
        auto next_desc = desc->next;
        free(desc);
        desc = next_desc;
    }
    pool->desc_list = nullptr;
    free(pool);
}

static void crpm_mpi_safe_memcpy(void *dst, const void *src, size_t length) {
    for (uint64_t offset = 0; offset < length; offset += kBlockSize) {
        size_t chunk_size = std::min(length - offset, kBlockSize);
        uintptr_t dst_addr = (uintptr_t) dst + offset;
        uintptr_t src_addr = (uintptr_t) src + offset;
        if (memcmp((void *) dst_addr, (void *) src_addr, chunk_size) != 0) {
            AnnotateCheckpointRegion((void *) dst_addr, chunk_size);
            memcpy((void *) dst_addr, (void *) src_addr, chunk_size);
        }
    }
}

void crpm_mpi_checkpoint(crpm_mpi_t *pool, unsigned int nr_threads) {
    auto native_pool = (MemoryPool *) pool->pool;
    auto desc = pool->desc_list;
    while (desc) {
        crpm_mpi_safe_memcpy(desc->persist_buf, desc->runtime_ptr, desc->length);
        desc = desc->next;
    }
    StoreFence();
    native_pool->get_engine()->checkpoint_for_mpi(nr_threads, pool->comm);
    StoreFence();
}

void crpm_protect(crpm_mpi_t *pool, unsigned int index, void *ptr, size_t length) {
    auto desc = (crpm_protect_desc_t *) malloc(sizeof(crpm_protect_desc_t));
    desc->runtime_ptr = ptr;
    desc->length = length;
    desc->persist_buf = crpm_get_root(pool->pool, index);
    if (desc->persist_buf) {
        memcpy(ptr, desc->persist_buf, length);
    } else {
        desc->persist_buf = crpm_malloc(pool->pool, length);
        if (!desc->persist_buf) {
            fprintf(stderr, "out of persistent memory\n");
            exit(EXIT_FAILURE);
        }
        crpm_set_root(pool->pool, index, desc->persist_buf);
    }
    desc->next = pool->desc_list;
    pool->desc_list = desc;
}