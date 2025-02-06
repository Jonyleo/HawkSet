#include <iostream>

#include "pin.H"

#include "utils.hpp"


KNOB<std::string> KnobPMMount(KNOB_MODE_WRITEONCE, "pintool", "pm-mount",
                              "/mnt/pmem0/", "PM mount in filesystem");


/*void TraceWrite(THREADID tid, const CONTEXT *ctxt, ADDRINT ip, ADDRINT address, uint32_t size, trace::Instruction type) {
    if (!IsPMAddress(address, size, tid)) 
        return;
    
    std::cerr << "write pm" << std::endl;

    backtrace_t backtrace = GetBacktrace(ctxt, 50);

    std::string trace = GetBacktraceSymbols(backtrace);


    std::cerr << trace << std::endl;
}

VOID TraceInstructions(INS ins, VOID *v) {
    if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) {
        int opcode = INS_Opcode(ins);
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
    } 
}*/
PIN_MUTEX backtrace_mutex;
int main(int argc, char *argv[]) {
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    //pm_mount = KnobPMMount.Value().c_str();

    std::cerr << "Heloo" << std::endl;
    //INS_AddInstrumentFunction(TraceInstructions, 0);

    //PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    //PIN_AddSyscallExitFunction(SyscallExit, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}