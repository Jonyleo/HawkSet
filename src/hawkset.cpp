/*
 *  HawkSet
 */

#include <execinfo.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <mutex>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <array>
#include <utility>

#include "pin.H"
#include "trace.hpp"
#include "utils.hpp"
#include "lockset.hpp"
#include "vector_clock.hpp"
#include "logger.hpp"


KNOB<std::string> KnobOutPath(KNOB_MODE_WRITEONCE, "pintool", "out",
                             "", "directory for tool i/o");

KNOB<std::string> KnobPMMount(KNOB_MODE_WRITEONCE, "pintool", "pm-mount",
                              "/mnt/pmem0/", "PM mount in filesystem");

KNOB<int> KnobInitRemoval(KNOB_MODE_WRITEONCE, "pintool", "irh",
                              "1", "Initialization removal heuristic level");

KNOB<int> KnobBacktraceDepth(KNOB_MODE_WRITEONCE, "pintool", "bt-depth",
                              "50", "Depth of backtraces reported (heavily affects performance)");

KNOB<std::string> KnobConfigFiles(KNOB_MODE_APPEND, "pintool", "cfg",
                              "", "Configuration file (repeatable)");


struct RaceLikelyPoint {
    pLockSet common_set;
    uint16_t clock_i;
    backtrace_t write_trace;
    backtrace_t fence_trace;
    uint64_t address;
    uint32_t size;
    std::bitset<64> mask;

    RaceLikelyPoint(pLockSet l, uint16_t t, backtrace_t wt, backtrace_t ft, uint64_t a, uint32_t s, std::bitset<64> m)
        : common_set(l), clock_i(t), write_trace(wt), fence_trace(ft), address(a), size(s), mask(m) {}
};

struct WriteData {
    pTimedLockSet timed_lockset;
    backtrace_t backtrace;
    uint32_t size;
};

int64_t  lockset_analysis_time = 0;
int64_t  output_time = 0;
int64_t  tool_execution_time = 0;

uint64_t use_init_removal_heuristic_n = 1;
uint64_t backtrace_depth = 50;

std::map<pthread_t, uint64_t> pthread_id_to_tid;

PIN_SEMAPHORE thread_creation_semaphore;

pthread_t* created_pthread_id_address;
uint64_t created_thread_id;

VectorClock creator_thread_clock;
int64_t creator_thread_id = -1;

std::map<uint64_t, VectorClock> exit_threads_clock;

std::map<uint64_t, std::bitset<64>> variable_accessed;
std::map<uint64_t, uint64_t> variable_accessed_map;

PIN_MUTEX variable_accessed_mutex;

PIN_MUTEX thread_creation_mutex;
PIN_MUTEX thread_exit_mutex;
PIN_MUTEX backtrace_mutex;

typedef std::set<pLockSet> lockset_set_t;


struct alignas(64) ThreadData {
    TimedLockSet timed_lockset;
    pTimedLockSet timedlockset_cached = NULL;

    inline pTimedLockSet get_timedlockset() {
        if(timedlockset_cached == NULL)
            timedlockset_cached = timedlockset_cache_get(&timed_lockset);

        return timedlockset_cached;
    }

    std::vector<VectorClock> vector_clocks;

    std::vector<RaceLikelyPoint> race_likely_points;
    
    /*
    Access points:
        (address, backtrace, clock) -> locksets
    */
    std::unordered_map<access_key_t,
        lockset_set_t> access_points;


    /*
    Access points optimized for analysis:
        (address) ->
            (backtrace_t, clock_i) -> locksets
    */
    std::unordered_map<uint64_t,
        std::unordered_map<std::pair<backtrace_t, uint64_t>,
            std::vector<const lockset_set_t*>>> access_points_opt;

    /*
    Cache
        cache_line -> address -> state
    */
    std::unordered_map<uint64_t, 
        std::unordered_map<uint64_t, 
            WriteData>> mem_state;


    std::unordered_map<uint64_t, 
        std::unordered_map<uint64_t, 
            WriteData>> flushed_mem_state;


    //uint64_t pthread_id;
    uint64_t tid;

    uint32_t lock_clock = 0;
    uint64_t try_lock_mutex, try_lock_return_address;

    bool used = false;
};


#define TLS_MAX_SIZE 1000

ThreadData analysis_data_tls[TLS_MAX_SIZE];

ThreadData * get_thread_data(uint64_t tid) {
    return analysis_data_tls + tid;
}

uint32_t get_next_id(ThreadData * tdata) {
    return tdata->lock_clock++;
}


/*

    BUG DETECTION

*/

bool IgnoreAccess_(uint64_t tid, uint64_t address) {
    if(!variable_accessed_map.contains(tid))
        variable_accessed_map[tid] = variable_accessed_map.size();

    tid = variable_accessed_map[tid];
    assert(tid < 64);

    // first thread accessing the address, ignore access
    if(!variable_accessed.contains(address)) {
        variable_accessed[address].set(tid);
        return true;
    }

    // thread accessing the address after limit was reached in previous access
    if(variable_accessed[address].none()) {
        return false;
    }

    // thread repeats access, does not count as new thread
    if(variable_accessed[address].test(tid)) {
        return true;
    }

    variable_accessed[address].set(tid);

    // thread access limit reached, clearing the set implies limit reached for future accesses
    if(variable_accessed[address].count() == use_init_removal_heuristic_n + 1) {
        variable_accessed[address].reset();
        return false;
    // thread access limit not reached
    } else {
        return true;
    }        
}

inline std::bitset<64> IgnoreAccess(uint64_t tid, uint64_t address, uint32_t size) {
    std::bitset<64> mask;

    assert(size <= 64);

    if(use_init_removal_heuristic_n == 0) {
        if(size == 64)
            mask.set();
        else
            mask = std::bitset<64>((((uint64_t)1 << size) - 1U));

        return mask;
    }

    PIN_MutexLock(&variable_accessed_mutex);

    for(size_t i = 0; i < size; i++) {
        mask.set(i, !IgnoreAccess_(tid, address));
    }

    PIN_MutexUnlock(&variable_accessed_mutex);
    return mask;
}

void RegisterAccess(uint64_t tid, uint64_t ip, uint64_t address, uint32_t size, const CONTEXT *ctxt) {
    std::bitset<64> mask;

    mask = IgnoreAccess(tid, address, size);

    if(mask.none())
        return;

COUNT_TIME_GENERIC(backtrace_time, 
#ifdef NO_BACKTRACE
    backtrace_t backtrace = (void *) ip;
#else
    backtrace_t backtrace = GetBacktrace(ctxt, backtrace_depth);
#endif
);
    
    ThreadData * tdata = get_thread_data(tid);
    uint16_t clock_i = tdata->vector_clocks.size()-1;

    access_key_t key = {address, backtrace, clock_i, mask};
    auto & access_points = tdata->access_points[key];

    access_points.insert(tdata->get_timedlockset()->lockset);
}

void ProcessFlush(uint64_t tid, uint64_t ip, trace::Instruction flushtype, uint64_t address) {
    ThreadData * tdata = get_thread_data(tid);

    auto& mem_state = tdata->mem_state;
    auto& flushed_mem_state = tdata->flushed_mem_state;

    uint64_t cache_line = CACHE_LINE(address);

    if(!mem_state.contains(cache_line))
        return;

    for(const auto & entry : mem_state[cache_line]) {
        const WriteData * data = &entry.second;

        flushed_mem_state[cache_line][entry.first]= *data;
    }

    mem_state.erase(cache_line);
}

void ProcessStore(uint64_t tid, uint64_t ip, uint64_t address, uint32_t size, const CONTEXT *ctxt) {
    ThreadData * tdata = get_thread_data(tid);

    IgnoreAccess(tid, address, size);

    uint64_t start = CACHE_LINE(address);
    uint64_t end = CACHE_LINE(address + size - 1);

COUNT_TIME_GENERIC(backtrace_time, 
#ifdef NO_BACKTRACE
    backtrace_t backtrace = (void *) ip;
#else
    backtrace_t backtrace = GetBacktrace(ctxt, backtrace_depth);
#endif
);

    pTimedLockSet timed_lockset = NULL;

    auto mem_state = &tdata->mem_state;

    for(uint64_t cl = start; cl <= end; cl++) {
        uint64_t slice_address = cl;
        if(cl == start)
            slice_address = address;

        uint32_t slice_size = 64;
        if(cl == end)
            slice_size = size % 64;

        if(timed_lockset == NULL)
            timed_lockset = tdata->get_timedlockset();

        WriteData data = {
            timed_lockset, backtrace, slice_size
        };

        (*mem_state)[cl][slice_address] = data;
    }

}

void ProcessRead(uint64_t tid, uint64_t ip, uint64_t address, uint32_t size, const CONTEXT *ctxt) {
    RegisterAccess(tid, ip, address, size, ctxt);
}

void ProcessFence(uint64_t tid, uint64_t ip, bool is_rmw, const CONTEXT *ctxt, uint64_t address = 0, uint32_t size = 0) {
    ThreadData * tdata = get_thread_data(tid);


    if(is_rmw && size != 0) {
        ProcessStore(tid, ip, address, size, ctxt);
    }

    
    auto& mem_state = tdata->flushed_mem_state;
    pTimedLockSet timed_lockset = tdata->get_timedlockset();


    if(mem_state.empty())
        return;

    backtrace_t backtrace = nullptr;

    
    // Iterate cache lines
    for(const auto &cache_line : mem_state) {
        auto &cache_line_state = cache_line.second;

        // Iterate addresses
        for(const auto &write : cache_line_state) {
            const WriteData *write_data = &write.second;
            assert(write_data != NULL);

            uint64_t address = write.first;

            std::bitset<64> mask = IgnoreAccess(tid, address, write_data->size);

            if(mask.none())
                continue;

            if(backtrace == nullptr) {
        COUNT_TIME_GENERIC(backtrace_time, 
            #ifdef NO_BACKTRACE
                backtrace = (void *) ip;
            #else
                backtrace = GetBacktrace(ctxt, backtrace_depth);
            #endif
        );    
            }

            pLockSet common_set;

            common_set = timed_lockset->intersect(write_data->timed_lockset);

            RaceLikelyPoint rlp(
                common_set, 
                tdata->vector_clocks.size()-1,
                write_data->backtrace, 
                backtrace,
                address,
                write_data->size,
                mask
            );

            tdata->race_likely_points.push_back(rlp);
        }
    }

    mem_state.clear();
}

void ProcessLock(uint64_t tid, uint64_t ip, trace::Instruction locktype, uint64_t mutex, bool special = false) {
    ThreadData * tdata = get_thread_data(tid);

    TimedLockSet * timed_lockset = &tdata->timed_lockset;
    tdata->timedlockset_cached = NULL;

    switch(locktype) {
        case trace::Instruction::LOCK:
        case trace::Instruction::WRLOCK:
        case trace::Instruction::RDLOCK:
            if(special)
                timed_lockset->lock_special(mutex, get_next_id(tdata));
            else
                timed_lockset->lock(mutex, get_next_id(tdata));
            break;


        case trace::Instruction::RWUNLOCK: 
        case trace::Instruction::UNLOCK:
            if(special)
                timed_lockset->unlock_special(mutex);
            else
                timed_lockset->unlock(mutex);
            break;


        default:
            break;
    }
}


void ProcessThreadCreate(uint64_t tid) {
    ThreadData * tdata = get_thread_data(tid);

    VectorClock clock = tdata->vector_clocks.back();

    clock.update(tid);

    tdata->vector_clocks.push_back(clock);
    creator_thread_clock = tdata->vector_clocks.back();
}

void ProcessThreadInit(uint64_t tid, int64_t creator_tid) {
    ThreadData * tdata = get_thread_data(tid);
    tdata->tid = tid;

    VectorClock creator_clock;
    if(creator_tid != -1) {
        creator_clock = creator_thread_clock;
    } 

    creator_clock.update(tid);

    tdata->vector_clocks.push_back(creator_clock);
}

void ProcessThreadJoin(uint64_t tid, uint64_t exit_tid) {
    ThreadData * tdata = get_thread_data(tid);

    VectorClock clock = tdata->vector_clocks.back();

    clock.update(tid);

    PIN_MutexLock(&thread_exit_mutex);
    clock.update(exit_threads_clock[exit_tid]);
    PIN_MutexUnlock(&thread_exit_mutex);

    tdata->vector_clocks.push_back(clock);
}

void ProcessThreadExit(uint64_t tid) {
    ThreadData * tdata = get_thread_data(tid);

    VectorClock clock = tdata->vector_clocks.back();

    clock.update(tid);

    tdata->vector_clocks.push_back(clock);

    PIN_MutexLock(&thread_exit_mutex);
    exit_threads_clock[tid] = clock;
    PIN_MutexUnlock(&thread_exit_mutex);
} 

std::set<backtrace_t> CheckPMRacesPerThread(uint64_t tid, const RaceLikelyPoint& rlp) {
    std::set<backtrace_t> races;
    uint64_t start_address = rlp.address;
    uint64_t size = rlp.size;
    std::bitset<64> mask = rlp.mask;
    pLockSet common_set = rlp.common_set;
    VectorClock& clock = get_thread_data(tid)->vector_clocks[rlp.clock_i];


    std::set<const lockset_set_t*> visited;

    for(uint64_t access_tid = 0; access_tid < TLS_MAX_SIZE; access_tid++) {
        ThreadData &thread_data = *get_thread_data(access_tid);

        if(!thread_data.used)
            continue;

        if(access_tid == tid)
            continue;

        auto& access_points = thread_data.access_points_opt;

        for(uint64_t address = start_address; address < start_address + size; address++) {
            if(!mask.test(address - start_address))
                continue;

            if(!access_points.contains(address)) 
                continue;
            
            for(const auto & access_entry : access_points[address]) {
                backtrace_t backtrace;
                uint64_t clock_i;
                std::tie(backtrace, clock_i) = access_entry.first;

                for(const lockset_set_t * locksets : access_entry.second) {
                    if(visited.contains(locksets))
                        continue;

                    visited.insert(locksets);

                    VectorClock & vc = thread_data.vector_clocks[clock_i];

                    for(const pLockSet ls : *locksets) {
                        if(!ls->simple_intersect(common_set)) {
                            if(vc.is_concurrent(clock)) {
                                races.insert(backtrace);
                                goto found_race;
                            }
                        }
                    }
                }

                found_race:
                continue;
            }

        }
    }
    return races;
}

VOID CheckPMRaces(VOID *v) {

    std::cerr << "-----------------------------" << std::endl;
    std::cerr << "Checking for persistent races" << std::endl;
    std::cerr << "-----------------------------" << std::endl;
    
    lockset_analysis_time -= realtime();

    for(int i = 0; i < TLS_MAX_SIZE; i++) {
        ThreadData &thread_data = *get_thread_data(i);

        if(!thread_data.used)
            continue;

        auto & access_points = thread_data.access_points;
        auto & access_points_opt = thread_data.access_points_opt;

        for(const auto & access_iterator : access_points) {
            uint64_t address = access_iterator.first.address;
            backtrace_t trace = access_iterator.first.backtrace;
            uint64_t clock_i = access_iterator.first.clock_i;
            std::bitset<64> mask = access_iterator.first.mask;

            const lockset_set_t * locksets = &access_iterator.second;
            auto key = std::make_pair(trace, clock_i);

            for(int i = 0; i < 64; i++) {
                if(mask.test(i)) {

                    auto & opt_info = access_points_opt[address+i][key];
                    opt_info.push_back(locksets);
                }
            }        
        }
    }

    std::map<std::array<backtrace_t, 2>, std::set<backtrace_t>> races_per_rlp;

    for(int i = 0; i < TLS_MAX_SIZE; i++) {
        ThreadData &thread_data = *get_thread_data(i);

        if(!thread_data.used)
            continue;

        for(auto& vc : thread_data.vector_clocks) {
            vc.update(i);
        }

        auto& race_likely_points = thread_data.race_likely_points;

        for(const auto & rlp : race_likely_points) {
            std::set<backtrace_t> races = CheckPMRacesPerThread(i, rlp);

            if(races.size() == 0)
                continue;
            std::array<backtrace_t, 2> traces = {rlp.write_trace, rlp.fence_trace};

            if(!races_per_rlp.contains(traces)) {
                races_per_rlp[traces] = std::set<backtrace_t>();
            }

            auto & rlp_race = races_per_rlp[traces];

            for(const auto & trace : races) {
                rlp_race.insert(trace);
            }
        } 
    }

    lockset_analysis_time += realtime();

    output_time -= realtime();

    std::ostream* p_trace_out = &std::cout;
    std::ofstream fout;

    std::string out_path = KnobOutPath.Value();
    debug("Output file %s\n", (out_path != "") ? out_path.c_str() : "stdout");
    if (out_path != "") {
        fout.open(out_path);
        p_trace_out = &fout;
    }

    std::ostream & trace_out = *p_trace_out;

    for(const auto &entry : races_per_rlp) {
        auto & traces = entry.first;
        auto & races = entry.second;

        std::string write_symbols = GetBacktraceSymbols(traces[0]);
        std::string fence_symbols = GetBacktraceSymbols(traces[1]);

        trace_out << "PM address written in:" << std::endl;
        trace_out << write_symbols << std::endl;

        trace_out << "flushed in:" << std::endl;
        trace_out << fence_symbols << std::endl;

        trace_out << "can be acessed concurrently in: " << std::endl;
        bool start = true;
        for(const auto &access_trace : races) {
            std::string access_symbols = GetBacktraceSymbols(access_trace);

            if(!start) {
                trace_out << "---" << std::endl;
            }
            start = false;

            trace_out << access_symbols;  

        }
        trace_out << std::endl;
    }   

    trace_out.flush();
    if (out_path != "") {
        fout.close();
    }

    output_time += realtime();
}

/*

    INSTRUMENTATION

*/

void TraceWrite(THREADID tid, const CONTEXT *ctxt, ADDRINT ip, ADDRINT address, uint32_t size, trace::Instruction type) {
COUNT_TIME_GENERIC(instr_time, {
    COUNT_STORE;
    if (IsPMAddress(address, size, tid)) {   
        if(type == trace::Instruction::NON_TEMPORAL_STORE) {
            COUNT_PM_NT_STORE;

        COUNT_FLUSH_TIME({
            ProcessFlush(tid, ip, trace::Instruction::CLFLUSHOPT, address);
        })
            return;
        }

        COUNT_PM_STORE;
        COUNT_STORE_TIME({
            ProcessStore(tid, ip, address, size, ctxt);
        })
    }
})
}

void TraceFlush(THREADID tid, ADDRINT ip, ADDRINT address, trace::Instruction flushtype) {
COUNT_TIME_GENERIC(instr_time, {    
    if (IsPMAddress(address, 0, tid)) {  // only the start matters in this case 
        COUNT_FLUSH;

        COUNT_FLUSH_TIME({
            ProcessFlush(tid, ip, flushtype, address);
        })    
    }
})
}

void TraceFence(THREADID tid, const CONTEXT *ctxt, ADDRINT ip) {  
COUNT_TIME_GENERIC(instr_time, {
    COUNT_FENCE;

    COUNT_FENCE_TIME({
        ProcessFence(tid, ip, false, ctxt);
    })
})
}

void TraceRMW(THREADID tid, const CONTEXT *ctxt, ADDRINT ip, ADDRINT address, uint32_t size) {  
COUNT_TIME_GENERIC(instr_time, {
    //COUNT_FENCE;
    COUNT_RMW;

    if (IsPMAddress(address, size, tid)) {
        //COUNT_STORE;
        COUNT_RMW_TIME({
            ProcessFence(tid, ip, true, ctxt, address, size);
        })
    } else {
        COUNT_RMW_TIME({
            ProcessFence(tid, ip, true, ctxt);
        })
    }
})
}

void TraceRead(THREADID tid, const CONTEXT *ctxt, ADDRINT ip, ADDRINT address, uint32_t size, ADDRINT address2) {
COUNT_TIME_GENERIC(instr_time, {
    if(IsPMAddress(address, size, tid)) { 
        COUNT_PM_LOAD;

        COUNT_LOAD_TIME({
            ProcessRead(tid, ip, address, size, ctxt);
        })
    }
    else if(address2 != 0 && IsPMAddress(address2, size, tid)) {
        COUNT_PM_LOAD;
        COUNT_LOAD_TIME({
            ProcessRead(tid, ip, address2, size, ctxt);
        })
    }
})
}

VOID TraceInstructions(INS ins, VOID *v) {
    [[maybe_unused]] uint64_t tid = 0;
COUNT_TIME_GENERIC(injection_time, {
    int opcode = INS_Opcode(ins);

    if (INS_IsAtomicUpdate(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceRMW,
                       IARG_THREAD_ID, 
                       IARG_CONST_CONTEXT, 
                       IARG_INST_PTR, 
                       IARG_MEMORYWRITE_EA, 
                       IARG_MEMORYWRITE_SIZE, 
                       IARG_END);
    } else if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) {
        trace::Instruction inst = IsMovnt(opcode)
                                      ? trace::Instruction::NON_TEMPORAL_STORE
                                      : trace::Instruction::STORE;
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceWrite,
                       IARG_THREAD_ID, 
                       IARG_CONST_CONTEXT, 
                       IARG_INST_PTR, 
                       IARG_MEMORYWRITE_EA, 
                       IARG_MEMORYWRITE_SIZE, 
                       IARG_UINT64, inst, 
                       IARG_END);
    } else if (INS_IsCacheLineFlush(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceFlush,
                       IARG_THREAD_ID, 
                       IARG_INST_PTR, 
                       IARG_MEMORYOP_EA, 0, 
                       IARG_UINT64, GetFlushType(opcode),
                       IARG_END);
    } else if (IsFence(opcode)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceFence, 
                       IARG_THREAD_ID, 
                       IARG_CONST_CONTEXT, 
                       IARG_INST_PTR, 
                       IARG_END);
    } else if(INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) {
        if(INS_HasMemoryRead2(ins)) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceRead, 
                           IARG_THREAD_ID, 
                           IARG_CONST_CONTEXT, 
                           IARG_INST_PTR,
                           IARG_MEMORYREAD_EA, 
                           IARG_MEMORYREAD_SIZE,
                           IARG_MEMORYREAD2_EA,
                           IARG_END);
        } else {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceRead, 
                           IARG_THREAD_ID, 
                           IARG_CONST_CONTEXT, 
                           IARG_INST_PTR,
                           IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
                           IARG_ADDRINT, 0,
                           IARG_END);
        }
    }
})
}


void LockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    COUNT_LOCK;
    ProcessLock(tid, retAddr, trace::Instruction::LOCK, mutexAddress);
})
}

void WriteLockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    COUNT_WRLOCK;
    ProcessLock(tid, retAddr, trace::Instruction::WRLOCK, mutexAddress);
})
}

void ReadLockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    COUNT_RDLOCK;
    ProcessLock(tid, retAddr, trace::Instruction::RDLOCK, mutexAddress);
})
}

void TryLockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    ThreadData * tdata = get_thread_data(tid);
    tdata->try_lock_mutex = mutexAddress;
    tdata->try_lock_return_address = retAddr;
})
}

void TryLockAfter(THREADID tid, ADDRINT result, UINT64 success, uint64_t negate) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    bool check = (result == success);
    if(negate)
        check = !check;

    if(check) {
        COUNT_LOCK;
        ThreadData * tdata = get_thread_data(tid);
        uint64_t retAddr = tdata->try_lock_return_address;
        uint64_t mutexAddress = tdata->try_lock_mutex;

        ProcessLock(tid, retAddr, trace::Instruction::TRY_LOCK, mutexAddress);
    }
})
}
void TryWriteLockAfter(THREADID tid, ADDRINT result) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    if(result == 0) {
        COUNT_WRLOCK;
        ThreadData * tdata = get_thread_data(tid);
        uint64_t retAddr = tdata->try_lock_return_address;
        uint64_t mutexAddress = tdata->try_lock_mutex;
        ProcessLock(tid, retAddr, trace::Instruction::TRY_WRLOCK, mutexAddress);
    }
})
}
void TryReadLockAfter(THREADID tid, ADDRINT result) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    if(result == 0) {
        COUNT_RDLOCK;
        ThreadData * tdata = get_thread_data(tid);
        uint64_t retAddr = tdata->try_lock_return_address;
        uint64_t mutexAddress = tdata->try_lock_mutex;
        ProcessLock(tid, retAddr, trace::Instruction::TRY_RDLOCK, mutexAddress);
    }
})
}

void UnlockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    COUNT_UNLOCK;
    ProcessLock(tid, retAddr, trace::Instruction::UNLOCK, mutexAddress);
})
}

void RWUnlockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    COUNT_RWUNLOCK;
    ProcessLock(tid, retAddr, trace::Instruction::RWUNLOCK, mutexAddress);
})
}

int PthreadCreateReplacement(THREADID tid, 
                             AFUNPTR original, 
                             const CONTEXT * ctxt,
                             ADDRINT pid,
                             ADDRINT attr,
                             ADDRINT start_routine,
                             ADDRINT arg) {

    PIN_MutexLock(&thread_creation_mutex);

    creator_thread_id = (int64_t) tid;

    ProcessThreadCreate(creator_thread_id);

    created_pthread_id_address = (pthread_t *) pid;
    int result = -1;

    PIN_CallApplicationFunction(ctxt, creator_thread_id, CALLINGSTD_DEFAULT, original, NULL, 
                                    PIN_PARG(int), &result,
                                    PIN_PARG(void *), pid,
                                    PIN_PARG(void *), attr,
                                    PIN_PARG(void *), start_routine,
                                    PIN_PARG(void *), arg,
                                    PIN_PARG_END());
    if(result == 0) {
        PIN_SemaphoreWait(&thread_creation_semaphore);
        PIN_SemaphoreClear(&thread_creation_semaphore);

        pthread_t pthread_id = *created_pthread_id_address;

        pthread_id_to_tid[pthread_id] = created_thread_id;
    }

    PIN_MutexUnlock(&thread_creation_mutex);   

    return result;
}

VOID TraceThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v) {
    created_thread_id = tid;

    if(tid != 0) {        
        PIN_SemaphoreSet(&thread_creation_semaphore);
    } 

    assert(tid < TLS_MAX_SIZE);

    get_thread_data(tid)->used = true;

    ProcessThreadInit(tid, creator_thread_id);
}

VOID TraceAdqRelBefore(THREADID tid, ADDRINT retAddr, UINT64 id) {
    ProcessLock(tid, retAddr, trace::Instruction::LOCK, id, true);
}

VOID TraceAdqRelAfter(THREADID tid, UINT64 id) {
    ProcessLock(tid, 0, trace::Instruction::UNLOCK, id, true);
}


int PthreadJoinReplacement(THREADID tid,
                           AFUNPTR original, 
                           const CONTEXT * ctxt,
                           ADDRINT pid,
                           ADDRINT retval) {


    int result = -1;

    PIN_CallApplicationFunction(ctxt, tid, CALLINGSTD_DEFAULT, original, NULL,
                                      PIN_PARG(int *), &result,
                                      PIN_PARG(void *), pid,
                                      PIN_PARG(void *), retval,
                                      PIN_PARG_END());

    if(result == 0) {
        ProcessThreadJoin(tid, (uint64_t) pthread_id_to_tid[pid]);
    }

    return result;
}

VOID TraceThreadExit(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v) {
    ProcessThreadExit(tid);

    for(const auto ele : locksets_cache) {
        assert((void*)ele != (void*)get_thread_data(tid)->timed_lockset.lockset);
    }

    SUM_GLOBAL_INSTRUCTION_COUNT
    SUM_GLOBAL_TIME_COUNT
}



VOID ImageLoad(IMG img, VOID *v) {
    [[maybe_unused]] uint64_t tid = 0;
COUNT_TIME_GENERIC(image_time, {
    std::vector<MutexConfig> &configs = *((std::vector<MutexConfig>*) v);

    debug("Image: %s\n", IMG_Name(img).c_str());

    for(const auto & config : configs) {
        for(const auto & adqConfig : config.adquire) {
            if(adqConfig.type == MUTEX)
                InstrumentLockRoutine(img, adqConfig.name.c_str(), 
                    (AFUNPTR) LockBefore, nullptr, adqConfig.mutex_id_arg);
            else if(adqConfig.type == WRITE)
                InstrumentLockRoutine(img, adqConfig.name.c_str(), 
                    (AFUNPTR) WriteLockBefore, nullptr, adqConfig.mutex_id_arg);
            else if(adqConfig.type == READ)
                InstrumentLockRoutine(img, adqConfig.name.c_str(), 
                    (AFUNPTR) ReadLockBefore, nullptr, adqConfig.mutex_id_arg);
        }
        for(const auto & relConfig : config.release) {
            if(relConfig.type == MUTEX)
                InstrumentLockRoutine(img, relConfig.name.c_str(), 
                    (AFUNPTR) UnlockBefore, nullptr, relConfig.mutex_id_arg);
            else
                InstrumentLockRoutine(img, relConfig.name.c_str(), 
                    (AFUNPTR) RWUnlockBefore, nullptr, relConfig.mutex_id_arg);
        }
        for(const auto & try_adqConfig : config.try_adquire) {
            if(try_adqConfig.type == MUTEX)
                InstrumentLockRoutine(img, try_adqConfig.name.c_str(), 
                    (AFUNPTR) TryLockBefore, (AFUNPTR) TryLockAfter, 
                    try_adqConfig.mutex_id_arg, try_adqConfig.success_value, try_adqConfig.success_is_failure);

            else if(try_adqConfig.type == WRITE) 
                InstrumentLockRoutine(img, try_adqConfig.name.c_str(), 
                    (AFUNPTR) TryLockBefore, (AFUNPTR) TryWriteLockAfter, 
                    try_adqConfig.mutex_id_arg, try_adqConfig.success_value, try_adqConfig.success_is_failure);
                
            else if(try_adqConfig.type == READ)
                InstrumentLockRoutine(img, try_adqConfig.name.c_str(), 
                    (AFUNPTR) TryLockBefore, (AFUNPTR) TryReadLockAfter, 
                    try_adqConfig.mutex_id_arg, try_adqConfig.success_value, try_adqConfig.success_is_failure);

        }

        uint64_t special_mutex = LockSet::register_special_mutex();
        for(const std::string & adq_relFunc : config.adq_rel) {
            InstrumentAdqRelRoutine(img, adq_relFunc.c_str(), 
                    (AFUNPTR)TraceAdqRelBefore, 
                    (AFUNPTR)TraceAdqRelAfter, special_mutex);
        }
    }

    RTN routine = RTN_FindByName(img, "pthread_create");
    if(RTN_Valid(routine)) {
        debug("Instrument pthread_create\n"); 
        RTN_ReplaceSignature(routine, (AFUNPTR) PthreadCreateReplacement, 
            IARG_THREAD_ID,
            IARG_ORIG_FUNCPTR, 
            IARG_CONST_CONTEXT, 
            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
            IARG_END);
    }

    routine = RTN_FindByName(img, "pthread_join");
    if(RTN_Valid(routine)) {
        debug("Instrument pthread_join\n");
        RTN_ReplaceSignature(routine, (AFUNPTR) PthreadJoinReplacement, 
            IARG_THREAD_ID,
            IARG_ORIG_FUNCPTR, 
            IARG_CONST_CONTEXT, 
            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
            IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
            IARG_END);
    }   
}) 
}

template<typename Key, typename T>
size_t get_map_size(const std::unordered_map<Key, T> &m) {
    return m.size() * (sizeof(Key) + sizeof(T));
}

VOID Fini(INT32 code, VOID *v) {
    PIN_MutexFini(&variable_accessed_mutex);
    PIN_MutexFini(&thread_creation_mutex);
    PIN_MutexFini(&thread_exit_mutex);
    PIN_MutexFini(&lock_register_mutex);
    PIN_MutexFini(&lockset_cache_mutex);
    PIN_MutexFini(&timedlockset_cache_mutex);
    PIN_MutexFini(&backtrace_mutex);

    PIN_SemaphoreFini(&thread_creation_semaphore);  

    tool_execution_time += realtime();
    FINISH_TIME_COUNT

    size_t n_rlps = 0;
    size_t vector_cap = 0;
    size_t mem_state_size = 0;
    size_t access_point_size = 0;
    size_t vcs_n = 0;
    
    for(int i = 0; i < TLS_MAX_SIZE; i++) {
        ThreadData & thread_data = *get_thread_data(i);

        if(!thread_data.used)
            continue;

        n_rlps += thread_data.race_likely_points.size();
        vcs_n += thread_data.vector_clocks.size();
        vector_cap += thread_data.race_likely_points.capacity() * sizeof(RaceLikelyPoint);
        access_point_size += get_map_size(thread_data.access_points);
        mem_state_size += get_map_size(thread_data.mem_state);
        mem_state_size += get_map_size(thread_data.flushed_mem_state);

        for(const auto & it : thread_data.mem_state) {
            mem_state_size += get_map_size(it.second);
        }
        for(const auto & it : thread_data.flushed_mem_state) {
            mem_state_size += get_map_size(it.second);
        }
    }


    OUTPUT_INSTRUCTION_COUNT

    OUTPUT_TIME_COUNT

    std::cerr << "-- General Data Structures Usage (Approximation) --" << std::endl;
    std::cerr << "    WriteData (KB):      " << mem_state_size / 1000 << std::endl;
    std::cerr << "    RaceLikelyPoint(KB): " << vector_cap / 1000 << std::endl;
    std::cerr << "    Access points(KB):   " << access_point_size / 1000 << std::endl;
    std::cerr << "    Trace(#):            " << get_traces_size() << std::endl;
    std::cerr << "    Vector Clocks(#):    " << vcs_n << std::endl;
    std::cerr << "    Locksets(#):         " << locksets_cache.size() << std::endl;
    std::cerr << "    Timed Locksets(#):   " << timedlocksets_cache.size() << std::endl;
    std::cerr << "    N allocs(#):         " << allocs.capacity() << std::endl;
    std::cerr << "    IRH check(KB):       " << variable_accessed.size() * 16 / 1000 << std::endl;

    std::cerr << std::endl;

    std::cerr << "-- LockSet Analysis Report --" << std::endl;
    std::cerr << "    Race Likely Points Compared (#): " << n_rlps << std::endl; 
    std::cerr << "    Analysis Time (s): " << (double) lockset_analysis_time / 1000000000 << std::endl;
    std::cerr << "    Output Time (s): " << (double) output_time / 1000000000 << std::endl;
    std::cerr << "    Tool Execution Time (s): " << (double) tool_execution_time / 1000000000 << std::endl;
    std::cerr << std::endl;
}

void HandleKnobs() {
    // Parse inputs
    pm_mount = KnobPMMount.Value().c_str();
    use_init_removal_heuristic_n = (uint64_t) KnobInitRemoval.Value();
    backtrace_depth = (uint64_t) KnobBacktraceDepth.Value();

    debug("Backtrace depth - %ld\n", backtrace_depth);
    debug("Using Initialization Removal Heuristic for %ld threads\n", use_init_removal_heuristic_n);

    std::vector<MutexConfig> *configs = new std::vector<MutexConfig>();

    for(uint32_t i = 0; i < KnobConfigFiles.NumberOfValues(); i++) {
        debug("Importing config file %s\n", KnobConfigFiles.Value(i).c_str());
        configs->emplace_back(KnobConfigFiles.Value(i));
    }

    IMG_AddInstrumentFunction(ImageLoad, (void *) configs);
}

int main(int argc, char *argv[]) {
    tool_execution_time -= realtime();
    INIT_TIME_COUNT

    PIN_InitSymbols();
    PIN_Init(argc, argv);

    HandleKnobs();

    PIN_MutexInit(&variable_accessed_mutex);
    PIN_MutexInit(&thread_creation_mutex);
    PIN_MutexInit(&thread_exit_mutex);
    PIN_MutexInit(&lock_register_mutex);
    PIN_MutexInit(&lockset_cache_mutex);
    PIN_MutexInit(&timedlockset_cache_mutex);
    PIN_MutexInit(&backtrace_mutex);

    PIN_SemaphoreInit(&thread_creation_semaphore);

    INS_AddInstrumentFunction(TraceInstructions, 0);
    PIN_AddPrepareForFiniFunction(CheckPMRaces, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_AddThreadStartFunction(TraceThreadStart, 0);
    PIN_AddThreadFiniFunction(TraceThreadExit, 0);
        
    PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    PIN_AddSyscallExitFunction(SyscallExit, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
