#include <iostream>

#include "pin.H"

#include "utils.hpp"


void Before(const CONTEXT *ctxt, ADDRINT retAddr, ADDRINT arg) {
	backtrace_t backtrace = GetBacktrace(ctxt, 50);

	std::cerr << GetBacktraceSymbols(backtrace) << std::endl;
}

VOID ImageLoad(IMG img, VOID *v) {

    RTN routine = RTN_FindByName(img, "test_trap_hawkset");

    if(RTN_Valid(routine)) {
        RTN_Open(routine);

        
        RTN_InsertCall(routine, IPOINT_BEFORE, (AFUNPTR) Before,
        					IARG_CONST_CONTEXT,
                            IARG_RETURN_IP,
                            IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                            IARG_END);
        
        RTN_Close(routine);
    }
}


int main(int argc, char *argv[]) {
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    
    IMG_AddInstrumentFunction(ImageLoad, (void *) 0);

    PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    PIN_AddSyscallExitFunction(SyscallExit, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}