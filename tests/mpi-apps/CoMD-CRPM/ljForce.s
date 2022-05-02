; ModuleID = 'ljForce.c'
source_filename = "ljForce.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.BasePotentialSt = type { double, double, double, [8 x i8], [3 x i8], i32, i32 (%struct.SimFlatSt*)*, void (%struct._IO_FILE*, %struct.BasePotentialSt*)*, {}* }
%struct.SimFlatSt = type { i32, i32, double, %struct.DomainSt*, %struct.LinkCellSt*, %struct.AtomsSt*, %struct.SpeciesDataSt*, double, double, %struct.BasePotentialSt*, %struct.HaloExchangeSt* }
%struct.DomainSt = type { [3 x i32], [3 x i32], [3 x double], [3 x double], [3 x double], [3 x double], [3 x double], [3 x double] }
%struct.LinkCellSt = type { [3 x i32], i32, i32, i32, [3 x double], [3 x double], [3 x double], [3 x double], i32* }
%struct.AtomsSt = type { i32, i32, i32*, i32*, [3 x double]*, [3 x double]*, [3 x double]*, double* }
%struct.SpeciesDataSt = type { [3 x i8], i32, double }
%struct.HaloExchangeSt = type { [6 x i32], i32, i32 (i8*, i8*, i32, i8*)*, void (i8*, i8*, i32, i32, i8*)*, void (i8*)*, i8* }
%struct._IO_FILE = type { i32, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, %struct._IO_marker*, %struct._IO_FILE*, i32, i32, i64, i16, i8, [1 x i8], i8*, i64, %struct._IO_codecvt*, %struct._IO_wide_data*, %struct._IO_FILE*, i8*, i64, i32, [20 x i8] }
%struct._IO_marker = type opaque
%struct._IO_codecvt = type opaque
%struct._IO_wide_data = type opaque
%struct.LjPotentialSt = type { double, double, double, [8 x i8], [3 x i8], i32, i32 (%struct.SimFlatSt*)*, void (%struct._IO_FILE*, %struct.BasePotentialSt*)*, void (%struct.BasePotentialSt**)*, double, double }

@.str.1 = private unnamed_addr constant [3 x i8] c"Cu\00", align 1
@.str.2 = private unnamed_addr constant [36 x i8] c"  Potential type   : Lennard-Jones\0A\00", align 1
@.str.3 = private unnamed_addr constant [25 x i8] c"  Species name     : %s\0A\00", align 1
@.str.4 = private unnamed_addr constant [25 x i8] c"  Atomic number    : %d\0A\00", align 1
@.str.5 = private unnamed_addr constant [30 x i8] c"  Mass             : %lg amu\0A\00", align 1
@.str.6 = private unnamed_addr constant [25 x i8] c"  Lattice Type     : %s\0A\00", align 1
@.str.7 = private unnamed_addr constant [36 x i8] c"  Lattice spacing  : %lg Angstroms\0A\00", align 1
@.str.8 = private unnamed_addr constant [36 x i8] c"  Cutoff           : %lg Angstroms\0A\00", align 1
@.str.9 = private unnamed_addr constant [29 x i8] c"  Epsilon          : %lg eV\0A\00", align 1
@.str.10 = private unnamed_addr constant [36 x i8] c"  Sigma            : %lg Angstroms\0A\00", align 1
@.str.11 = private unnamed_addr constant [8 x i8] c"jBox>=0\00", align 1
@.str.12 = private unnamed_addr constant [10 x i8] c"ljForce.c\00", align 1
@__PRETTY_FUNCTION__.ljForce = private unnamed_addr constant [23 x i8] c"int ljForce(SimFlat *)\00", align 1

; Function Attrs: nounwind uwtable
define dso_local void @ljDestroy(%struct.BasePotentialSt** %0) #0 {
  %2 = icmp eq %struct.BasePotentialSt** %0, null
  br i1 %2, label %10, label %3

3:                                                ; preds = %1
  %4 = bitcast %struct.BasePotentialSt** %0 to %struct.LjPotentialSt**
  %5 = load %struct.LjPotentialSt*, %struct.LjPotentialSt** %4, align 8, !tbaa !2
  %6 = icmp eq %struct.LjPotentialSt* %5, null
  br i1 %6, label %10, label %7

7:                                                ; preds = %3
  %8 = bitcast %struct.LjPotentialSt* %5 to i8*
  tail call void @crpm_default_free(i8* nonnull %8) #8
  %9 = ptrtoint %struct.BasePotentialSt** %0 to i64
  call void @__crpm_hook_rt_store(i64 %9) #8
  store %struct.BasePotentialSt* null, %struct.BasePotentialSt** %0, align 8, !tbaa !2
  br label %10

10:                                               ; preds = %7, %3, %1
  ret void
}

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local %struct.BasePotentialSt* @initLjPot() local_unnamed_addr #0 {
  %1 = tail call i8* @crpm_default_malloc(i64 80) #8
  %2 = getelementptr inbounds i8, i8* %1, i64 40
  %3 = bitcast i8* %2 to i32 (%struct.SimFlatSt*)**
  %4 = ptrtoint i8* %2 to i64
  call void @__crpm_hook_rt_store(i64 %4) #8
  store i32 (%struct.SimFlatSt*)* @ljForce, i32 (%struct.SimFlatSt*)** %3, align 8, !tbaa !6
  %5 = getelementptr inbounds i8, i8* %1, i64 48
  %6 = bitcast i8* %5 to void (%struct._IO_FILE*, %struct.BasePotentialSt*)**
  %7 = ptrtoint i8* %5 to i64
  call void @__crpm_hook_rt_store(i64 %7) #8
  store void (%struct._IO_FILE*, %struct.BasePotentialSt*)* @ljPrint, void (%struct._IO_FILE*, %struct.BasePotentialSt*)** %6, align 8, !tbaa !10
  %8 = getelementptr inbounds i8, i8* %1, i64 56
  %9 = bitcast i8* %8 to void (%struct.BasePotentialSt**)**
  %10 = ptrtoint i8* %8 to i64
  call void @__crpm_hook_rt_store(i64 %10) #8
  store void (%struct.BasePotentialSt**)* @ljDestroy, void (%struct.BasePotentialSt**)** %9, align 8, !tbaa !11
  %11 = getelementptr inbounds i8, i8* %1, i64 64
  %12 = bitcast i8* %11 to <2 x double>*
  %13 = ptrtoint i8* %11 to i64
  call void @__crpm_hook_rt_store(i64 %13) #8
  store <2 x double> <double 2.315000e+00, double 1.670000e-01>, <2 x double>* %12, align 8, !tbaa !12
  %14 = getelementptr inbounds i8, i8* %1, i64 16
  %15 = bitcast i8* %14 to double*
  %16 = ptrtoint i8* %14 to i64
  call void @__crpm_hook_rt_store(i64 %16) #8
  store double 3.615000e+00, double* %15, align 8, !tbaa !13
  %17 = getelementptr inbounds i8, i8* %1, i64 24
  %18 = bitcast i8* %17 to i32*
  %19 = ptrtoint i8* %17 to i64
  call void @__crpm_hook_rt_store(i64 %19) #8
  store i32 4408134, i32* %18, align 1
  %20 = bitcast i8* %1 to <2 x double>*
  %21 = ptrtoint i8* %1 to i64
  call void @__crpm_hook_rt_store(i64 %21) #8
  store <2 x double> <double 5.787500e+00, double 0x40B9BA7E39DCDE3E>, <2 x double>* %20, align 8, !tbaa !12
  %22 = getelementptr inbounds i8, i8* %1, i64 32
  %23 = ptrtoint i8* %22 to i64
  call void @__crpm_hook_rt_range_store(i64 %23, i64 3) #8
  tail call void @llvm.memcpy.p0i8.p0i8.i64(i8* nonnull align 1 dereferenceable(3) %22, i8* nonnull align 1 dereferenceable(3) getelementptr inbounds ([3 x i8], [3 x i8]* @.str.1, i64 0, i64 0), i64 3, i1 false) #8
  %24 = getelementptr inbounds i8, i8* %1, i64 36
  %25 = bitcast i8* %24 to i32*
  %26 = ptrtoint i8* %24 to i64
  call void @__crpm_hook_rt_store(i64 %26) #8
  store i32 29, i32* %25, align 4, !tbaa !14
  %27 = bitcast i8* %1 to %struct.BasePotentialSt*
  ret %struct.BasePotentialSt* %27
}

; Function Attrs: nounwind uwtable
define internal i32 @ljForce(%struct.SimFlatSt* nocapture %0) #0 {
  %2 = alloca [27 x i32], align 16
  %3 = alloca [3 x double], align 16
  %4 = getelementptr inbounds %struct.SimFlatSt, %struct.SimFlatSt* %0, i64 0, i32 9
  %5 = bitcast %struct.BasePotentialSt** %4 to %struct.LjPotentialSt**
  %6 = load %struct.LjPotentialSt*, %struct.LjPotentialSt** %5, align 8, !tbaa !15
  %7 = getelementptr inbounds %struct.LjPotentialSt, %struct.LjPotentialSt* %6, i64 0, i32 9
  %8 = load double, double* %7, align 8, !tbaa !17
  %9 = getelementptr inbounds %struct.LjPotentialSt, %struct.LjPotentialSt* %6, i64 0, i32 10
  %10 = load double, double* %9, align 8, !tbaa !18
  %11 = getelementptr inbounds %struct.LjPotentialSt, %struct.LjPotentialSt* %6, i64 0, i32 0
  %12 = load double, double* %11, align 8, !tbaa !19
  %13 = fmul double %12, %12
  %14 = getelementptr inbounds %struct.SimFlatSt, %struct.SimFlatSt* %0, i64 0, i32 7
  store double 0.000000e+00, double* %14, align 8, !tbaa !20
  %15 = getelementptr inbounds %struct.SimFlatSt, %struct.SimFlatSt* %0, i64 0, i32 4
  %16 = load %struct.LinkCellSt*, %struct.LinkCellSt** %15, align 8, !tbaa !21
  %17 = getelementptr inbounds %struct.LinkCellSt, %struct.LinkCellSt* %16, i64 0, i32 3
  %18 = load i32, i32* %17, align 4, !tbaa !22
  %19 = shl nsw i32 %18, 6
  %20 = getelementptr inbounds %struct.SimFlatSt, %struct.SimFlatSt* %0, i64 0, i32 5
  %21 = load %struct.AtomsSt*, %struct.AtomsSt** %20, align 8, !tbaa !24
  %22 = getelementptr inbounds %struct.AtomsSt, %struct.AtomsSt* %21, i64 0, i32 7
  %23 = load double*, double** %22, align 8, !tbaa !25
  %24 = bitcast double* %23 to i8*
  %25 = getelementptr inbounds %struct.AtomsSt, %struct.AtomsSt* %21, i64 0, i32 6
  %26 = load [3 x double]*, [3 x double]** %25, align 8, !tbaa !27
  %27 = bitcast [3 x double]* %26 to i8*
  %28 = sext i32 %19 to i64
  %29 = shl nsw i64 %28, 3
  tail call void @AnnotateCheckpointRegion(i8* %24, i64 %29) #8
  %30 = mul nsw i64 %28, 24
  tail call void @AnnotateCheckpointRegion(i8* %27, i64 %30) #8
  %31 = icmp sgt i32 %18, 0
  br i1 %31, label %32, label %34

32:                                               ; preds = %1
  br label %54

33:                                               ; preds = %54
  br label %34

34:                                               ; preds = %33, %1
  %35 = fmul double %8, %8
  %36 = fmul double %8, %35
  %37 = fmul double %8, %36
  %38 = fmul double %8, %37
  %39 = fmul double %8, %38
  %40 = fmul double %13, %13
  %41 = fmul double %13, %40
  %42 = fdiv double %39, %41
  %43 = fadd double %42, -1.000000e+00
  %44 = fmul double %42, %43
  %45 = bitcast [27 x i32]* %2 to i8*
  call void @llvm.lifetime.start.p0i8(i64 108, i8* nonnull %45) #8
  %46 = load %struct.LinkCellSt*, %struct.LinkCellSt** %15, align 8, !tbaa !21
  %47 = getelementptr inbounds %struct.LinkCellSt, %struct.LinkCellSt* %46, i64 0, i32 1
  %48 = load i32, i32* %47, align 4, !tbaa !28
  %49 = icmp sgt i32 %48, 0
  br i1 %49, label %50, label %63

50:                                               ; preds = %34
  %51 = getelementptr inbounds [27 x i32], [27 x i32]* %2, i64 0, i64 0
  %52 = bitcast [3 x double]* %3 to i8*
  %53 = fmul double %10, -4.000000e+00
  br label %67

54:                                               ; preds = %32, %54
  %55 = phi i64 [ %59, %54 ], [ 0, %32 ]
  %56 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %55, i64 0
  %57 = getelementptr inbounds double, double* %23, i64 %55
  %58 = bitcast double* %56 to i8*
  call void @llvm.memset.p0i8.i64(i8* nonnull align 8 dereferenceable(24) %58, i8 0, i64 24, i1 false)
  store double 0.000000e+00, double* %57, align 8, !tbaa !12
  %59 = add nuw nsw i64 %55, 1
  %60 = icmp slt i64 %59, %28
  br i1 %60, label %54, label %33

61:                                               ; preds = %314
  %62 = phi double [ %316, %314 ]
  br label %63

63:                                               ; preds = %61, %34
  %64 = phi double [ 0.000000e+00, %34 ], [ %62, %61 ]
  %65 = fmul double %64, 4.000000e+00
  %66 = fmul double %10, %65
  store double %66, double* %14, align 8, !tbaa !20
  call void @llvm.lifetime.end.p0i8(i64 108, i8* nonnull %45) #8
  ret i32 0

67:                                               ; preds = %50, %314
  %68 = phi %struct.LinkCellSt* [ %46, %50 ], [ %315, %314 ]
  %69 = phi i64 [ 0, %50 ], [ %317, %314 ]
  %70 = phi i32 [ 0, %50 ], [ %322, %314 ]
  %71 = phi double [ 0.000000e+00, %50 ], [ %316, %314 ]
  %72 = zext i32 %70 to i64
  %73 = getelementptr inbounds %struct.LinkCellSt, %struct.LinkCellSt* %68, i64 0, i32 8
  %74 = load i32*, i32** %73, align 8, !tbaa !29
  %75 = getelementptr inbounds i32, i32* %74, i64 %69
  %76 = load i32, i32* %75, align 4, !tbaa !30
  %77 = icmp eq i32 %76, 0
  br i1 %77, label %314, label %78

78:                                               ; preds = %67
  %79 = trunc i64 %69 to i32
  %80 = call i32 @getNeighborBoxes(%struct.LinkCellSt* nonnull %68, i32 %79, i32* nonnull %51) #8
  %81 = icmp sgt i32 %80, 0
  br i1 %81, label %84, label %82

82:                                               ; preds = %78
  %83 = load %struct.LinkCellSt*, %struct.LinkCellSt** %15, align 8, !tbaa !21
  br label %314

84:                                               ; preds = %78
  %85 = icmp slt i32 %76, 1
  %86 = add i32 %76, %70
  %87 = zext i32 %80 to i64
  br label %88

88:                                               ; preds = %307, %84
  %89 = phi i64 [ 0, %84 ], [ %309, %307 ]
  %90 = phi double [ %71, %84 ], [ %308, %307 ]
  %91 = getelementptr inbounds [27 x i32], [27 x i32]* %2, i64 0, i64 %89
  %92 = load i32, i32* %91, align 4, !tbaa !30
  %93 = icmp sgt i32 %92, -1
  br i1 %93, label %95, label %94

94:                                               ; preds = %88
  call void @__assert_fail(i8* getelementptr inbounds ([8 x i8], [8 x i8]* @.str.11, i64 0, i64 0), i8* getelementptr inbounds ([10 x i8], [10 x i8]* @.str.12, i64 0, i64 0), i32 159, i8* getelementptr inbounds ([23 x i8], [23 x i8]* @__PRETTY_FUNCTION__.ljForce, i64 0, i64 0)) #9
  unreachable

95:                                               ; preds = %88
  %96 = load %struct.LinkCellSt*, %struct.LinkCellSt** %15, align 8, !tbaa !21
  %97 = getelementptr inbounds %struct.LinkCellSt, %struct.LinkCellSt* %96, i64 0, i32 8
  %98 = load i32*, i32** %97, align 8, !tbaa !29
  %99 = zext i32 %92 to i64
  %100 = getelementptr inbounds i32, i32* %98, i64 %99
  %101 = load i32, i32* %100, align 4, !tbaa !30
  %102 = icmp eq i32 %101, 0
  %103 = or i1 %102, %85
  br i1 %103, label %307, label %104

104:                                              ; preds = %95
  %105 = load %struct.AtomsSt*, %struct.AtomsSt** %20, align 8, !tbaa !24
  %106 = getelementptr inbounds %struct.AtomsSt, %struct.AtomsSt* %105, i64 0, i32 2
  %107 = load i32*, i32** %106, align 8, !tbaa !31
  %108 = icmp sgt i32 %101, 0
  %109 = getelementptr inbounds %struct.LinkCellSt, %struct.LinkCellSt* %96, i64 0, i32 1
  %110 = getelementptr inbounds %struct.AtomsSt, %struct.AtomsSt* %105, i64 0, i32 4
  %111 = shl i32 %92, 6
  %112 = sext i32 %111 to i64
  br label %113

113:                                              ; preds = %217, %104
  %114 = phi i64 [ %72, %104 ], [ %219, %217 ]
  %115 = phi double [ %90, %104 ], [ %218, %217 ]
  %116 = getelementptr inbounds i32, i32* %107, i64 %114
  %117 = load i32, i32* %116, align 4, !tbaa !30
  br i1 %108, label %118, label %217

118:                                              ; preds = %113
  %119 = load i32, i32* %109, align 4, !tbaa !28
  %120 = icmp slt i32 %92, %119
  %121 = getelementptr inbounds double, double* %23, i64 %114
  br i1 %120, label %122, label %123

122:                                              ; preds = %118
  br label %125

123:                                              ; preds = %118
  %124 = load [3 x double]*, [3 x double]** %110, align 8, !tbaa !32
  br label %222

125:                                              ; preds = %122, %182
  %126 = phi i64 [ %185, %182 ], [ %112, %122 ]
  %127 = phi double [ %183, %182 ], [ %115, %122 ]
  %128 = phi i32 [ %184, %182 ], [ 0, %122 ]
  call void @llvm.lifetime.start.p0i8(i64 24, i8* nonnull %52) #8
  %129 = getelementptr inbounds i32, i32* %107, i64 %126
  %130 = load i32, i32* %129, align 4, !tbaa !30
  %131 = icmp sgt i32 %130, %117
  br i1 %131, label %132, label %182

132:                                              ; preds = %125
  %133 = load [3 x double]*, [3 x double]** %110, align 8, !tbaa !32
  br label %187

134:                                              ; preds = %187
  %135 = fdiv double 1.000000e+00, %211
  %136 = fmul double %135, %135
  %137 = fmul double %135, %136
  %138 = fmul double %39, %137
  %139 = fadd double %138, -1.000000e+00
  %140 = fmul double %138, %139
  %141 = fsub double %140, %44
  %142 = fmul double %141, 5.000000e-01
  %143 = load double, double* %121, align 8, !tbaa !12
  %144 = fadd double %142, %143
  store double %144, double* %121, align 8, !tbaa !12
  %145 = getelementptr inbounds double, double* %23, i64 %126
  %146 = load double, double* %145, align 8, !tbaa !12
  %147 = fadd double %142, %146
  store double %147, double* %145, align 8, !tbaa !12
  %148 = fmul double %53, %138
  %149 = fmul double %135, %148
  %150 = fmul double %138, 1.200000e+01
  %151 = fadd double %150, -6.000000e+00
  %152 = fmul double %149, %151
  br label %153

153:                                              ; preds = %134
  %154 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 0
  %155 = load double, double* %154, align 8, !tbaa !12
  %156 = fmul double %152, %155
  %157 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %114, i64 0
  %158 = load double, double* %157, align 8, !tbaa !12
  %159 = fsub double %158, %156
  store double %159, double* %157, align 8, !tbaa !12
  %160 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %126, i64 0
  %161 = load double, double* %160, align 8, !tbaa !12
  %162 = fadd double %156, %161
  store double %162, double* %160, align 8, !tbaa !12
  %163 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 1
  %164 = load double, double* %163, align 8, !tbaa !12
  %165 = fmul double %152, %164
  %166 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %114, i64 1
  %167 = load double, double* %166, align 8, !tbaa !12
  %168 = fsub double %167, %165
  store double %168, double* %166, align 8, !tbaa !12
  %169 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %126, i64 1
  %170 = load double, double* %169, align 8, !tbaa !12
  %171 = fadd double %165, %170
  store double %171, double* %169, align 8, !tbaa !12
  %172 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 2
  %173 = load double, double* %172, align 8, !tbaa !12
  %174 = fmul double %152, %173
  %175 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %114, i64 2
  %176 = load double, double* %175, align 8, !tbaa !12
  %177 = fsub double %176, %174
  store double %177, double* %175, align 8, !tbaa !12
  %178 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %126, i64 2
  %179 = load double, double* %178, align 8, !tbaa !12
  %180 = fadd double %174, %179
  store double %180, double* %178, align 8, !tbaa !12
  %181 = fadd double %127, %141
  br label %182

182:                                              ; preds = %153, %187, %125
  %183 = phi double [ %127, %125 ], [ %127, %187 ], [ %181, %153 ]
  call void @llvm.lifetime.end.p0i8(i64 24, i8* nonnull %52) #8
  %184 = add nuw nsw i32 %128, 1
  %185 = add nsw i64 %126, 1
  %186 = icmp eq i32 %184, %101
  br i1 %186, label %213, label %125

187:                                              ; preds = %132
  %188 = getelementptr inbounds [3 x double], [3 x double]* %133, i64 %114, i64 0
  %189 = load double, double* %188, align 8, !tbaa !12
  %190 = getelementptr inbounds [3 x double], [3 x double]* %133, i64 %126, i64 0
  %191 = load double, double* %190, align 8, !tbaa !12
  %192 = fsub double %189, %191
  %193 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 0
  store double %192, double* %193, align 8, !tbaa !12
  %194 = fmul double %192, %192
  %195 = fadd double 0.000000e+00, %194
  %196 = getelementptr inbounds [3 x double], [3 x double]* %133, i64 %114, i64 1
  %197 = load double, double* %196, align 8, !tbaa !12
  %198 = getelementptr inbounds [3 x double], [3 x double]* %133, i64 %126, i64 1
  %199 = load double, double* %198, align 8, !tbaa !12
  %200 = fsub double %197, %199
  %201 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 1
  store double %200, double* %201, align 8, !tbaa !12
  %202 = fmul double %200, %200
  %203 = fadd double %195, %202
  %204 = getelementptr inbounds [3 x double], [3 x double]* %133, i64 %114, i64 2
  %205 = load double, double* %204, align 8, !tbaa !12
  %206 = getelementptr inbounds [3 x double], [3 x double]* %133, i64 %126, i64 2
  %207 = load double, double* %206, align 8, !tbaa !12
  %208 = fsub double %205, %207
  %209 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 2
  store double %208, double* %209, align 8, !tbaa !12
  %210 = fmul double %208, %208
  %211 = fadd double %203, %210
  %212 = fcmp ogt double %211, %13
  br i1 %212, label %182, label %134

213:                                              ; preds = %182
  %214 = phi double [ %183, %182 ]
  br label %217

215:                                              ; preds = %300
  %216 = phi double [ %301, %300 ]
  br label %217

217:                                              ; preds = %215, %213, %113
  %218 = phi double [ %115, %113 ], [ %214, %213 ], [ %216, %215 ]
  %219 = add nuw nsw i64 %114, 1
  %220 = trunc i64 %219 to i32
  %221 = icmp eq i32 %86, %220
  br i1 %221, label %305, label %113

222:                                              ; preds = %300, %123
  %223 = phi i64 [ %112, %123 ], [ %303, %300 ]
  %224 = phi double [ %115, %123 ], [ %301, %300 ]
  %225 = phi i32 [ 0, %123 ], [ %302, %300 ]
  call void @llvm.lifetime.start.p0i8(i64 24, i8* nonnull %52) #8
  br label %226

226:                                              ; preds = %222
  %227 = getelementptr inbounds [3 x double], [3 x double]* %124, i64 %114, i64 0
  %228 = load double, double* %227, align 8, !tbaa !12
  %229 = getelementptr inbounds [3 x double], [3 x double]* %124, i64 %223, i64 0
  %230 = load double, double* %229, align 8, !tbaa !12
  %231 = fsub double %228, %230
  %232 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 0
  store double %231, double* %232, align 8, !tbaa !12
  %233 = fmul double %231, %231
  %234 = fadd double 0.000000e+00, %233
  %235 = getelementptr inbounds [3 x double], [3 x double]* %124, i64 %114, i64 1
  %236 = load double, double* %235, align 8, !tbaa !12
  %237 = getelementptr inbounds [3 x double], [3 x double]* %124, i64 %223, i64 1
  %238 = load double, double* %237, align 8, !tbaa !12
  %239 = fsub double %236, %238
  %240 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 1
  store double %239, double* %240, align 8, !tbaa !12
  %241 = fmul double %239, %239
  %242 = fadd double %234, %241
  %243 = getelementptr inbounds [3 x double], [3 x double]* %124, i64 %114, i64 2
  %244 = load double, double* %243, align 8, !tbaa !12
  %245 = getelementptr inbounds [3 x double], [3 x double]* %124, i64 %223, i64 2
  %246 = load double, double* %245, align 8, !tbaa !12
  %247 = fsub double %244, %246
  %248 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 2
  store double %247, double* %248, align 8, !tbaa !12
  %249 = fmul double %247, %247
  %250 = fadd double %242, %249
  %251 = fcmp ogt double %250, %13
  br i1 %251, label %300, label %252

252:                                              ; preds = %226
  %253 = fdiv double 1.000000e+00, %250
  %254 = fmul double %253, %253
  %255 = fmul double %253, %254
  %256 = fmul double %39, %255
  %257 = fadd double %256, -1.000000e+00
  %258 = fmul double %256, %257
  %259 = fsub double %258, %44
  %260 = fmul double %259, 5.000000e-01
  %261 = load double, double* %121, align 8, !tbaa !12
  %262 = fadd double %260, %261
  store double %262, double* %121, align 8, !tbaa !12
  %263 = getelementptr inbounds double, double* %23, i64 %223
  %264 = load double, double* %263, align 8, !tbaa !12
  %265 = fadd double %260, %264
  store double %265, double* %263, align 8, !tbaa !12
  %266 = fmul double %53, %256
  %267 = fmul double %253, %266
  %268 = fmul double %256, 1.200000e+01
  %269 = fadd double %268, -6.000000e+00
  %270 = fmul double %267, %269
  br label %271

271:                                              ; preds = %252
  %272 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 0
  %273 = load double, double* %272, align 8, !tbaa !12
  %274 = fmul double %270, %273
  %275 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %114, i64 0
  %276 = load double, double* %275, align 8, !tbaa !12
  %277 = fsub double %276, %274
  store double %277, double* %275, align 8, !tbaa !12
  %278 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %223, i64 0
  %279 = load double, double* %278, align 8, !tbaa !12
  %280 = fadd double %274, %279
  store double %280, double* %278, align 8, !tbaa !12
  %281 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 1
  %282 = load double, double* %281, align 8, !tbaa !12
  %283 = fmul double %270, %282
  %284 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %114, i64 1
  %285 = load double, double* %284, align 8, !tbaa !12
  %286 = fsub double %285, %283
  store double %286, double* %284, align 8, !tbaa !12
  %287 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %223, i64 1
  %288 = load double, double* %287, align 8, !tbaa !12
  %289 = fadd double %283, %288
  store double %289, double* %287, align 8, !tbaa !12
  %290 = getelementptr inbounds [3 x double], [3 x double]* %3, i64 0, i64 2
  %291 = load double, double* %290, align 8, !tbaa !12
  %292 = fmul double %270, %291
  %293 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %114, i64 2
  %294 = load double, double* %293, align 8, !tbaa !12
  %295 = fsub double %294, %292
  store double %295, double* %293, align 8, !tbaa !12
  %296 = getelementptr inbounds [3 x double], [3 x double]* %26, i64 %223, i64 2
  %297 = load double, double* %296, align 8, !tbaa !12
  %298 = fadd double %292, %297
  store double %298, double* %296, align 8, !tbaa !12
  %299 = fadd double %224, %260
  br label %300

300:                                              ; preds = %271, %226
  %301 = phi double [ %224, %226 ], [ %299, %271 ]
  call void @llvm.lifetime.end.p0i8(i64 24, i8* nonnull %52) #8
  %302 = add nuw nsw i32 %225, 1
  %303 = add nsw i64 %223, 1
  %304 = icmp eq i32 %302, %101
  br i1 %304, label %215, label %222

305:                                              ; preds = %217
  %306 = phi double [ %218, %217 ]
  br label %307

307:                                              ; preds = %305, %95
  %308 = phi double [ %90, %95 ], [ %306, %305 ]
  %309 = add nuw nsw i64 %89, 1
  %310 = icmp eq i64 %309, %87
  br i1 %310, label %311, label %88

311:                                              ; preds = %307
  %312 = phi double [ %308, %307 ]
  %313 = phi %struct.LinkCellSt* [ %96, %307 ]
  br label %314

314:                                              ; preds = %311, %82, %67
  %315 = phi %struct.LinkCellSt* [ %68, %67 ], [ %83, %82 ], [ %313, %311 ]
  %316 = phi double [ %71, %67 ], [ %71, %82 ], [ %312, %311 ]
  %317 = add nuw nsw i64 %69, 1
  %318 = getelementptr inbounds %struct.LinkCellSt, %struct.LinkCellSt* %315, i64 0, i32 1
  %319 = load i32, i32* %318, align 4, !tbaa !28
  %320 = sext i32 %319 to i64
  %321 = icmp slt i64 %317, %320
  %322 = add i32 %70, 64
  br i1 %321, label %67, label %61
}

; Function Attrs: nofree nounwind uwtable
define internal void @ljPrint(%struct._IO_FILE* nocapture %0, %struct.BasePotentialSt* %1) #2 {
  %3 = tail call i64 @fwrite(i8* getelementptr inbounds ([36 x i8], [36 x i8]* @.str.2, i64 0, i64 0), i64 35, i64 1, %struct._IO_FILE* %0)
  %4 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 0, i32 4, i64 0
  %5 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([25 x i8], [25 x i8]* @.str.3, i64 0, i64 0), i8* nonnull %4)
  %6 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 0, i32 5
  %7 = load i32, i32* %6, align 4, !tbaa !14
  %8 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([25 x i8], [25 x i8]* @.str.4, i64 0, i64 0), i32 %7)
  %9 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 0, i32 1
  %10 = load double, double* %9, align 8, !tbaa !33
  %11 = fdiv double %10, 0x4059E921DD37DC65
  %12 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([30 x i8], [30 x i8]* @.str.5, i64 0, i64 0), double %11)
  %13 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 0, i32 3, i64 0
  %14 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([25 x i8], [25 x i8]* @.str.6, i64 0, i64 0), i8* nonnull %13)
  %15 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 0, i32 2
  %16 = load double, double* %15, align 8, !tbaa !13
  %17 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([36 x i8], [36 x i8]* @.str.7, i64 0, i64 0), double %16)
  %18 = getelementptr %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 0, i32 0
  %19 = load double, double* %18, align 8, !tbaa !19
  %20 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([36 x i8], [36 x i8]* @.str.8, i64 0, i64 0), double %19)
  %21 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 1, i32 1
  %22 = load double, double* %21, align 8, !tbaa !18
  %23 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([29 x i8], [29 x i8]* @.str.9, i64 0, i64 0), double %22)
  %24 = getelementptr inbounds %struct.BasePotentialSt, %struct.BasePotentialSt* %1, i64 1, i32 0
  %25 = load double, double* %24, align 8, !tbaa !17
  %26 = tail call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %0, i8* getelementptr inbounds ([36 x i8], [36 x i8]* @.str.10, i64 0, i64 0), double %25)
  ret void
}

; Function Attrs: norecurse nounwind readnone uwtable
define dso_local i64 @sizeofLJForce(%struct.BasePotentialSt* nocapture readnone %0) local_unnamed_addr #3 {
  ret i64 55
}

; Function Attrs: norecurse nounwind readnone uwtable
define dso_local void @writeLJForce(i8** nocapture %0, %struct.BasePotentialSt* nocapture %1) local_unnamed_addr #3 {
  ret void
}

; Function Attrs: nounwind uwtable
define dso_local %struct.BasePotentialSt* @readLJForce(i8** nocapture %0) local_unnamed_addr #0 {
  %2 = tail call i8* @crpm_default_malloc(i64 80) #8
  %3 = getelementptr inbounds i8, i8* %2, i64 40
  %4 = bitcast i8* %3 to i32 (%struct.SimFlatSt*)**
  %5 = ptrtoint i8* %3 to i64
  call void @__crpm_hook_rt_store(i64 %5) #8
  store i32 (%struct.SimFlatSt*)* @ljForce, i32 (%struct.SimFlatSt*)** %4, align 8, !tbaa !6
  %6 = getelementptr inbounds i8, i8* %2, i64 48
  %7 = bitcast i8* %6 to void (%struct._IO_FILE*, %struct.BasePotentialSt*)**
  %8 = ptrtoint i8* %6 to i64
  call void @__crpm_hook_rt_store(i64 %8) #8
  store void (%struct._IO_FILE*, %struct.BasePotentialSt*)* @ljPrint, void (%struct._IO_FILE*, %struct.BasePotentialSt*)** %7, align 8, !tbaa !10
  %9 = getelementptr inbounds i8, i8* %2, i64 56
  %10 = bitcast i8* %9 to void (%struct.BasePotentialSt**)**
  %11 = ptrtoint i8* %9 to i64
  call void @__crpm_hook_rt_store(i64 %11) #8
  store void (%struct.BasePotentialSt**)* @ljDestroy, void (%struct.BasePotentialSt**)** %10, align 8, !tbaa !11
  %12 = bitcast i8** %0 to i64**
  %13 = load i64*, i64** %12, align 8, !tbaa !2
  %14 = bitcast i8* %2 to i64*
  %15 = ptrtoint i8* %2 to i64
  call void @__crpm_hook_rt_store(i64 %15) #8
  %16 = load i64, i64* %13, align 1
  store i64 %16, i64* %14, align 1
  %17 = load i8*, i8** %0, align 8, !tbaa !2
  %18 = getelementptr inbounds i8, i8* %17, i64 8
  %19 = ptrtoint i8** %0 to i64
  call void @__crpm_hook_rt_store(i64 %19) #8
  store i8* %18, i8** %0, align 8, !tbaa !2
  %20 = getelementptr inbounds i8, i8* %2, i64 8
  %21 = bitcast i8* %18 to i64*
  %22 = bitcast i8* %20 to i64*
  %23 = ptrtoint i8* %20 to i64
  call void @__crpm_hook_rt_store(i64 %23) #8
  %24 = load i64, i64* %21, align 1
  store i64 %24, i64* %22, align 1
  %25 = load i8*, i8** %0, align 8, !tbaa !2
  %26 = getelementptr inbounds i8, i8* %25, i64 8
  %27 = ptrtoint i8** %0 to i64
  call void @__crpm_hook_rt_store(i64 %27) #8
  store i8* %26, i8** %0, align 8, !tbaa !2
  %28 = getelementptr inbounds i8, i8* %2, i64 16
  %29 = bitcast i8* %26 to i64*
  %30 = bitcast i8* %28 to i64*
  %31 = ptrtoint i8* %28 to i64
  call void @__crpm_hook_rt_store(i64 %31) #8
  %32 = load i64, i64* %29, align 1
  store i64 %32, i64* %30, align 1
  %33 = load i8*, i8** %0, align 8, !tbaa !2
  %34 = getelementptr inbounds i8, i8* %33, i64 8
  %35 = ptrtoint i8** %0 to i64
  call void @__crpm_hook_rt_store(i64 %35) #8
  store i8* %34, i8** %0, align 8, !tbaa !2
  %36 = getelementptr inbounds i8, i8* %2, i64 24
  %37 = bitcast i8* %34 to i64*
  %38 = bitcast i8* %36 to i64*
  %39 = ptrtoint i8* %36 to i64
  call void @__crpm_hook_rt_store(i64 %39) #8
  %40 = load i64, i64* %37, align 1
  store i64 %40, i64* %38, align 1
  %41 = load i8*, i8** %0, align 8, !tbaa !2
  %42 = getelementptr inbounds i8, i8* %41, i64 8
  store i8* %42, i8** %0, align 8, !tbaa !2
  %43 = getelementptr inbounds i8, i8* %2, i64 32
  %44 = ptrtoint i8* %43 to i64
  call void @__crpm_hook_rt_range_store(i64 %44, i64 3) #8
  tail call void @llvm.memcpy.p0i8.p0i8.i64(i8* nonnull align 1 dereferenceable(3) %43, i8* nonnull align 1 dereferenceable(3) %42, i64 3, i1 false) #8
  %45 = load i8*, i8** %0, align 8, !tbaa !2
  %46 = getelementptr inbounds i8, i8* %45, i64 3
  %47 = ptrtoint i8** %0 to i64
  call void @__crpm_hook_rt_store(i64 %47) #8
  store i8* %46, i8** %0, align 8, !tbaa !2
  %48 = getelementptr inbounds i8, i8* %2, i64 36
  %49 = bitcast i8* %46 to i32*
  %50 = bitcast i8* %48 to i32*
  %51 = ptrtoint i8* %48 to i64
  call void @__crpm_hook_rt_store(i64 %51) #8
  %52 = load i32, i32* %49, align 1
  store i32 %52, i32* %50, align 1
  %53 = load i8*, i8** %0, align 8, !tbaa !2
  %54 = getelementptr inbounds i8, i8* %53, i64 4
  %55 = ptrtoint i8** %0 to i64
  call void @__crpm_hook_rt_store(i64 %55) #8
  store i8* %54, i8** %0, align 8, !tbaa !2
  %56 = getelementptr inbounds i8, i8* %2, i64 64
  %57 = bitcast i8* %54 to i64*
  %58 = bitcast i8* %56 to i64*
  %59 = ptrtoint i8* %56 to i64
  call void @__crpm_hook_rt_store(i64 %59) #8
  %60 = load i64, i64* %57, align 1
  store i64 %60, i64* %58, align 1
  %61 = load i8*, i8** %0, align 8, !tbaa !2
  %62 = getelementptr inbounds i8, i8* %61, i64 8
  store i8* %62, i8** %0, align 8, !tbaa !2
  %63 = getelementptr inbounds i8, i8* %2, i64 72
  %64 = bitcast i8* %62 to i64*
  %65 = bitcast i8* %63 to i64*
  %66 = ptrtoint i8* %63 to i64
  call void @__crpm_hook_rt_store(i64 %66) #8
  %67 = load i64, i64* %64, align 1
  store i64 %67, i64* %65, align 1
  %68 = load i8*, i8** %0, align 8, !tbaa !2
  %69 = getelementptr inbounds i8, i8* %68, i64 8
  store i8* %69, i8** %0, align 8, !tbaa !2
  %70 = bitcast i8* %2 to %struct.BasePotentialSt*
  ret %struct.BasePotentialSt* %70
}

declare dso_local void @crpm_default_free(i8*) local_unnamed_addr #4

declare dso_local i8* @crpm_default_malloc(i64) local_unnamed_addr #4

; Function Attrs: nofree nounwind
declare dso_local i32 @fprintf(%struct._IO_FILE* nocapture, i8* nocapture readonly, ...) local_unnamed_addr #5

declare dso_local void @AnnotateCheckpointRegion(i8*, i64) local_unnamed_addr #4

declare dso_local i32 @getNeighborBoxes(%struct.LinkCellSt*, i32, i32*) local_unnamed_addr #4

; Function Attrs: noreturn nounwind
declare dso_local void @__assert_fail(i8*, i8*, i32, i8*) local_unnamed_addr #6

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #1

; Function Attrs: nofree nounwind
declare i64 @fwrite(i8* nocapture, i64, i64, %struct._IO_FILE* nocapture) local_unnamed_addr #7

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.memset.p0i8.i64(i8* nocapture writeonly, i8, i64, i1 immarg) #1

declare void @__crpm_hook_rt_store(i64)

declare void @__crpm_hook_rt_range_store(i64, i64)

attributes #0 = { nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { argmemonly nounwind willreturn }
attributes #2 = { nofree nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { norecurse nounwind readnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #5 = { nofree nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #6 = { noreturn nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #7 = { nofree nounwind }
attributes #8 = { nounwind }
attributes #9 = { noreturn nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 10.0.0-4ubuntu1 "}
!2 = !{!3, !3, i64 0}
!3 = !{!"any pointer", !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
!6 = !{!7, !3, i64 40}
!7 = !{!"LjPotentialSt", !8, i64 0, !8, i64 8, !8, i64 16, !4, i64 24, !4, i64 32, !9, i64 36, !3, i64 40, !3, i64 48, !3, i64 56, !8, i64 64, !8, i64 72}
!8 = !{!"double", !4, i64 0}
!9 = !{!"int", !4, i64 0}
!10 = !{!7, !3, i64 48}
!11 = !{!7, !3, i64 56}
!12 = !{!8, !8, i64 0}
!13 = !{!7, !8, i64 16}
!14 = !{!7, !9, i64 36}
!15 = !{!16, !3, i64 64}
!16 = !{!"SimFlatSt", !9, i64 0, !9, i64 4, !8, i64 8, !3, i64 16, !3, i64 24, !3, i64 32, !3, i64 40, !8, i64 48, !8, i64 56, !3, i64 64, !3, i64 72}
!17 = !{!7, !8, i64 64}
!18 = !{!7, !8, i64 72}
!19 = !{!7, !8, i64 0}
!20 = !{!16, !8, i64 48}
!21 = !{!16, !3, i64 24}
!22 = !{!23, !9, i64 20}
!23 = !{!"LinkCellSt", !4, i64 0, !9, i64 12, !9, i64 16, !9, i64 20, !4, i64 24, !4, i64 48, !4, i64 72, !4, i64 96, !3, i64 120}
!24 = !{!16, !3, i64 32}
!25 = !{!26, !3, i64 48}
!26 = !{!"AtomsSt", !9, i64 0, !9, i64 4, !3, i64 8, !3, i64 16, !3, i64 24, !3, i64 32, !3, i64 40, !3, i64 48}
!27 = !{!26, !3, i64 40}
!28 = !{!23, !9, i64 12}
!29 = !{!23, !3, i64 120}
!30 = !{!9, !9, i64 0}
!31 = !{!26, !3, i64 8}
!32 = !{!26, !3, i64 24}
!33 = !{!7, !8, i64 8}
