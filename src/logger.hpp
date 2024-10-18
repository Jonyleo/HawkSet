#ifndef __HAWKSET_LOGGER_HPP__
#define __HAWKSET_LOGGER_HPP__


// ----------------- DEBUGGING INFORMATION OUTPUT ----------------
#ifdef DEBUG
#define debug(...) fprintf(stderr, "[DBG] " __VA_ARGS__)
#else
#define debug(...)
#endif  // !DEBUG

// ------------ INSTRUCTION & TIME INFORMATION TRACKING ----------

#define COUNT_INSTRUCTIONS
//#define COUNT_TIME


#if defined(COUNT_INSTRUCTIONS) || defined(COUNT_TIME)
#include "pin.H"

struct alignas(64) LoggingData {
    uint64_t pm_stores = 0;
    uint64_t stores = 0;
    uint64_t loads = 0;
    uint64_t pm_loads = 0;
    uint64_t flushes = 0;
    uint64_t fences = 0;
    uint64_t rmw = 0;
    uint64_t locks = 0;
    uint64_t unlocks = 0;
    uint64_t wrlocks = 0;
    uint64_t rdlocks = 0;
    uint64_t rwunlocks = 0;
    uint64_t pm_nt_stores = 0;

    uint64_t fence_time = 0;
    uint64_t flush_time = 0;
    uint64_t store_time = 0;
    uint64_t load_time = 0;
    uint64_t rmw_time = 0;
    uint64_t backtrace_time = 0;
    uint64_t instr_time = 0;
    uint64_t ispm_time = 0;
    uint64_t lock_time = 0;

    uint64_t injection_time = 0;
    uint64_t image_time = 0;
};

std::atomic_ulong pm_stores = 0;
std::atomic_ulong stores = 0;
std::atomic_ulong loads = 0;
std::atomic_ulong pm_loads = 0;
std::atomic_ulong flushes = 0;
std::atomic_ulong fences = 0;
std::atomic_ulong rmw = 0;
std::atomic_ulong locks = 0;
std::atomic_ulong unlocks = 0;
std::atomic_ulong wrlocks = 0;
std::atomic_ulong rdlocks = 0;
std::atomic_ulong rwunlocks = 0;
std::atomic_ulong pm_nt_stores = 0;

std::atomic_ulong fence_time = 0;
std::atomic_ulong flush_time = 0;
std::atomic_ulong store_time = 0; 
std::atomic_ulong load_time = 0;
std::atomic_ulong rmw_time = 0;
std::atomic_ulong backtrace_time = 0;
std::atomic_ulong instr_time = 0; 
std::atomic_ulong ispm_time = 0;
std::atomic_ulong lock_time = 0;

std::atomic_ulong injection_time = 0;
std::atomic_ulong image_time = 0;

uint64_t tool_execution_tick = 0;

LoggingData logging_tls[1000];

inline LoggingData * get_logging_data(uint64_t tid) {
    return logging_tls + tid;
}
#endif


#ifdef COUNT_INSTRUCTIONS
#include <iostream>

#define COUNT_PM_STORE get_logging_data(tid)->pm_stores++
#define COUNT_PM_LOAD get_logging_data(tid)->pm_loads++
#define COUNT_PM_NT_STORE get_logging_data(tid)->pm_nt_stores++
#define COUNT_STORE get_logging_data(tid)->stores++
#define COUNT_LOAD get_logging_data(tid)->loads++
#define COUNT_FLUSH get_logging_data(tid)->flushes++
#define COUNT_FENCE get_logging_data(tid)->fences++
#define COUNT_RMW get_logging_data(tid)->rmw++
#define COUNT_LOCK get_logging_data(tid)->locks++
#define COUNT_UNLOCK get_logging_data(tid)->unlocks++
#define COUNT_WRLOCK get_logging_data(tid)->wrlocks++
#define COUNT_RDLOCK get_logging_data(tid)->rdlocks++
#define COUNT_RWUNLOCK get_logging_data(tid)->rwunlocks++
#define SUM_GLOBAL_INSTRUCTION_COUNT \
    pm_stores += get_logging_data(tid)->pm_stores; \
    pm_loads += get_logging_data(tid)->pm_loads; \
    pm_nt_stores += get_logging_data(tid)->pm_nt_stores; \
    stores += get_logging_data(tid)->stores; \
    loads += get_logging_data(tid)->loads; \
    flushes += get_logging_data(tid)->flushes; \
    fences += get_logging_data(tid)->fences; \
    rmw += get_logging_data(tid)->rmw; \
    locks += get_logging_data(tid)->locks; \
    unlocks += get_logging_data(tid)->unlocks; \
    wrlocks += get_logging_data(tid)->wrlocks; \
    rdlocks += get_logging_data(tid)->rdlocks; \
    rwunlocks += get_logging_data(tid)->rwunlocks;
#define OUTPUT_INSTRUCTION_COUNT \
    std::cerr << "-- # of Instructions and Sync Primitives -- " << std::endl; \
    std::cerr << "    pm stores:    " << pm_stores << std::endl; \
    std::cerr << "    pm nt stores: " << pm_nt_stores << std::endl; \
    std::cerr << "    pm loads:     " << pm_loads << std::endl; \
    std::cerr << "    loads:        " << loads << std::endl; \
    std::cerr << "    stores:       " << stores << std::endl; \
    std::cerr << "    flushes:      " << flushes << std::endl; \
    std::cerr << "    fences:       " << fences << std::endl; \
    std::cerr << "    rmw:          " << rmw << std::endl; \
    std::cerr << "    lock:         " << locks << std::endl; \
    std::cerr << "    unlocks:      " << unlocks << std::endl; \
    std::cerr << "    read lock:    " << rdlocks << std::endl; \
    std::cerr << "    write lock:   " << wrlocks << std::endl; \
    std::cerr << "    rd/wr unlock: " << rwunlocks << std::endl << std::endl; 
#else
#define COUNT_PM_STORE
#define COUNT_PM_LOAD
#define COUNT_PM_NT_STORE
#define COUNT_STORE
#define COUNT_LOAD
#define COUNT_FLUSH
#define COUNT_FENCE
#define COUNT_RMW
#define COUNT_LOCK
#define COUNT_UNLOCK
#define COUNT_WRLOCK
#define COUNT_RDLOCK
#define COUNT_RWUNLOCK
#define SUM_GLOBAL_INSTRUCTION_COUNT
#define OUTPUT_INSTRUCTION_COUNT
#endif

// ----------------- TIME INFORMATION TRACKING ----------------


long realtime() {
    struct timespec t;

    clock_gettime(CLOCK_REALTIME, &t);

    return t.tv_sec * 1000000000 + t.tv_nsec;
}

#ifdef COUNT_TIME
#include <iostream>
#include <atomic>
#include <time.h>

static inline unsigned long long getticks(void)
{
    unsigned long long lo;
    unsigned long long hi;

    // RDTSC copies contents of 64-bit TSC into EDX:EAX
    asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
    asm volatile("shl $32, %0\n\t"
                 "or %1, %0\n"
                 : "=d" (hi)
                 : "a" (lo));
                 
    return hi;
}

/*static inline unsigned long long getticks(void)
{
    unsigned int lo, hi;

    // RDTSC copies contents of 64-bit TSC into EDX:EAX
    asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
    return (unsigned long long)hi << 32 | lo;
}*/

#define COUNT_TIME_GENERIC2(V1, V2, B) \
long _time = getticks(); \
get_logging_data(tid)->V1 -= _time; \
get_logging_data(tid)->V2 -= _time; \
B \
_time = getticks(); \
get_logging_data(tid)->V1 += _time; \
get_logging_data(tid)->V2 += _time;

#define COUNT_TIME_GENERIC(V, B) get_logging_data(tid)->V -= getticks(); \
B \
get_logging_data(tid)->V += getticks();

#define UNCOUNT_TIME_GENERIC(V, B) get_logging_data(tid)->V += getticks(); \
B \
get_logging_data(tid)->V -= getticks();

#define OUTPUT_TIME_GENERIC(V, M) std::cerr << M << (double) V / 1000000000 << std::endl;
#define COUNT_FENCE_TIME(B) COUNT_TIME_GENERIC(fence_time, B)
#define COUNT_FLUSH_TIME(B) COUNT_TIME_GENERIC(flush_time, B)
#define COUNT_STORE_TIME(B) COUNT_TIME_GENERIC(store_time, B)
#define COUNT_LOAD_TIME(B) COUNT_TIME_GENERIC(load_time, B)
#define COUNT_RMW_TIME(B) COUNT_TIME_GENERIC(rmw_time, B)
#define INIT_TIME_COUNT tool_execution_tick -= getticks();
#define FINISH_TIME_COUNT tool_execution_tick += getticks();
#define SUM_GLOBAL_TIME_COUNT \
    fence_time += get_logging_data(tid)->fence_time; \
    flush_time += get_logging_data(tid)->flush_time; \
    store_time += get_logging_data(tid)->store_time; \
    load_time += get_logging_data(tid)->load_time; \
    rmw_time += get_logging_data(tid)->rmw_time; \
    backtrace_time += get_logging_data(tid)->backtrace_time; \
    instr_time += get_logging_data(tid)->instr_time; \
    ispm_time += get_logging_data(tid)->ispm_time; \
    lock_time += get_logging_data(tid)->lock_time; \
    injection_time += get_logging_data(tid)->injection_time; \
    image_time += get_logging_data(tid)->image_time; 

#define OUTPUT_TIME_COUNT \
std::cerr << "-- Time Breakdown (% of Instrumentation) --" << std::endl; \
std::cerr << "    store:     " << 100 * (double) store_time     / instr_time << "%" << std::endl; \
std::cerr << "    load:      " << 100 * (double) load_time      / instr_time << "%" << std::endl; \
std::cerr << "    flush:     " << 100 * (double) flush_time     / instr_time << "%" << std::endl; \
std::cerr << "    fence:     " << 100 * (double) fence_time     / instr_time << "%" << std::endl; \
std::cerr << "    rmw:       " << 100 * (double) rmw_time       / instr_time << "%" << std::endl; \
std::cerr << "    lock:      " << 100 * (double) lock_time      / instr_time << "%" << std::endl; \
std::cerr << "    backtrace: " << 100 * (double) backtrace_time / instr_time << "%" << std::endl; \
std::cerr << "    pm check:  " << 100 * (double) ispm_time      / instr_time << "%" << std::endl << std::endl; \
std::cerr << "-- Time Breakdown (% of PM Instructions) --" << std::endl; \
std::cerr << "    backtrace: " << 100 * (double) backtrace_time / \
(store_time+load_time+fence_time+rmw_time) << "%" << std::endl << std::endl; \
std::cerr << "-- Time Breakdown (% of Total Execution) --" << std::endl; \
std::cerr << "    injection:       " << 100 * (double) injection_time / tool_execution_tick << "%" << std::endl; \
std::cerr << "    image:           " << 100 * (double) image_time     / tool_execution_tick << "%" << std::endl << std::endl;

#define OUTPUT_CHECK_TIME_COUNT \
std::cerr << "analysis time (s): " << (double) analysis_time / 1000000000 << std::endl;

#else
#define COUNT_TIME_GENERIC2(V1, V2, B) B
#define COUNT_TIME_GENERIC(V, B) B
#define UNCOUNT_TIME_GENERIC(V, B) B
#define OUTPUT_TIME_GENERIC(V, M)
#define COUNT_FENCE_TIME(B) B
#define COUNT_FLUSH_TIME(B) B
#define COUNT_STORE_TIME(B) B 
#define COUNT_LOAD_TIME(B) B
#define COUNT_RMW_TIME(B) B
#define START_COUNT_CHECK_TIME
#define END_COUNT_CHECK_TIME
#define SUM_GLOBAL_TIME_COUNT
#define OUTPUT_TIME_COUNT
#define OUTPUT_CHECK_TIME_COUNT
#define INIT_TIME_COUNT
#define FINISH_TIME_COUNT
#endif


#endif  // !__HAWKSET_LOGGER_H__