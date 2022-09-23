#ifndef CoMD_info_hpp
#define CoMD_info_hpp

#define CoMD_VARIANT "CoMD-mpi"
#define CoMD_HOSTNAME "optane04"
#define CoMD_KERNEL_NAME "'Linux'"
#define CoMD_KERNEL_RELEASE "'5.4.0-124-generic'"
#define CoMD_PROCESSOR "'x86_64'"

#define CoMD_COMPILER "'/usr/bin/clang'"
#define CoMD_COMPILER_VERSION "'clang version 10.0.0-4ubuntu1 '"
#define CoMD_CFLAGS "'-std=c11 -D_GNU_SOURCE -DDOUBLE -DTIMER -DDO_MPI -O3   -flto -Xclang -load -Xclang ../../../build/instrumentation/libcrpm-opt.so -fno-unroll-loops -I../../../runtime/include -I/home/renfeng/.local/openmpi-4.1.1/install/include -pthread'"
#define CoMD_LDFLAGS "' -L../../../build/runtime -lcrpm_mpi -lnuma -pthread -Wl,-rpath -Wl,/home/renfeng/.local/openmpi-4.1.1/install/lib -Wl,--enable-new-dtags -L/home/renfeng/.local/openmpi-4.1.1/install/lib -lmpi -lm -flto -Xclang -load -Xclang ../../../build/instrumentation/libcrpm-opt.so -fno-unroll-loops'"

#endif
