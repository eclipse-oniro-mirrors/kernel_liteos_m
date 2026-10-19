#include "litecrab/capability.h"

#include <stdio.h>

static int alarm_start(LiteCrabContext* context) {
    if (AlarmAppStart(&context->alarm, &context->alarmOptions)) {
        fprintf(stderr, "alarm startup failed; check configuration and credentials\n");
        return -1;
    }
    return 0;
}

static int alarm_quiesce(LiteCrabContext* context) {
    return AlarmAppQuiesce(&context->alarm);
}

static void alarm_stop(LiteCrabContext* context) {
    (void) AlarmAppStop(&context->alarm);
}

const LiteCrabCapabilityOps litecrab_cap_alarm = {
    .id = "alarm", .start = alarm_start, .quiesce = alarm_quiesce, .stop = alarm_stop};
