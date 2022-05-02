//
// Created by alogfans on 2/22/21.
//

#ifndef LIBCRPM_CRPM_MPI_H
#define LIBCRPM_CRPM_MPI_H

#include "crpm.h"
#include <mpi/mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crpm_protect_desc {
    void *runtime_ptr;
    void *persist_buf;
    size_t length;
    unsigned int index;
    struct crpm_protect_desc *next;
} crpm_protect_desc_t;

typedef struct crpm_mpi {
    crpm_t pool;
    MPI_Comm comm;
    crpm_protect_desc_t *desc_list;
} crpm_mpi_t;

crpm_mpi_t *crpm_mpi_open(const char *path, crpm_option_t *option, MPI_Comm comm);

void crpm_mpi_close(crpm_mpi_t *pool);

void crpm_mpi_checkpoint(crpm_mpi_t *pool, unsigned int nr_threads);

void crpm_protect(crpm_mpi_t *pool, unsigned int index, void *ptr, size_t length);

#ifdef __cplusplus
}
#endif

#endif //LIBCRPM_CRPM_MPI_H
