/*
 *  HawkSet - An automatic, agnostic, and efficient concurrent PM bug detector
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
#include <utility>

#include "pin.H"
#include "trace.hpp"
#include "utils.hpp"

#include "lockset.hpp"
#include "vector_clock.hpp"
#include "logger.hpp"
#include "cache.hpp"


KNOB<std::string> KnobOutPath(KNOB_MODE_WRITEONCE, "pintool", "out",
                             "", "Bug reports output");

KNOB<std::string> KnobPMMount(KNOB_MODE_OVERWRITE, "pintool", "pm-mount",
                              "/mnt/pmem0/", "PM mount in filesystem");

KNOB<int> KnobInitRemoval(KNOB_MODE_WRITEONCE, "pintool", "irh",
                              "1", "Initialization removal heuristic (0 or 1)");

KNOB<int> KnobBacktraceDepth(KNOB_MODE_WRITEONCE, "pintool", "bt-depth",
                              "50", "Depth of backtraces reported");

KNOB<std::string> KnobConfigFiles(KNOB_MODE_APPEND, "pintool", "cfg",
                              "", "Configuration file (repeatable)");

KNOB<bool> KnobCheckUnpersistedWrites(KNOB_MODE_WRITEONCE, "pintool", "unpersisted",
                                        "0", "Use unpersisted writes in the analysis");

void PrintUsage()
{
    PIN_ERROR("HawkSet: Automatic, Application-Agnostic, and Efficient Concurrent PM Bug Detection\n" + KNOB_BASE::StringKnobSummary() + "\n");
}

bool check_unpersisted_stores;

// StoreData from the algorithm described in atc's paper
struct StoreFenceData {
    pLockset common_set;
    uint16_t clock_i;
    backtrace_t write_trace;
    backtrace_t fence_trace;
    uint64_t address;
    bool persisted;
    bool was_flushed;

    StoreFenceData(pLockset l, uint16_t t, backtrace_t wt, backtrace_t ft, uint64_t a, bool p, bool f)
        : common_set(l), clock_i(t), write_trace(wt), fence_trace(ft), address(a), persisted(p), was_flushed(f) {}
};

struct StoreData {
    pTimedLockset timed_lockset;
    backtrace_t backtrace;
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

typedef std::set<pLockset> lockset_set_t;

/* cache_line -> address -> state */
typedef std::unordered_map<uint64_t, std::unordered_map<uint64_t, StoreData>> mem_state_t;
typedef std::map<std::tuple<backtrace_t, backtrace_t, bool>, std::set<backtrace_t>>  reports_t;

struct alignas(64) ThreadData {
    TimedLockset current_timedlockset;
    pTimedLockset cached_timedlockset = NULL;

    inline pTimedLockset get_timedlockset() {
        if(cached_timedlockset == NULL)
            cached_timedlockset = timedlockset_cache_get(&current_timedlockset);

        return cached_timedlockset;
    }

    std::vector<VectorClock> vector_clocks;

    std::vector<StoreFenceData> race_likely_stores;
    
    /*
    Access points:
        (address, backtrace, clock) -> locksets
    */
    std::unordered_map<access_key_t,
        lockset_set_t> race_likely_loads;


    /*
    Access points optimized for analysis:
        (address) ->
            (backtrace_t, clock_i) -> locksets
    */
    std::unordered_map<uint64_t,
        std::unordered_map<std::pair<backtrace_t, uint64_t>,
            lockset_set_t>> race_likely_loads_opt;

    /*
    Cache
        cache_line -> address -> state
    */
    mem_state_t mem_state;
    mem_state_t flushed_mem_state;


    //uint64_t pthread_id;
    uint64_t tid;

    uint32_t lock_clock = 0;
    uint64_t try_lock_mutex, try_lock_return_address, result_address;

    bool used = false;

    std::vector<void*> stack;
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

bool IgnoreAccess(uint64_t tid, uint64_t address) {
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

inline bool HandleAccessByte(uint64_t tid, uint64_t address) {
    if(use_init_removal_heuristic_n == 0) {
        return true;
    }

    bool ret;

    PIN_MutexLock(&variable_accessed_mutex);

    ret = !IgnoreAccess(tid, address);

    PIN_MutexUnlock(&variable_accessed_mutex);

    return ret;
}

inline std::bitset<64> HandleAccess(uint64_t tid, uint64_t address, uint32_t size) {
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
        mask.set(i, !IgnoreAccess(tid, address));
    }

    PIN_MutexUnlock(&variable_accessed_mutex);
    return mask;
}

void RegisterAccess(uint64_t tid, uint64_t ip, uint64_t address, uint32_t size, const CONTEXT *ctxt) {
    std::bitset<64> mask;

    mask = HandleAccess(tid, address, size);

    if(mask.none())
        return; 

    ThreadData * tdata = get_thread_data(tid);

COUNT_TIME_GENERIC(backtrace_time, 
#ifdef NO_BACKTRACE
    backtrace_t backtrace = (void *) ip;
#else
    backtrace_t backtrace = GetBacktrace(ctxt, backtrace_depth, tdata->stack);
#endif
);
    
    uint16_t clock_i = tdata->vector_clocks.size()-1;

    access_key_t key = {address, backtrace, clock_i, mask};
    auto & race_likely_loads = tdata->race_likely_loads[key];

    Lockset access = std::move(tdata->get_timedlockset()->to_lockset());

    race_likely_loads.insert(lockset_cache_get(&access));
}

void RegisterUnpersistedStore(uint64_t tid, uint64_t address, StoreData &data, pTimedLockset current_timedlockset, backtrace_t trace, bool was_flushed) {
    if(!HandleAccessByte(tid, address))
        return;

    ThreadData * tdata = get_thread_data(tid);

    pLockset common_set = intersect_timedlockset(current_timedlockset, data.timed_lockset);

    StoreFenceData rlp(
        common_set, 
        tdata->vector_clocks.size()-1,
        data.backtrace, 
        trace,
        address,
        false,
        was_flushed
    );

    tdata->race_likely_stores.push_back(rlp);
}

void ProcessFlush(uint64_t tid, uint64_t ip, trace::Instruction flushtype, uint64_t address) {
    ThreadData * tdata = get_thread_data(tid);

    auto& mem_state = tdata->mem_state;
    auto& flushed_mem_state = tdata->flushed_mem_state;

    uint64_t cache_line = CACHE_LINE(address);

    if(!mem_state.contains(cache_line))
        return;    

    for(const auto & entry : mem_state[cache_line]) {
        flushed_mem_state[cache_line][entry.first] = entry.second;
    }

    mem_state.erase(cache_line);
}

void CheckOverwrite(uint64_t tid, uint64_t address, mem_state_t & mem_state, pTimedLockset ls, backtrace_t trace, bool was_flushed) {

    uint64_t cl = CACHE_LINE(address);

    auto & cache_line_state = mem_state[cl];

    if(cache_line_state.contains(address)) {
        RegisterUnpersistedStore(tid, address, cache_line_state[address], ls, trace, was_flushed);
        cache_line_state.erase(address);
    }
}

void ProcessStore(uint64_t tid, uint64_t ip, uint64_t address, uint32_t size, const CONTEXT *ctxt) {
    ThreadData * tdata = get_thread_data(tid);

    HandleAccess(tid, address, size);


COUNT_TIME_GENERIC(backtrace_time, 
#ifdef NO_BACKTRACE
    backtrace_t backtrace = (void *) ip;
#else
    backtrace_t backtrace = GetBacktrace(ctxt, backtrace_depth, tdata->stack);
#endif
);

    pTimedLockset store_timedlockset = tdata->get_timedlockset();
    StoreData data = {store_timedlockset, backtrace};

    auto & mem_state = tdata->mem_state;
    auto & flushed_mem_state = tdata->flushed_mem_state;

    for(uint64_t addr = address; addr < address + size; addr++) {
        if(check_unpersisted_stores) {
            CheckOverwrite(tid, addr, mem_state, store_timedlockset, backtrace, false);
            CheckOverwrite(tid, addr, flushed_mem_state, store_timedlockset, backtrace, true);
        }

        uint64_t cl = CACHE_LINE(addr);
        mem_state[cl][addr] = data;
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
    pTimedLockset fence_timedlockset = tdata->get_timedlockset();

    if(mem_state.empty())
        return;

    backtrace_t backtrace = nullptr;

    
    // Iterate cache lines
    for(const auto &cache_line : mem_state) {
        auto &cache_line_state = cache_line.second;

        // Iterate addresses
        for(const auto &write : cache_line_state) {
            const StoreData *write_data = &write.second;
            assert(write_data != NULL);

            uint64_t address = write.first;

            if(!HandleAccessByte(tid, address))
                continue;

            if(backtrace == nullptr) {
        COUNT_TIME_GENERIC(backtrace_time, 
            #ifdef NO_BACKTRACE
                backtrace = (void *) ip;
            #else
                backtrace = GetBacktrace(ctxt, backtrace_depth, tdata->stack);
            #endif
        );    
            }

            pLockset common_set = intersect_timedlockset(fence_timedlockset, write_data->timed_lockset);

            StoreFenceData rlp(
                common_set, 
                tdata->vector_clocks.size()-1,
                write_data->backtrace, 
                backtrace,
                address,
                true,
                true
            );

            tdata->race_likely_stores.push_back(rlp);
        }
    }

    mem_state.clear();
}

void ProcessLock(uint64_t tid, uint64_t ip, trace::Instruction locktype, uint64_t mutex, bool special = false) {
    ThreadData * tdata = get_thread_data(tid);

    tdata->cached_timedlockset = NULL;

    switch(locktype) {
        case trace::Instruction::TRY_LOCK:
        case trace::Instruction::TRY_WRLOCK:
        case trace::Instruction::TRY_RDLOCK:
        case trace::Instruction::LOCK:
        case trace::Instruction::WRLOCK:
        case trace::Instruction::RDLOCK:
            if(special)
                tdata->current_timedlockset.lock_special(mutex, get_next_id(tdata));
            else
                tdata->current_timedlockset.lock(mutex, get_next_id(tdata));
            break;


        case trace::Instruction::RWUNLOCK: 
        case trace::Instruction::UNLOCK:
            if(special)
                tdata->current_timedlockset.unlock_special(mutex);
            else
                tdata->current_timedlockset.unlock(mutex);
            break;


        default:
            break;
    }
}


void ProcessThreadCreate(uint64_t tid) {
    ThreadData * tdata = get_thread_data(tid);

    VectorClock clock = tdata->vector_clocks.back();

    clock.update(tid);
    
    creator_thread_clock = clock;

    clock.update(tid);

    tdata->vector_clocks.push_back(clock);
}

void ProcessThreadInit(uint64_t tid, int64_t creator_tid) {
    ThreadData * tdata = get_thread_data(tid);
    tdata->tid = tid;

    VectorClock creator_clock;
    if(creator_tid != -1) {
        creator_clock = creator_thread_clock;
    } 

    creator_clock.update(tid);
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

    if(check_unpersisted_stores) {
        auto& mem_state = tdata->mem_state;
        auto& flushed_mem_state = tdata->flushed_mem_state;

        for(auto & entry_cl : mem_state) {
            for(auto & entry_data : entry_cl.second) {
                RegisterUnpersistedStore(tid, entry_data.first, entry_data.second, tdata->get_timedlockset(), NULL, false);
            }    
        }

        for(auto & entry_cl : flushed_mem_state) {
            for(auto & entry_data : entry_cl.second) {
                RegisterUnpersistedStore(tid, entry_data.first, entry_data.second, tdata->get_timedlockset(), NULL, true);
            }    
        }
    }

    VectorClock clock = tdata->vector_clocks.back();

    clock.update(tid);

    tdata->vector_clocks.push_back(clock);

    PIN_MutexLock(&thread_exit_mutex);
    exit_threads_clock[tid] = clock;
    PIN_MutexUnlock(&thread_exit_mutex);
} 



void OutputRaces(reports_t &races_per_rlp, reports_t &unpersisted_races_per_rlp) {
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

        std::string write_symbols = GetBacktraceSymbols(std::get<0>(traces));
        std::string fence_symbols  = GetBacktraceSymbols(std::get<1>(traces));

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

    for(const auto &entry : unpersisted_races_per_rlp) {
        auto & traces = entry.first;
        auto & races = entry.second;

        std::string write_symbols = GetBacktraceSymbols(std::get<0>(traces));

        std::string ignore_symbols;
        
        if(std::get<1>(traces) == nullptr)
            ignore_symbols = "<THREAD_EXIT>\n";
        else    
            ignore_symbols = GetBacktraceSymbols(std::get<1>(traces));

        trace_out << "PM address written in:" << std::endl;
        trace_out << write_symbols << std::endl;

        if(std::get<2>(traces))
            trace_out << "ignored by (marked for flush):" << std::endl;
        else
            trace_out << "ignored by:" << std::endl;

        trace_out << ignore_symbols << std::endl;

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

uint64_t is_concurrent_exe = 0;
uint64_t intersect_exe = 0;
std::set<backtrace_t> CheckPMRacesPerThread(uint64_t tid, uint64_t write_address, pLockset write_set, VectorClock& write_clock) {
    std::set<backtrace_t> racy_loads;

    for(uint64_t load_tid = 0; load_tid < TLS_MAX_SIZE; load_tid++) {
        ThreadData &thread_data = *get_thread_data(load_tid);

        if(!thread_data.used)
            continue;

        if(load_tid == tid)
            continue;

        auto& race_likely_loads = thread_data.race_likely_loads_opt;

        if(!race_likely_loads.contains(write_address)) 
            continue;
        
        for(const auto & load_entry : race_likely_loads.at(write_address)) {
            backtrace_t backtrace;
            uint64_t clock_i;
            std::tie(backtrace, clock_i) = load_entry.first;

            if(racy_loads.contains(backtrace))
                continue;

            VectorClock & vc = thread_data.vector_clocks[clock_i];
            is_concurrent_exe++;
            if(!vc.is_concurrent(write_clock)) 
                continue;
            
            for(const pLockset ls : load_entry.second) {
                intersect_exe++;
                if(!ls->short_intersect(write_set)) {
                    racy_loads.insert(backtrace);
                    break;
                }
            }
        }
    }

    return racy_loads;
}


VOID CheckPMRaces(VOID *v) {
    std::cerr << "------------------------------" << std::endl;
    std::cerr << "Checking for persistency races" << std::endl;
    std::cerr << "------------------------------" << std::endl;
    
    lockset_analysis_time -= realtime();

    for(int tid = 0; tid < TLS_MAX_SIZE; tid++) {
        ThreadData &thread_data = *get_thread_data(tid);

        if(!thread_data.used)
            continue;

        auto & race_likely_loads = thread_data.race_likely_loads;
        auto & race_likely_loads_opt = thread_data.race_likely_loads_opt;

        for(const auto & access_iterator : race_likely_loads) {
            uint64_t address = access_iterator.first.address;
            backtrace_t trace = access_iterator.first.backtrace;
            uint64_t clock_i = access_iterator.first.clock_i;
            std::bitset<64> mask = access_iterator.first.mask;

            const lockset_set_t & locksets = access_iterator.second;
            auto key = std::make_pair(trace, clock_i);

            for(int i = 0; i < 64; i++) {
                if(mask.test(i)) {
                    auto & opt_info = race_likely_loads_opt[address+i][key];
                    opt_info.insert(locksets.cbegin(), locksets.cend());
                }
            }
        }
    }

    std::cerr << lockset_analysis_time + realtime() << std::endl;

    reports_t races_per_rlp;
    reports_t unpersisted_races_per_rlp;

    for(int tid = 0; tid < TLS_MAX_SIZE; tid++) {
        ThreadData &thread_data = *get_thread_data(tid);

        if(!thread_data.used)
            continue;

        auto& race_likely_stores = thread_data.race_likely_stores;

        /*
            Race likely stores optimized for analysis

            (address, persisted) ->
                (lockset) -> 
                    (clock_i) ->
                        (backtraces)[]
        */

        std::unordered_map<std::tuple<uint64_t, bool, bool>,
            std::unordered_map<pLockset,
                std::unordered_map<uint16_t,
                    std::unordered_set<std::pair<backtrace_t, backtrace_t>>>>> race_likely_stores_opt;

        for(const auto & rls : race_likely_stores) {
            uint64_t write_address = rls.address;

            race_likely_stores_opt[std::make_tuple(write_address, rls.persisted, rls.was_flushed)]
                                  [rls.common_set]
                                  [rls.clock_i].insert(std::make_pair(rls.write_trace, rls.fence_trace));
        }

        for(const auto & entry_adr : race_likely_stores_opt) {
            for(const auto & entry_ls : entry_adr.second) {
                for(const auto & entry_vc : entry_ls.second) {
                    std::set<backtrace_t> racy_loads = CheckPMRacesPerThread(tid, 
                        std::get<0>(entry_adr.first), // address
                        entry_ls.first, // lockset
                        thread_data.vector_clocks[entry_vc.first] // clock
                    );

                    if(racy_loads.size() == 0)
                        continue; 
 
                    for(const auto & trace : entry_vc.second) {
                        std::tuple<backtrace_t, backtrace_t, bool> key = std::make_tuple(trace.first, trace.second, std::get<2>(entry_adr.first));

                        if(std::get<1>(entry_adr.first))
                            races_per_rlp[key].insert(racy_loads.cbegin(), racy_loads.cend());
                        else
                            unpersisted_races_per_rlp[key].insert(racy_loads.cbegin(), racy_loads.cend());
                    }
                }
            }
        }
    }

    lockset_analysis_time += realtime();

    OutputRaces(races_per_rlp, unpersisted_races_per_rlp);
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
    COUNT_LOAD;
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

void TraceCall(uint64_t tid, const CONTEXT *ctxt, ADDRINT address) {
    get_thread_data(tid)->stack.push_back((void*) address);
}

void TraceRet(uint64_t tid, const CONTEXT *ctxt) {
    get_thread_data(tid)->stack.pop_back();
}

VOID TraceInstructions(INS ins, VOID *v) {
    [[maybe_unused]] uint64_t tid = 0;
COUNT_TIME_GENERIC(injection_time, {
    int opcode = INS_Opcode(ins);

    if(INS_IsCall(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceCall, 
                            IARG_THREAD_ID, 
                            IARG_CONST_CONTEXT, 
                            IARG_ADDRINT, INS_Address(ins),
                            IARG_END);
    }
    else if(INS_IsRet(ins)) {
        INS_InsertCall(ins, (INS_IsValidForIpointTakenBranch(ins) ? IPOINT_TAKEN_BRANCH : IPOINT_AFTER), 
            (AFUNPTR)TraceRet, 
            IARG_THREAD_ID, 
            IARG_CONST_CONTEXT, 
            IARG_END);
    }
    else if (INS_IsAtomicUpdate(ins)) {
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

void TryLockBefore(THREADID tid, ADDRINT retAddr, ADDRINT mutexAddress, ADDRINT resultAddress) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    ThreadData * tdata = get_thread_data(tid);
    tdata->try_lock_mutex = mutexAddress;
    tdata->result_address = resultAddress;
    tdata->try_lock_return_address = retAddr;
})
}
std::set<uint64_t> test_;

void TryLockAfter(THREADID tid, ADDRINT result, UINT64 success, uint64_t negate, uint64_t use_arg, LockType locktype) {
COUNT_TIME_GENERIC2(instr_time, lock_time, {
    if(use_arg) {
        ThreadData * tdata = get_thread_data(tid);
        result = *((bool*)tdata->result_address);
    }

    bool check = (result == success);
    if(negate)
        check = !check;

    if(check) {
        COUNT_LOCK;
        ThreadData * tdata = get_thread_data(tid);
        uint64_t retAddr = tdata->try_lock_return_address;
        uint64_t mutexAddress = tdata->try_lock_mutex;
        switch(locktype) {
        case LockType::MUTEX:
            ProcessLock(tid, retAddr, trace::Instruction::TRY_LOCK, mutexAddress);
            break;
        case LockType::WRITE:
            ProcessLock(tid, retAddr, trace::Instruction::TRY_WRLOCK, mutexAddress);
            break;
        case LockType::READ:
            ProcessLock(tid, retAddr, trace::Instruction::TRY_RDLOCK, mutexAddress);
            break;
        }
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
                InstrumentLockRoutine(img, (AFUNPTR) LockBefore, adqConfig);
            else if(adqConfig.type == WRITE)
                InstrumentLockRoutine(img, (AFUNPTR) WriteLockBefore, adqConfig);
            else if(adqConfig.type == READ)
                InstrumentLockRoutine(img, (AFUNPTR) ReadLockBefore, adqConfig);
        }
        for(const auto & relConfig : config.release) {
            if(relConfig.type == MUTEX)
                InstrumentLockRoutine(img, (AFUNPTR) UnlockBefore, relConfig);
            else
                InstrumentLockRoutine(img, (AFUNPTR) RWUnlockBefore, relConfig);
        }
        for(const auto & try_adqConfig : config.try_adquire) {
            InstrumentLockRoutine(img, (AFUNPTR) TryLockBefore, try_adqConfig, (AFUNPTR) TryLockAfter);
        }

        uint64_t special_mutex = Lockset::register_special_mutex();
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

        n_rlps += thread_data.race_likely_stores.size();
        vcs_n += thread_data.vector_clocks.size();
        vector_cap += thread_data.race_likely_stores.capacity() * sizeof(StoreFenceData);
        access_point_size += get_map_size(thread_data.race_likely_loads);
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
    std::cerr << "    StoreData (KB):      " << mem_state_size / 1000 << std::endl;
    std::cerr << "    StoreFenceData(KB): " << vector_cap / 1000 << std::endl;
    std::cerr << "    Access points(KB):   " << access_point_size / 1000 << std::endl;
    std::cerr << "    Trace(#):            " << get_traces_size() << std::endl;
    std::cerr << "    Vector Clocks(#):    " << vcs_n << std::endl;
    std::cerr << "    Locksets(#):         " << locksets_cache.size() << std::endl;
    std::cerr << "    Timed Locksets(#):   " << timedlocksets_cache.size() << std::endl;
    std::cerr << "    N allocs(#):         " << allocs.capacity() << std::endl;
    std::cerr << "    IRH check(KB):       " << variable_accessed.size() * 16 / 1000 << std::endl;

    std::cerr << std::endl;

    std::cerr << "-- Lockset Analysis Report --" << std::endl;
    std::cerr << "    Race Likely Points Compared (#): " << n_rlps << std::endl; 
    std::cerr << "    Analysis Time (s): " << (double) lockset_analysis_time / 1000000000 << std::endl;
    std::cerr << "    Output Time (s): " << (double) output_time / 1000000000 << std::endl;
    std::cerr << "    Tool Execution Time (s): " << (double) tool_execution_time / 1000000000 << std::endl;
    std::cerr << "    Is Concurrent (#): " << is_concurrent_exe << std::endl;
    std::cerr << "    Intersect (#): " << intersect_exe << std::endl;
    std::cerr << std::endl;
}

void HandleKnobs() {
    // Parse inputs
    pm_mount = KnobPMMount.Value().c_str();
    use_init_removal_heuristic_n = (uint64_t) KnobInitRemoval.Value();
    backtrace_depth = (uint64_t) KnobBacktraceDepth.Value();
    check_unpersisted_stores = KnobCheckUnpersistedWrites.Value();

    debug("Backtrace depth - %ld\n", backtrace_depth);
    debug("Using Initialization Removal Heuristic for %ld threads\n", use_init_removal_heuristic_n);
    if(check_unpersisted_stores)
        debug("Checking unpersisted writes in analysis");

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
    if(PIN_Init(argc, argv)) {
        PrintUsage();
        return -1;
    }
 

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
