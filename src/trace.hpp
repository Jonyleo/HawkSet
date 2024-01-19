#ifndef __HAWKSET_TRACE_HPP__
#define __HAWKSET_TRACE_HPP__

#define BACKTRACE_ADDRESSES_LIMIT 50
#define CACHELINE_SIZE 64

#include <map>
#include <vector>
#include <fstream>

namespace trace {
    // Enum to define the type of all instructions being instrumented
    enum Instruction {
        STORE,
        NON_TEMPORAL_STORE,
        CLFLUSH,
        CLFLUSHOPT,
        CLWB,
        FENCE,
        RMW,
        LOAD,

        LOCK,
        TRY_LOCK,
        UNLOCK,

        WRLOCK,
        TRY_WRLOCK,
        
        RDLOCK,
        TRY_RDLOCK,
        RWUNLOCK,
        ERROR
    };

}  // namespace trace



#endif  // !MUMAK_TRACE
