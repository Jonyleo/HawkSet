#ifndef __HAWKSET_UTILS_HPP__
#define __HAWKSET_UTILS_HPP__

#if defined(TARGET_MAC)
#include <sys/syscall.h>
#else
#include <syscall.h>
#endif

#include <libunwind.h>
#include <execinfo.h>
#include <unistd.h>

#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <string_view>

#include <yaml.h>

#include "pin.H"
#include "trace.hpp"
#include "logger.hpp"


#define RECORD_OPERATIONS_LIMIT 10000000

#define CACHE_LINE(A) (((A) / 64) * 64)


static bool found_alloc = false;
static const char *pm_mount;

// Structure used when instrumenting mmap allocations
struct PMAllocation {
    PMAllocation(uint64_t size) : size(size), start(0), end(size) {}
    PMAllocation() : PMAllocation(0) {}

    inline uint64_t get_size() const { return size; }

    inline uint64_t get_start() const { return start; }

    inline uint64_t get_end() const { return end; }

    void set_start(uint64_t new_start) { start = new_start; end = start+size; }

    uint64_t size;
    uint64_t start;
    uint64_t end;
};

std::vector<PMAllocation> allocs;
int n_allocs = 0;

bool FDPointsToPM(int fd) {
    char fd_path[32];
    sprintf(fd_path, "/proc/self/fd/%d", fd);
    char file_path[100];
    int size = readlink(fd_path, file_path, 100);
    file_path[size] = '\0';
    LOG("PM allocation (" + std::to_string(fd) + ")" + "- " + std::string(file_path) + " - ");

    if (size != -1) {
        int i = 0;
        while (pm_mount[i] != '\0' && i <= size) {
            if (pm_mount[i] != file_path[i]) return false;
            i++;
        }
        return true;
    }
    return false;
}

// Save the size for mmap allocation
VOID SysBefore(ADDRINT ip, ADDRINT num, ADDRINT size, ADDRINT flags,
               ADDRINT fd) {
    // If the mmap call is not creating a private mapping, that is, they use the
    // MAP_SHARED or MAP_SHARED_VALIDATE flags.
    if (num == SYS_mmap &&
        (((flags & 0x01) == 0x01) || ((flags & 0x03) == 0x03)) &&
        FDPointsToPM(fd)) {
        LOG("size: " + std::to_string((uint64_t) size));
        allocs.emplace_back(size);
        found_alloc = true;
    }
}

// Save the returned memory address for each allocation
VOID SysAfter(ADDRINT return_value) {
    if (found_alloc == true) {
        allocs.back().set_start(return_value);
        found_alloc = false;
        LOG("start: " + std::to_string((uint64_t) return_value) + "\n");
    }
}

VOID SyscallEntry(THREADID thread, CONTEXT *ctxt, SYSCALL_STANDARD std,
                  VOID *v) {
    SysBefore(PIN_GetContextReg(ctxt, REG_INST_PTR),
              PIN_GetSyscallNumber(ctxt, std),
              PIN_GetSyscallArgument(ctxt, std, 1),
              PIN_GetSyscallArgument(ctxt, std, 3),
              PIN_GetSyscallArgument(ctxt, std, 4));
}

VOID SyscallExit(THREADID thread, CONTEXT *ctxt, SYSCALL_STANDARD std,
                 VOID *v) {
    SysAfter(PIN_GetSyscallReturn(ctxt, std));
}

// Function will return true if the write is operating on Pmem
inline bool IsPMAddress(ADDRINT address, uint32_t size, uint64_t tid) {
    bool res;
COUNT_TIME_GENERIC(ispm_time, {
    uint64_t range_end = address + size;
    res = false;
    for(const auto& allocation : allocs) {
        if ((address >= allocation.get_start()) &&
            ((range_end) <= (allocation.get_end()))) {
            res = true;
            break;
        }
    }
})
    return res;
}

// Function to check if instruction is a non-temporal store
bool IsMovnt(int opcode) {
    bool inst_is_movnt = false;
    switch (opcode) {
        case XED_ICLASS_MOVNTDQ:
        case XED_ICLASS_MOVNTDQA:
        case XED_ICLASS_MOVNTI:
        case XED_ICLASS_MOVNTPD:
        case XED_ICLASS_MOVNTPS:
        case XED_ICLASS_MOVNTQ:
        case XED_ICLASS_MOVNTSD:
        case XED_ICLASS_MOVNTSS:
        case XED_ICLASS_VMOVNTDQ:
        case XED_ICLASS_VMOVNTDQA:
        case XED_ICLASS_VMOVNTPD:
        case XED_ICLASS_VMOVNTPS:
            inst_is_movnt = true;
            break;
        default:
            inst_is_movnt = false;
    }
    return inst_is_movnt;
}

// Check if instruction is a fence
bool IsFence(int opcode) {
    bool inst_is_fence = false;
    switch (opcode) {
        case XED_ICLASS_SFENCE:
        case XED_ICLASS_MFENCE:
            inst_is_fence = true;
            break;
        default:
            inst_is_fence = false;
    }
    return inst_is_fence;
}

trace::Instruction GetFlushType(int opcode) {
    switch (opcode) {
        case XED_ICLASS_CLFLUSH:
            return trace::Instruction::CLFLUSH;
        case XED_ICLASS_CLFLUSHOPT:
            return trace::Instruction::CLFLUSHOPT;
        case XED_ICLASS_CLWB:
            return trace::Instruction::CLWB;
        default:
            return trace::Instruction::ERROR;
    }
}

std::string GetLocation(void * addr) {
    PIN_LockClient();
    char ** strings = backtrace_symbols(&addr, 1);
    PIN_UnlockClient();
    char * location = strings[0];
    free(strings);
    return location;
}

static std::string _GetBacktraceSymbols(void**addresses, size_t size) {
    std::string trace;
    char ** strings;
    
    PIN_LockClient();
    strings = backtrace_symbols(addresses, size);
    PIN_UnlockClient();

    if(strings == NULL)
        return "(no symbols found)";


    for (size_t i = 0; i < size; i++) {
        
        trace += std::string(strings[i]) + "\n";

        /*
        std::string_view trace_view(strings[i]);
        size_t plus = trace_view.find("+0x");
        size_t file = trace_view.rfind("(");

        if(plus == std::string::npos || file == std::string::npos) {
            trace += std::string(strings[i]) + "\n";
        } else {
            trace += std::string(strings[i], plus) + " " + std::string(strings[i] + file) + "\n";
        }*/
    }
    free(strings);
    return trace;
}

#ifdef NO_BACKTRACE
typedef void* backtrace_t;

std::string GetBacktraceSymbols(backtrace_t trace) {
    return _GetBacktraceSymbols(&trace, 1);
} 

size_t get_traces_size() {
    return 0;
}
#else
typedef std::vector<void*>* backtrace_t;

std::string GetBacktraceSymbols(backtrace_t trace) {
    return _GetBacktraceSymbols(trace->data(), trace->size());
} 

struct BacktraceComparator {
    bool operator() (const backtrace_t r1, const backtrace_t r2) {
        if(r1->size() == r2->size())
            return memcmp(r1->data(), r2->data(), r1->size() * sizeof(void *)) < 0;

        return r1->size() < r2->size();
    }
};


static std::set<backtrace_t, struct BacktraceComparator> backtraces;

size_t get_traces_size() {
    size_t s = 0;
    for(const auto& it : backtraces) {
        s += sizeof(void *) * it->capacity();
    }
    return s;
}

int CustomBackTrace(const CONTEXT *ctxt, void ** addresses, uint64_t depth) {
    if(depth==0)
        return 0;

    uint64_t i = 0;

    addresses[i++] = (void *) PIN_GetContextReg(ctxt, REG_INST_PTR);
    if(!addresses[0])
        return 0;

    if(depth==1)
        return 1;

    uint64_t* frame = (uint64_t*) PIN_GetContextReg(ctxt, REG_RBP);
    uint64_t* old_frame = (uint64_t *) PIN_GetContextReg(ctxt, REG_RSP);

    while(i < depth && frame) {
        if(((uint64_t)(std::max(frame, old_frame) - std::min(frame,old_frame))) > 0x10000) {
            break;
        }
        old_frame = frame;

        addresses[i++] = (void *) *(frame+1);
        frame = (uint64_t*) *(frame);
    }

    return i;
} 

int StackUnwind(const CONTEXT *ctxt, void ** addresses, uint64_t depth) { 
    if(depth == 0)
        return 0;
    
    unw_context_t uc;
    unw_cursor_t cursor;
    PIN_LockClient();
    unw_init_local(&cursor, &uc);
    PIN_GetInitialContextForUnwind(ctxt, &cursor);


    uint64_t i = 0;

    unw_get_reg(&cursor, UNW_REG_IP, (unw_word_t*) &addresses[i]);
    while (++i < depth) {
        int res = unw_step(&cursor);

        if(res == 0)
            break;

        if(res < 0) {
            perror("libunwind");
            PIN_ExitApplication(-1);
        }

        unw_get_reg(&cursor, UNW_REG_IP, (unw_word_t*) &addresses[i]);
    }
    PIN_UnlockClient();

    return i;
}

extern PIN_MUTEX backtrace_mutex;

backtrace_t GetBacktrace(const CONTEXT *ctxt, uint64_t depth) {
    void * addresses[depth];
    int num_addresses;
    backtrace_t ret = nullptr;

    
    //PIN_LockClient();
    //num_addresses = PIN_Backtrace(ctxt, addresses, depth);
    //PIN_UnlockClient();
    
    
    num_addresses = CustomBackTrace(ctxt, addresses, depth);

    //num_addresses = StackUnwind(ctxt, addresses, depth);

    std::vector<void*> vec(addresses, addresses + num_addresses);
    
    PIN_MutexLock(&backtrace_mutex);

    auto vec_it = backtraces.find(&vec);

    if(vec_it == backtraces.end()) {
        vec_it = backtraces.insert(
            new std::vector<void *>(addresses, addresses + num_addresses)
        ).first;
    } 

    ret = *vec_it;
    PIN_MutexUnlock(&backtrace_mutex);
    return ret;
}

#endif

void InstrumentAdqRelRoutine(IMG img, const char * name, AFUNPTR beforePtr, AFUNPTR afterPtr,
    uint64_t value_passed = 0) {

    RTN routine = RTN_FindByName(img, name);

    if(RTN_Valid(routine)) {
        debug("Instrument %s: %p - %p || %ld\n", name, beforePtr, afterPtr, value_passed);

        RTN_Open(routine);

        RTN_InsertCall(routine, IPOINT_BEFORE, beforePtr,
                                IARG_THREAD_ID,
                                IARG_RETURN_IP,
                                IARG_UINT64, value_passed,
                                IARG_END);


        RTN_InsertCall(routine, IPOINT_AFTER, (AFUNPTR) afterPtr,
                                IARG_THREAD_ID,
                                IARG_UINT64, value_passed,
                                IARG_END);
        

        RTN_Close(routine);
    }
}

void InstrumentLockRoutine(IMG img, const char * name, AFUNPTR beforePtr, AFUNPTR afterPtr = NULL,
    int argument_index = 0, uint64_t constant_value = 0, bool negate = false) {

    RTN routine = RTN_FindByName(img, name);

    if(RTN_Valid(routine)) {
        debug("Instrument %s: %p - %p || %d %ld %d\n", name, beforePtr, afterPtr, argument_index, constant_value, (int)negate);

        RTN_Open(routine);

        if(afterPtr) {
            RTN_InsertCall(routine, IPOINT_BEFORE, beforePtr,
                                IARG_THREAD_ID,
                                IARG_RETURN_IP,
                                IARG_FUNCARG_ENTRYPOINT_VALUE, argument_index,
                                IARG_END);


            RTN_InsertCall(routine, IPOINT_AFTER, (AFUNPTR) afterPtr,
                                    IARG_THREAD_ID,
                                    IARG_FUNCRET_EXITPOINT_VALUE,
                                    IARG_UINT64, constant_value,
                                    IARG_UINT64, negate,
                                    IARG_END);
        } else {
            RTN_InsertCall(routine, IPOINT_BEFORE, beforePtr,
                                IARG_THREAD_ID,
                                IARG_RETURN_IP,
                                IARG_FUNCARG_ENTRYPOINT_VALUE, argument_index,
                                IARG_END);
        }

        RTN_Close(routine);
    }
}

enum LockType {MUTEX, WRITE, READ};
enum MemState {DIRTY, FLUSHED};

struct access_key_t {
    uint64_t address; 
    backtrace_t backtrace; 
    uint16_t clock_i;
    std::bitset<64> mask;
};

template<>
struct std::hash<access_key_t> {
    std::size_t operator()(const access_key_t &k) const {
        return std::hash<long>()(k.address) ^
               std::hash<backtrace_t>()(k.backtrace) ^
               std::hash<long>()(k.clock_i) ^
               std::hash<long>()(k.mask.to_ulong());
    }
};

template<>
struct std::equal_to<access_key_t> {
    bool operator()(const access_key_t &lhs, const access_key_t &rhs) const {
        return lhs.address == rhs.address &&
               lhs.backtrace == rhs.backtrace &&
               lhs.clock_i == rhs.clock_i &&
               lhs.mask == rhs.mask;
    }
};

template<>
struct std::hash<std::pair<backtrace_t, uint64_t>> {
    std::size_t operator()(const std::pair<backtrace_t, uint64_t> &p) const {
        return std::hash<backtrace_t>()(p.first) ^
               std::hash<long>()(p.second);
    }
};

static void parseError(std::string filename, const char *msg, int val) {
    if(!val) {
        debug("Failed to parse %s: %s\n", filename.c_str(), msg);
        exit(-1);
    }
}


std::map<std::string, enum LockType> mutexTypes = {
    {"regular", MUTEX},
    {"read", READ},
    {"shared", READ},
    {"write", WRITE},
    {"unique", WRITE}
};

struct MutexFunctionInfo{
    std::string name;
    int mutex_id_arg;
    int success_value;
    bool success_is_failure;
    enum LockType type;

    MutexFunctionInfo(char * _name, yaml_node_t * node, yaml_document_t * document) {
        name = _name;

        for(yaml_node_pair_t * cur = node->data.mapping.pairs.start; cur < node->data.mapping.pairs.top; cur++) { 
            int key_index = cur->key;
            int value_index = cur->value;

            yaml_node_t * key_node = yaml_document_get_node(document, key_index);
            yaml_node_t * value_node = yaml_document_get_node(document, value_index);
            switch(value_node->type) {
                case YAML_SCALAR_NODE:
                    if(!strcmp((char *)key_node->data.scalar.value, "mutex_id_arg")) {
                        mutex_id_arg = atoi((char *)value_node->data.scalar.value);
                    } else if(!strcmp((char *)key_node->data.scalar.value, "success_value")) {
                        success_value = atoi((char *)value_node->data.scalar.value);
                        success_is_failure = false;
                    } else if(!strcmp((char *)key_node->data.scalar.value, "failure_value")) {
                        success_value = atoi((char *)value_node->data.scalar.value);
                        success_is_failure = true;
                    } else if(!strcmp((char *)key_node->data.scalar.value, "type")) {
                        type = mutexTypes[(char *)value_node->data.scalar.value];
                    }
                    break;
                case YAML_SEQUENCE_NODE:
                case YAML_MAPPING_NODE:
                case YAML_NO_NODE:
                    parseError("", "invalid function info", 0);
                    break;
            }
        }
    }
    void Print() const {
        std::cout << "\t" << name 
                << " [" << mutex_id_arg << ", " << success_value << ", " << type << "]" << std::endl;
    }
};

struct MutexConfig {
    std::vector<MutexFunctionInfo> adquire;
    std::vector<MutexFunctionInfo> release;
    std::vector<MutexFunctionInfo> try_adquire;
    std::vector<std::string> adq_rel;

    int mutex_id_size;

    MutexConfig(std::string filename) {
        FILE *f = fopen(filename.c_str(), "rb");
        yaml_parser_t parser;
        yaml_document_t document;

        if(f == NULL) {
            perror("Failed to open file!");
            return;
        }

        if(!yaml_parser_initialize(&parser)) {
            fputs("Failed to initialize parser!\n", stderr);
            return;
        }

        yaml_parser_set_input_file(&parser, f);

        if (!yaml_parser_load(&parser, &document)) {
            fputs("Failed to load document!\n", stderr);
            return;
        }

        yaml_node_t * root = yaml_document_get_root_node(&document);

    
        parseError(filename, "root must be a mapping", root->type == YAML_MAPPING_NODE);



        for(yaml_node_pair_t * cur = root->data.mapping.pairs.start; cur < root->data.mapping.pairs.top; cur++) {

            int key_index = cur->key;
            int value_index = cur->value;

            
            yaml_node_t * key_node = yaml_document_get_node(&document, key_index);
            yaml_node_t * value_node = yaml_document_get_node(&document, value_index);

            switch(value_node->type) {
                case YAML_SCALAR_NODE:
                    if(!strcmp((char *)key_node->data.scalar.value, "mutex_id_size")) {
                        mutex_id_size = atoi((char *)value_node->data.scalar.value);
                    } else {
                        parseError(filename, "invalid scalar", 0);
                    }
                    break;
                case YAML_MAPPING_NODE:
                    if(!strcmp((char *)key_node->data.scalar.value, "adquire")) {
                        PopulateFunctions(adquire, value_node, &document);
                    } else if(!strcmp((char *)key_node->data.scalar.value, "try_adquire")) {
                        PopulateFunctions(try_adquire, value_node, &document);
                    } else if(!strcmp((char *)key_node->data.scalar.value, "release")) {
                        PopulateFunctions(release, value_node, &document);
                    } else {
                        parseError(filename, "invalid mapping", 0);
                    }
                    break;
                case YAML_SEQUENCE_NODE:
                    if(!strcmp((char *)key_node->data.scalar.value, "adq_rel")) {
                        ExtractFunctionList(adq_rel, value_node, &document);
                    } else {
                        parseError(filename, "invalid sequence", 0);
                    }
                    break;
                case YAML_NO_NODE:
                    parseError(filename, "invalid node", 0);
                    break;
            }
        }

        yaml_document_delete(&document);


        yaml_parser_delete(&parser);
        fclose(f);
    }

    void Print() const {
        std::cout << "mutex size: " << mutex_id_size << std::endl;

        auto funcs = {adquire, release, try_adquire};

        for(const auto & func : funcs) {
            for(const MutexFunctionInfo& info : func) {
                info.Print();
            }
        }
    }

private:
    void ExtractFunctionList(std::vector<std::string> & funcs, yaml_node_t * node, yaml_document_t * document) {
        for(yaml_node_item_t * cur = node->data.sequence.items.start; cur < node->data.sequence.items.top; cur++) { 
            yaml_node_t * value_node = yaml_document_get_node(document, *cur);

            switch(value_node->type) {
                case YAML_SCALAR_NODE:
                    funcs.emplace_back((char *)value_node->data.scalar.value);
                    break;
                case YAML_SEQUENCE_NODE:
                case YAML_NO_NODE:
                case YAML_MAPPING_NODE:
                    parseError("", "invalid function list element", 0);
                    break;
            }
        }
    }
    void PopulateFunctions(std::vector<MutexFunctionInfo> & funcs, yaml_node_t * node, yaml_document_t * document) {
        for(yaml_node_pair_t * cur = node->data.mapping.pairs.start; cur < node->data.mapping.pairs.top; cur++) { 
            int key_index = cur->key;
            int value_index = cur->value;

            
            yaml_node_t * key_node = yaml_document_get_node(document, key_index);
            yaml_node_t * value_node = yaml_document_get_node(document, value_index);

            switch(value_node->type) {
                case YAML_MAPPING_NODE:
                    funcs.emplace_back((char *)key_node->data.scalar.value, value_node, document);
                    break;
                case YAML_SEQUENCE_NODE:
                case YAML_SCALAR_NODE:
                case YAML_NO_NODE:
                    parseError("", "invalid function info element", 0);
                    break;
            }
        }
    }
};
#endif  // !MUMAK_UTILS
