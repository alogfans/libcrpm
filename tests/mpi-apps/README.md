# Evaluate MPI programs
The `tests/mpi-apps` directory contains three MPI applications, transformed by `libcrpm`. Before executing any of these applications, ensure that the kernel flush module and `libcrpm` have been built and installed.

Usage: for each test, run `make` to build the binary code (do not use `make -j` which may lead instrumentation failure); run `runme.sh` to perform end-to-end tests (three problem scales, described in our paper).

The script may remove data in `/mnt/pmem0/libcrpm` silently. DO NOT put any valuable data in this directory.