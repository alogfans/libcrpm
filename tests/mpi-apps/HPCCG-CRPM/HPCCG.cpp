
//@HEADER
// ************************************************************************
// 
//               HPCCG: Simple Conjugate Gradient Benchmark Code
//                 Copyright (2006) Sandia Corporation
// 
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
// 
// BSD 3-Clause License
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// 
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// Questions? Contact Michael A. Heroux (maherou@sandia.gov) 
// 
// ************************************************************************
//@HEADER
/////////////////////////////////////////////////////////////////////////

// Routine to compute an approximate solution to Ax = b where:

// A - known matrix stored as an HPC_Sparse_Matrix struct

// b - known right hand side vector

// x - On entry is initial guess, on exit new approximate solution

// max_iter - Maximum number of iterations to perform, even if
//            tolerance is not met.

// tolerance - Stop and assert convergence if norm of residual is <=
//             to tolerance.

// niters - On output, the number of iterations actually performed.

/////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <fstream>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#define USE_MPI_EXTENSION
#include "crpm.h"
#include "crpm_mpi.h"
#include <mpi.h>
#include <mpi-ext.h>
using std::cout;
using std::cerr;
using std::endl;
#include <cmath>
#include "mytimer.hpp"
#include "HPCCG.hpp"
#include <sys/time.h>

#define TICK()  t0 = mytimer() // Use TICK and TOCK to time a code section
#define TOCK(t) t += mytimer() - t0

extern crpm_mpi_t *g_pool;

/* XXX: Definition of variables: promoted to globals to persist longjmp  */
double t_begin;
double t0, t1, t2, t3, t4;
#ifdef USING_MPI
double t5;
#endif
int nrow;
int ncol;

double * r;
double * p;
double * Ap;

double rtrans;
double oldrtrans;

size_t CP_SIZE;
char *cp_data;
int fi_iter, fi_rank;

int k;
int print_freq;
int rank;

/* ====================================== */


int HPCCG(HPC_Sparse_Matrix * A,
    const double * const b, double * const x,
    const int max_iter, const double tolerance, int &niters, double & normr,
    double * times,
    int cp_iters, bool procfi, bool nodefi, int level)

{
  // Initialize data when new or restarted
  t_begin = mytimer();  // Start timing right away

  t0 = 0.0, t1 = 0.0, t2 = 0.0, t3 = 0.0, t4 = 0.0;
#ifdef USING_MPI
  t5 = 0.0;
#endif
  nrow = A->local_nrow;
  ncol = A->local_ncol;

  bool do_init = false;
  r = (double *) crpm_get_root(g_pool->pool, 0);
  crpm_protect(g_pool, 2, &normr, sizeof(double));
  crpm_protect(g_pool, 3, &rtrans, sizeof(double));
  crpm_protect(g_pool, 4, &niters, sizeof(int));
  crpm_protect(g_pool, 5, &k, sizeof(int));
  if (r) {
    p = (double *) crpm_get_root(g_pool->pool, 1);
    k++;
    if (rank==0) cout << "Restart Residual = "<< normr << endl;
  } else {
    do_init = true;
    r = (double *) crpm_malloc(g_pool->pool, sizeof(double) * nrow);
    p = (double *) crpm_malloc(g_pool->pool, sizeof(double) * ncol); // In parallel case, A is rectangular
    crpm_set_root(g_pool->pool, 0, r);
    crpm_set_root(g_pool->pool, 1, p);
    normr = 0.0;
    rtrans = 0.0;
    k = 1;
  }

  Ap = new double [nrow];
  oldrtrans = 0.0;

  // compute size saving, p, normr, rtrans, niters, iter
  CP_SIZE = sizeof(double) * ( 1 * nrow + ncol + 2 ) + sizeof(int) * 2;
  // buffer to save checkpoint data
  cp_data = new char[CP_SIZE];

#ifdef USING_MPI
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#else
  rank = 0; // Serial case (not using MPI)
#endif
  print_freq = 5;

  if (do_init) {
    // p is of length ncols, copy x to p for sparse MV operation
    TICK(); waxpby(nrow, 1.0, x, 0.0, x, p); TOCK(t2);
#ifdef USING_MPI
    TICK(); exchange_externals(A,p); TOCK(t5);
#endif
    TICK(); HPC_sparsemv(A, p, Ap); TOCK(t3);
    TICK(); waxpby(nrow, 1.0, b, -1.0, Ap, r); TOCK(t2);
    TICK(); ddot(nrow, r, r, &rtrans, t4); TOCK(t1);
    normr = sqrt(rtrans);

    if (rank==0) cout << "Initial Residual = "<< normr << endl;
  }

  double ckpt_time = 0;
  
  for(; k<max_iter && normr > tolerance; k++ )
  {
    if (k == 1)
	  {
	    TICK(); waxpby(nrow, 1.0, r, 0.0, r, p); TOCK(t2);
	  }
    else
	  {
      oldrtrans = rtrans;
      TICK(); ddot (nrow, r, r, &rtrans, t4); TOCK(t1);// 2*nrow ops
      double beta = rtrans/oldrtrans;
      TICK(); waxpby (nrow, 1.0, r, beta, p, p);  TOCK(t2);// 2*nrow ops
	  }
    normr = sqrt(rtrans);
    if (rank==0 && k%print_freq == 0)
	    cout << "Iteration = "<< k << "   Residual = "<< normr << endl;
#ifdef USING_MPI
    TICK(); exchange_externals(A,p); TOCK(t5);
#endif
    TICK(); HPC_sparsemv(A, p, Ap); TOCK(t3); // 2*nnz ops
    double alpha = 0.0;
    TICK(); ddot(nrow, p, Ap, &alpha, t4); TOCK(t1); // 2*nrow ops
    alpha = rtrans/alpha;
    TICK(); waxpby(nrow, 1.0, x, alpha, p, x);// 2*nrow ops
    waxpby(nrow, 1.0, r, -alpha, Ap, r);  TOCK(t2);// 2*nrow ops
    niters = k;

	  if ((k % cp_iters) == 0){
      double tt1 = MPI_Wtime();
		  crpm_mpi_checkpoint(g_pool, 1);
      double tt2 = MPI_Wtime();
      ckpt_time += tt2 - tt1;
	  }
  }

  if (rank == 0) {
    printf("checkpoint time = %.5lf\n", ckpt_time);
  }

  // Store times
  times[1] = t1; // ddot time
  times[2] = t2; // waxpby time
  times[3] = t3; // sparsemv time
  times[4] = t4; // AllReduce time
#ifdef USING_MPI
  times[5] = t5; // exchange boundary time
#endif
  crpm_free(g_pool->pool, p);
  delete [] Ap;
  crpm_free(g_pool->pool, r);
  delete [] cp_data;
  times[0] = mytimer() - t_begin;  // Total time. All done...
  return(0);
}
