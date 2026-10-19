#include "litecrab/external_tool.h"

#include <stdio.h>
#include <string.h>

static int invocationCount;
static char invokedTool[CRAB_TOOL_NAME_MAX];
static char invokedArguments[1024];

static int invoke(void* context,
                  uint64_t invocationId,
                  const char* toolId,
                  const char* arguments,
                  char* result,
                  size_t resultSize,
                  const LiteCrabDeadline* deadline) {
    (void) context;
    (void) invocationId;
    (void) deadline;
    invocationCount++;
    snprintf(invokedTool, sizeof invokedTool, "%s", toolId);
    snprintf(invokedArguments, sizeof invokedArguments, "%s", arguments);
    snprintf(result, resultSize, "{\"ok\":true}");
    return 0;
}

static void set_string_arg(CrabToolCall* call,
                           size_t index,
                           const char* name,
                           const char* value) {
    snprintf(call->args[index].name, sizeof call->args[index].name, "%s", name);
    call->args[index].value.type = CRAB_TOOL_VALUE_STRING;
    call->args[index].value.stringValue = value;
    if (call->argCount <= index)
        call->argCount = index + 1;
}

static void set_int_arg(CrabToolCall* call, size_t index, const char* name, int64_t value) {
    snprintf(call->args[index].name, sizeof call->args[index].name, "%s", name);
    call->args[index].value.type = CRAB_TOOL_VALUE_INT;
    call->args[index].value.intValue = value;
    if (call->argCount <= index)
        call->argCount = index + 1;
}

static int call_tool(CrabRuntime* runtime, CrabToolCall* call) {
    CrabToolResponse response;
    int status = CrabRuntimeCallTool(runtime, call, &response);
    return status || !response.success || !strstr(response.content, "\"ok\":true");
}

int main(void) {
    CrabRuntime runtime;
    CrabRuntimeConfig config = {.workspaceRoot = ".", .rawResultDir = ".crab/raw"};
    if (CrabRuntimeInit(&runtime, &config))
        return 1;
    LiteCrabExternalToolBackend backend = {.invoke = invoke};
    if (LiteCrabExternalToolSetBackend(&backend) || LiteCrabRegisterRobotTools(&runtime))
        return 1;

    CrabToolCatalogView catalog;
    if (CrabRuntimeGetToolCatalog(&runtime, &catalog) || catalog.toolCount != 5 ||
        LiteCrabRobotToolsCount() != 5 || LiteCrabRobotActionCount() != 23)
        return 1;
    const char* expectedTools[] = {
        "robot.action", "robot.pose", "robot.play_sound", "robot.set_volume", "robot.stop",
    };
    for (size_t index = 0; index < sizeof expectedTools / sizeof expectedTools[0]; index++)
        if (!CrabRegistryFind(&runtime.registry, expectedTools[index]))
            return 1;

    CrabToolCall action = {0};
    snprintf(action.callId, sizeof action.callId, "1");
    snprintf(action.toolName, sizeof action.toolName, "robot.action");
    CrabToolResponse response;
    if (CrabRuntimeCallTool(&runtime, &action, &response) != CRAB_ERROR_INVALID_ARG ||
        invocationCount != 0)
        return 1;
    set_string_arg(&action, 0, "action", "wave");
    if (CrabRuntimeCallTool(&runtime, &action, &response) != CRAB_ERROR_INVALID_ARG ||
        invocationCount != 0)
        return 1;
    action.args[0].value.stringValue = "walk";
    set_int_arg(&action, 1, "steps", 3);
    set_int_arg(&action, 2, "speed", 800);
    set_int_arg(&action, 3, "direction", 1);
    set_int_arg(&action, 4, "amount", 25);
    if (call_tool(&runtime, &action) || strcmp(invokedTool, "robot.action") ||
        !strstr(invokedArguments, "\"action\":\"walk\"") ||
        !strstr(invokedArguments, "\"steps\":3"))
        return 1;

    CrabToolCall pose = {0};
    snprintf(pose.callId, sizeof pose.callId, "2");
    snprintf(pose.toolName, sizeof pose.toolName, "robot.pose");
    set_int_arg(&pose, 0, "left_hand", 181);
    set_int_arg(&pose, 1, "right_hand", 0);
    set_int_arg(&pose, 2, "time_ms", 700);
    if (CrabRuntimeCallTool(&runtime, &pose, &response) != CRAB_ERROR_INVALID_ARG ||
        invocationCount != 1)
        return 1;
    pose.args[0].value.intValue = 180;
    if (call_tool(&runtime, &pose) || strcmp(invokedTool, "robot.pose"))
        return 1;

    CrabToolCall sound = {0};
    snprintf(sound.callId, sizeof sound.callId, "3");
    snprintf(sound.toolName, sizeof sound.toolName, "robot.play_sound");
    set_string_arg(&sound, 0, "sound", "music");
    set_int_arg(&sound, 1, "volume", 50);
    if (CrabRuntimeCallTool(&runtime, &sound, &response) != CRAB_ERROR_INVALID_ARG ||
        invocationCount != 2)
        return 1;
    sound.args[0].value.stringValue = "cheer";
    if (call_tool(&runtime, &sound) || strcmp(invokedTool, "robot.play_sound"))
        return 1;

    CrabToolCall volume = {0};
    snprintf(volume.callId, sizeof volume.callId, "4");
    snprintf(volume.toolName, sizeof volume.toolName, "robot.set_volume");
    set_int_arg(&volume, 0, "volume", 101);
    if (CrabRuntimeCallTool(&runtime, &volume, &response) != CRAB_ERROR_INVALID_ARG ||
        invocationCount != 3)
        return 1;
    volume.args[0].value.intValue = 60;
    if (call_tool(&runtime, &volume) || strcmp(invokedTool, "robot.set_volume") ||
        strcmp(invokedArguments, "{\"volume\":60}"))
        return 1;

    CrabToolCall stop = {0};
    snprintf(stop.callId, sizeof stop.callId, "5");
    snprintf(stop.toolName, sizeof stop.toolName, "robot.stop");
    if (call_tool(&runtime, &stop) || strcmp(invokedTool, "robot.stop") ||
        strcmp(invokedArguments, "{}"))
        return 1;

    LiteCrabExternalToolResetBackend();
    CrabRuntimeDestroy(&runtime);
    return invocationCount == 5 ? 0 : 1;
}
