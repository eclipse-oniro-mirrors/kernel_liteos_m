#include "litecrab/agent.h"

#include <string.h>

int main(void) {
    LiteCrabAgent* agent = (LiteCrabAgent*) 1;
    if (LiteCrabAgentCreate(NULL, &agent) != LITECRAB_AGENT_INVALID_ARGUMENT || agent != (void*) 1)
        return 1;
    LiteCrabRequest request = {.content = "hello"};
    LiteCrabResponse response;
    memset(&response, 0, sizeof response);
    if (LiteCrabAgentExecuteSync(NULL, &request, &response) != LITECRAB_AGENT_INVALID_ARGUMENT)
        return 1;
    if (LiteCrabAgentCancel(NULL, "request") != LITECRAB_AGENT_INVALID_ARGUMENT)
        return 1;
    LiteCrabAgentDestroy(NULL);
    return 0;
}
