/*
 * Shim: OHOS liteos_m keeps LosQueueCB in the public header and has no
 * los_queue_pri.h; the staged boxes sources include the old private name.
 */
#ifndef LOS_QUEUE_PRI_H
#define LOS_QUEUE_PRI_H
#include "los_queue.h"
#endif
