#include "litecrab/kernel.h"
#include "litecrab/memory.h"
#include "litecrab/runtime.h"

static int do_memory_remember(CrabRuntime* r,const CrabToolCall* c,CrabRawResult* raw) {
    (void)r;
    int result=MemoryRemember(AgentSessionStateGetUserId(),
                              CrabToolArgsGetString(c,"category",NULL),
                              CrabToolArgsGetString(c,"content",NULL),"agent");
    const char* message=result==0 ? "{\"success\":true,\"code\":\"stored\",\"message\":\"memory stored\"}" :
        result==1 ? "{\"success\":true,\"code\":\"duplicate\",\"message\":\"memory already exists\"}" :
        result==-3 ? "{\"success\":false,\"code\":\"memory_full\",\"message\":\"long-term memory is full\"}" :
        result==-1 ? "{\"success\":false,\"code\":\"invalid_input\",\"message\":\"memory was not saved\"}" :
        "{\"success\":false,\"code\":\"io_error\",\"message\":\"memory was not saved\"}";
    raw->exitCode=result==0 || result==1 ? 0 : 1;
    return CrabRawResultSetStdout(raw,message);
}
int CrabRuntimeRegisterMemoryTool(CrabRuntime* r) {
    static const char* categories[]={"preference","fact","correction","experience","failure","note"};
    static const CrabToolParamSpec params[]={
        {.name="category",.description="memory category",.type=CRAB_TOOL_VALUE_STRING,.required=1,.enumValues=categories,.enumCount=6},
        {.name="content",.description="short memory statement",.type=CRAB_TOOL_VALUE_STRING,.required=1}
    };
    static const CrabToolSpec spec={"memory_remember",
        "Save a concise, safe user memory. Only explicit requests or stable verified information. Never save secrets. Check the JSON result before claiming success.",
        1,0,0,CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE,CRAB_TOOL_RISK_LOW,
        params,2,do_memory_remember,CrabBasicFilter};
    return r ? CrabRegistryRegister(&r->registry,&spec) : CRAB_ERROR_INVALID_ARG;
}
