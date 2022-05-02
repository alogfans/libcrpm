//
// Created by Feng Ren on 2021/1/23.
//

#include "internal/engine.h"
#include "internal/engines/noop_engine.h"
#include "internal/engines/nvm_engine.h"
#include "internal/engines/lmc_engine.h"
#include "internal/engines/undolog_engine.h"
#include "internal/engines/nvm_inst_engine.h"
#include "internal/engines/mprotect_engine.h"
#include "internal/engines/dirtybit_engine.h"
#include "internal/engines/hybrid_inst_engine.h"

namespace crpm {
    bool process_instrumented = false;

    Engine *Engine::Open(const char *path, const MemoryPoolOption &option) {
        if (process_instrumented) {
            // Instrumentation is enabled
#ifdef USE_NVM_INST_ENGINE
            if (option.engine_name == "default") {
                return NvmInstEngine::Open(path, option);
            }
#endif

#ifdef USE_HYBRID_INST_ENGINE
            if (option.engine_name == "default") {
                return HybridInstEngine::Open(path, option);
            }
#endif

#ifdef USE_SIMPLE_SHADOW_ENGINE
            if (option.engine_name == "simple") {
                return SimpleShadowEngine::Open(path, option);
            }
#endif

#ifdef USE_UNDOLOG_ENGINE
            if (option.engine_name == "undolog") {
                return UndoLogEngine::Open(path, option);
            }
#endif

#ifdef USE_LMC_ENGINE
            if (option.engine_name == "lmc") {
                return LmcEngine::Open(path, option);
            }
#endif
        } else {
            // Instrumentation is disabled
            if (option.engine_name == "noop") {
                return NoopEngine::Open(path, option);
            }
            if (option.engine_name == "nvm") {
                return NvmEngine::Open(path, option);
            }
            if (option.engine_name == "mprotect") {
                return MProtectEngine::Open(path, option);
            }
            if (option.engine_name == "dirty-bit") {
                return DirtyBitEngine::Open(path, option);
            }
        }

        fprintf(stderr, "unsupported engine %s\n", option.engine_name.c_str());
        return nullptr;
    }

#ifdef USE_MPI_EXTENSION
    Engine *Engine::OpenForMPI(const char *path, const MemoryPoolOption &option, MPI_Comm comm) {
        if (process_instrumented) {
            // Instrumentation is enabled
#ifdef USE_NVM_INST_ENGINE
            if (option.engine_name == "default") {
                return NvmInstEngine::OpenForMPI(path, option, comm);
            }
#endif

#ifdef USE_HYBRID_INST_ENGINE
            if (option.engine_name == "default") {
                return HybridInstEngine::OpenForMPI(path, option, comm);
            }
#endif
        } else {
            if (option.engine_name == "mprotect") {
                return MProtectEngine::OpenForMPI(path, option, comm);
            }
            if (option.engine_name == "dirty-bit") {
                return DirtyBitEngine::OpenForMPI(path, option, comm);
            }
        }
        fprintf(stderr, "unsupported engine %s\n", option.engine_name.c_str());
        return nullptr;
    }
#endif // USE_MPI_EXTENSION
}