#!/bin/bash

function run_bench() {
  rm -rf /mnt/pmem0/libcrpm/*
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank ./CoMD-mpi config.L1.fti -xproc 2 -yproc 2 -zproc 2 -nx $1 -ny $1 -nz $1 -level 1 -cp_stride $2
}

function run_bench_recovery() {
  rm -rf /mnt/pmem0/libcrpm/*
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank timeout -s KILL $3 ./CoMD-mpi config.L1.fti -xproc 2 -yproc 2 -zproc 2 -nx $1 -ny $1 -nz $1 -level 1 -cp_stride $2
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank timeout -s KILL $3 ./CoMD-mpi config.L1.fti -xproc 2 -yproc 2 -zproc 2 -nx $1 -ny $1 -nz $1 -level 1 -cp_stride $2
}

make > /dev/null

# Measure normal execution time
run_bench 128 5
run_bench 160 5
run_bench 192 5

# Kill the processes after 20 secs, then restart the application
# run_bench_recovery 192 5 20
