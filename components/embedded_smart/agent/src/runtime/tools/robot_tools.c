#include "litecrab/external_tool.h"

#include "litecrab/json.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t backendMutex = PTHREAD_MUTEX_INITIALIZER;
static LiteCrabExternalToolBackend activeBackend;
static atomic_ullong nextInvocationId = 1;

int LiteCrabExternalToolSetBackend(const LiteCrabExternalToolBackend* backend) {
    if (!backend || !backend->invoke)
        return -1;
    pthread_mutex_lock(&backendMutex);
    activeBackend = *backend;
    pthread_mutex_unlock(&backendMutex);
    return 0;
}

void LiteCrabExternalToolResetBackend(void) {
    pthread_mutex_lock(&backendMutex);
    memset(&activeBackend, 0, sizeof activeBackend);
    pthread_mutex_unlock(&backendMutex);
}

static int robot_execute(CrabRuntime* runtime,
                         const CrabToolCall* call,
                         CrabRawResult* raw) {
    (void) runtime;
    char arguments[4096];
    LjBuf buffer;
    LjBufInit(&buffer, arguments, sizeof arguments);
    LjAppend(&buffer, "{");
    for (size_t index = 0; index < call->argCount; index++) {
        if (index)
            LjAppend(&buffer, ",");
        LjAppendJsonString(&buffer, call->args[index].name);
        LjAppend(&buffer, ":");
        if (call->args[index].value.type == CRAB_TOOL_VALUE_STRING)
            LjAppendJsonString(&buffer, call->args[index].value.stringValue);
        else if (call->args[index].value.type == CRAB_TOOL_VALUE_INT)
            LjAppend(&buffer, "%lld", (long long) call->args[index].value.intValue);
        else if (call->args[index].value.type == CRAB_TOOL_VALUE_BOOL)
            LjAppend(&buffer, "%s", call->args[index].value.boolValue ? "true" : "false");
        else
            LjAppend(&buffer, "null");
    }
    LjAppend(&buffer, "}");
    if (buffer.failed)
        return CRAB_ERROR_INVALID_ARG;
    pthread_mutex_lock(&backendMutex);
    LiteCrabExternalToolBackend backend = activeBackend;
    pthread_mutex_unlock(&backendMutex);
    if (!backend.invoke) {
        CrabRawResultSetStderr(raw, "external robot backend is unavailable");
        raw->exitCode = CRAB_ERROR_EXECUTE_FAILED;
        return CRAB_ERROR_EXECUTE_FAILED;
    }
    char result[CRAB_TOOL_CONTENT_MAX];
    result[0] = 0;
    uint64_t invocationId = atomic_fetch_add(&nextInvocationId, 1);
    int status = backend.invoke(backend.context,
                                invocationId,
                                call->toolName,
                                arguments,
                                result,
                                sizeof result,
                                NULL);
    if (status) {
        CrabRawResultSetStderr(raw, result[0] ? result : "external robot backend failed");
        raw->exitCode = status;
        return status;
    }
    if (strlen(result) >= CRAB_TOOL_CONTENT_MAX || LjValidate(result, LJ_OBJECT)) {
        CrabRawResultSetStderr(raw, "external robot backend returned invalid JSON");
        raw->exitCode = CRAB_ERROR_FILTER_FAILED;
        return CRAB_ERROR_FILTER_FAILED;
    }
    return CrabRawResultSetStdout(raw, result);
}

static int robot_filter(const CrabToolCall* call,
                        const CrabRawResult* raw,
                        CrabFilteredResult* filtered) {
    (void) call;
    filtered->success = raw->exitCode == 0;
    snprintf(filtered->summary,
             sizeof filtered->summary,
             "%s",
             filtered->success ? "robot action completed" : "robot action failed");
    snprintf(filtered->content,
             sizeof filtered->content,
             "%s",
             raw->stdoutData ? raw->stdoutData : raw->stderrData ? raw->stderrData : "");
    filtered->contentSize = strlen(filtered->content);
    return 0;
}

#define VINT(value) {.type = CRAB_TOOL_VALUE_INT, .intValue = (value)}

static int robot_volume_filter(const CrabToolCall* call,
                               const CrabRawResult* raw,
                               CrabFilteredResult* filtered) {
    filtered->success = raw->exitCode == 0;
    if (filtered->success) {
        const CrabToolArg* volume = CrabToolCallFindArg(call, "volume");
        snprintf(filtered->summary,
                 sizeof filtered->summary,
                 "volume set to %d%%",
                 volume ? (int) volume->value.intValue : 0);
        snprintf(filtered->content,
                 sizeof filtered->content,
                 "%s",
                 raw->stdoutData ? raw->stdoutData : "");
    } else {
        snprintf(filtered->summary, sizeof filtered->summary, "volume control failed");
        snprintf(filtered->content,
                 sizeof filtered->content,
                 "%s",
                 raw->stderrData ? raw->stderrData : "");
    }
    filtered->contentSize = strlen(filtered->content);
    return 0;
}

static const char* const robotActionValues[] = {
    "walk", "walk_backward", "turn", "moonwalk",
    "bend", "shake_leg", "sit", "updown", "swing",
    "jump", "takeoff",
    "hands_up", "hands_down", "hand_wave",
    "windmill", "fitness",
    "greeting", "shy",
    "radio_calisthenics", "magic_circle", "showcase",
    "home", "stop",
};

static const CrabToolParamSpec robotActionParameters[] = {
    {.name = "action",
     .description = "Predefined robot action name",
     .type = CRAB_TOOL_VALUE_STRING,
     .required = 1,
     .enumValues = robotActionValues,
     .enumCount = sizeof robotActionValues / sizeof robotActionValues[0]},
    {.name = "steps", .description = "Repeat count for the action; ignored by home and stop",
     .type = CRAB_TOOL_VALUE_INT, .hasDefault = 1, .defaultValue = VINT(3), .minInt = 1, .maxInt = 100},
    {.name = "speed", .description = "Action cycle period in milliseconds; smaller is faster",
     .type = CRAB_TOOL_VALUE_INT, .hasDefault = 1, .defaultValue = VINT(1000), .minInt = 100, .maxInt = 3000},
    {.name = "direction", .description = "Action direction: 1=forward/left, -1=backward/right, 0=both hands",
     .type = CRAB_TOOL_VALUE_INT, .hasDefault = 1, .defaultValue = VINT(1), .minInt = -1, .maxInt = 1},
    {.name = "amount", .description = "Action amplitude in degrees",
     .type = CRAB_TOOL_VALUE_INT, .hasDefault = 1, .defaultValue = VINT(25), .minInt = 0, .maxInt = 170},
};

static const CrabToolParamSpec robotPoseParameters[] = {
    {.name = "left_leg", .description = "Left leg servo target angle in degrees", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(90), .minInt = 0, .maxInt = 180},
    {.name = "right_leg", .description = "Right leg servo target angle in degrees", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(90), .minInt = 0, .maxInt = 180},
    {.name = "left_foot", .description = "Left foot servo target angle in degrees", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(90), .minInt = 0, .maxInt = 180},
    {.name = "right_foot", .description = "Right foot servo target angle in degrees", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(90), .minInt = 0, .maxInt = 180},
    {.name = "left_hand", .description = "Left hand servo target angle in degrees", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(45), .minInt = 0, .maxInt = 180},
    {.name = "right_hand", .description = "Right hand servo target angle in degrees", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(135), .minInt = 0, .maxInt = 180},
    {.name = "time_ms", .description = "Transition time in milliseconds", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(700), .minInt = 100, .maxInt = 3000},
};

static const char* const robotSoundValues[] = {
    "applause", "cheer", "laugh", "sad", "alert", "beep", "startup", "shutdown",
};

static const CrabToolParamSpec robotSoundParameters[] = {
    {.name = "sound", .description = "Preset sound effect name", .type = CRAB_TOOL_VALUE_STRING,
     .required = 1, .enumValues = robotSoundValues,
     .enumCount = sizeof robotSoundValues / sizeof robotSoundValues[0]},
    {.name = "volume", .description = "Volume percentage", .type = CRAB_TOOL_VALUE_INT,
     .hasDefault = 1, .defaultValue = VINT(50), .minInt = 0, .maxInt = 100},
};

static const CrabToolParamSpec robotVolumeParameters[] = {
    {.name = "volume", .description = "Audio output volume percentage", .type = CRAB_TOOL_VALUE_INT,
     .required = 1, .minInt = 0, .maxInt = 100},
};

static const CrabToolSpec robotTools[] = {
    {.name = "robot.action",
     .description = "Execute one of 21 predefined robot actions, home, or emergency stop",
     .enabledByDefault = 1, .readonly = 0, .permissions = CRAB_TOOL_PERMISSION_DEVICE_CONTROL,
     .concurrency = CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, .riskLevel = CRAB_TOOL_RISK_HIGH,
     .params = robotActionParameters,
     .paramCount = sizeof robotActionParameters / sizeof robotActionParameters[0],
     .execute = robot_execute, .filter = robot_filter},
    {.name = "robot.pose",
     .description = "Set target angles for the six robot servos with a transition time",
     .enabledByDefault = 1, .readonly = 0, .permissions = CRAB_TOOL_PERMISSION_DEVICE_CONTROL,
     .concurrency = CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, .riskLevel = CRAB_TOOL_RISK_HIGH,
     .params = robotPoseParameters,
     .paramCount = sizeof robotPoseParameters / sizeof robotPoseParameters[0],
     .execute = robot_execute, .filter = robot_filter},
    {.name = "robot.play_sound",
     .description = "Play a preset sound effect file rather than synthesized speech",
     .enabledByDefault = 1, .readonly = 0, .permissions = CRAB_TOOL_PERMISSION_DEVICE_CONTROL,
     .concurrency = CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, .riskLevel = CRAB_TOOL_RISK_HIGH,
     .params = robotSoundParameters,
     .paramCount = sizeof robotSoundParameters / sizeof robotSoundParameters[0],
     .execute = robot_execute, .filter = robot_filter},
    {.name = "robot.set_volume",
     .description = "Set the device audio output volume as a percentage",
     .enabledByDefault = 1, .readonly = 0, .permissions = CRAB_TOOL_PERMISSION_DEVICE_CONTROL,
     .concurrency = CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, .riskLevel = CRAB_TOOL_RISK_HIGH,
     .params = robotVolumeParameters,
     .paramCount = sizeof robotVolumeParameters / sizeof robotVolumeParameters[0],
     .execute = robot_execute, .filter = robot_volume_filter},
    {.name = "robot.stop",
     .description = "Immediately stop all current robot motion",
     .enabledByDefault = 1, .readonly = 0, .permissions = CRAB_TOOL_PERMISSION_DEVICE_CONTROL,
     .concurrency = CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, .riskLevel = CRAB_TOOL_RISK_HIGH,
     .params = NULL, .paramCount = 0, .execute = robot_execute, .filter = robot_filter},
};

int LiteCrabRegisterRobotTools(CrabRuntime* runtime) {
    if (!runtime)
        return CRAB_ERROR_INVALID_ARG;
    for (size_t index = 0; index < sizeof robotTools / sizeof robotTools[0]; index++) {
        int status = CrabRegistryRegister(&runtime->registry, &robotTools[index]);
        if (status)
            return status;
    }
    return 0;
}

size_t LiteCrabRobotActionCount(void) {
    return sizeof robotActionValues / sizeof robotActionValues[0];
}

size_t LiteCrabRobotToolsCount(void) {
    return sizeof robotTools / sizeof robotTools[0];
}
