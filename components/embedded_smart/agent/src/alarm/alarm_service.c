#include "litecrab/hub.h"
#include "litecrab/json.h"
#include "litecrab/observability.h"
#include "monitor.h"

#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int used;
    int deadLetter;
    int handled;
    int attempts;
    uint64_t nextMs;
    uint64_t firstDispatchEpochMs;
    uint64_t holdDeadlineMs;
    char id[64];
    AlarmNotify notify;
} Delivery;

struct AlarmService {
    AlarmConfig cfg;
    pthread_mutex_t mu;
    pthread_cond_t wake;
    pthread_t listener;
    pthread_t dispatcher;
    int listenerRunning;
    int dispatcherRunning;
    int quiescing;
    int stopping;
    uint64_t sequence;
    Delivery deliveries[ALARM_QUEUE_CAPACITY];
    char seen[ALARM_SEEN_CAPACITY][65];
    size_t seenCount;
    size_t seenNext;
    char spoolPath[1200];
    char activeRequestId[64];
};

typedef struct {
    int32_t used;
    int32_t deadLetter;
    int32_t handled;
    int32_t attempts;
    uint64_t firstDispatchEpochMs;
    char id[64];
    char key[65];
    char message[ALARM_MESSAGE_MAX];
} AlarmSpoolRecord;
typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t recordCount;
    uint32_t seenCount;
    uint32_t seenNext;
    AlarmSpoolRecord records[ALARM_QUEUE_CAPACITY];
    char seen[ALARM_SEEN_CAPACITY][65];
} AlarmSpoolFile;

/* Read-only compatibility with the spool written before first-dispatch time
 * became durable.  New snapshots are always written as LCRALM4. */
typedef struct {
    int32_t used;
    int32_t deadLetter;
    int32_t handled;
    int32_t attempts;
    char id[64];
    char key[65];
    char message[ALARM_MESSAGE_MAX];
} AlarmSpoolRecordV3;
typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t recordCount;
    uint32_t seenCount;
    uint32_t seenNext;
    AlarmSpoolRecordV3 records[ALARM_QUEUE_CAPACITY];
    char seen[ALARM_SEEN_CAPACITY][65];
} AlarmSpoolFileV3;

static uint64_t now_ms(void);
static uint64_t epoch_ms(void);
static int has_seen(const AlarmService* service, const char* key);
static void remember(AlarmService* service, const char* key);

static int write_all(int fd, const void* data, size_t size) {
    const unsigned char* cursor = data;
    while (size) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        cursor += written;
        size -= (size_t) written;
    }
    return 0;
}
static int ensure_directory(const char* path) {
    struct stat status;
    if (!stat(path, &status))
        return S_ISDIR(status.st_mode) ? 0 : -1;
    return errno == ENOENT && !mkdir(path, 0700) ? 0 : -1;
}
static int spool_save_locked(AlarmService* service) {
    if (!service->spoolPath[0])
        return 0;
    AlarmSpoolFile* file = calloc(1, sizeof *file);
    if (!file)
        return -1;
    memcpy(file->magic, "LCRALM4", 8);
    file->version = 4;
    file->recordCount = ALARM_QUEUE_CAPACITY;
    file->seenCount = (uint32_t) service->seenCount;
    file->seenNext = (uint32_t) service->seenNext;
    memcpy(file->seen, service->seen, sizeof file->seen);
    for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++) {
        Delivery* delivery = &service->deliveries[i];
        AlarmSpoolRecord* record = &file->records[i];
        record->used = delivery->used;
        record->deadLetter = delivery->deadLetter;
        record->handled = delivery->handled;
        record->attempts = delivery->attempts;
        record->firstDispatchEpochMs = delivery->firstDispatchEpochMs;
        snprintf(record->id, sizeof record->id, "%s", delivery->id);
        snprintf(record->key, sizeof record->key, "%s", delivery->notify.key);
        snprintf(record->message, sizeof record->message, "%s", delivery->notify.message);
    }
    char temporary[1280];
    snprintf(temporary, sizeof temporary, "%s.tmp.%ld", service->spoolPath, (long) getpid());
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int result = fd < 0 || write_all(fd, file, sizeof *file) || fsync(fd);
    if (fd >= 0 && close(fd))
        result = -1;
    if (!result && rename(temporary, service->spoolPath))
        result = -1;
    if (!result) {
        char directory[sizeof service->spoolPath];
        snprintf(directory, sizeof directory, "%s", service->spoolPath);
        char* slash = strrchr(directory, '/');
        if (slash) {
            *slash = 0;
            int directoryFd = open(directory, O_RDONLY | O_DIRECTORY);
            if (directoryFd < 0 || fsync(directoryFd))
                result = -1;
            if (directoryFd >= 0)
                close(directoryFd);
        }
    }
    if (result)
        unlink(temporary);
    free(file);
    return result ? -1 : 0;
}
static int spool_load(AlarmService* service) {
    if (!service->spoolPath[0])
        return 0;
    int fd = open(service->spoolPath, O_RDONLY);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    struct stat status;
    if (fstat(fd, &status)) {
        close(fd);
        return -1;
    }
    size_t inputSize = (size_t) status.st_size;
    if (inputSize != sizeof(AlarmSpoolFile) && inputSize != sizeof(AlarmSpoolFileV3)) {
        close(fd);
        return -1;
    }
    void* input = malloc(inputSize);
    AlarmSpoolFile* file = calloc(1, sizeof *file);
    if (!input || !file) {
        free(input);
        free(file);
        close(fd);
        return -1;
    }
    size_t offset = 0;
    while (offset < inputSize) {
        ssize_t got = read(fd, (unsigned char*) input + offset, inputSize - offset);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        offset += (size_t) got;
    }
    close(fd);
    int valid = 0;
    if (offset == sizeof(AlarmSpoolFile)) {
        AlarmSpoolFile* current = input;
        if (!memcmp(current->magic, "LCRALM4", 8) && current->version == 4) {
            *file = *current;
            valid = 1;
        }
    } else if (offset == sizeof(AlarmSpoolFileV3)) {
        AlarmSpoolFileV3* legacy = input;
        if (!memcmp(legacy->magic, "LCRALM3", 8) && legacy->version == 3) {
            file->recordCount = legacy->recordCount;
            file->seenCount = legacy->seenCount;
            file->seenNext = legacy->seenNext;
            memcpy(file->seen, legacy->seen, sizeof file->seen);
            for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++) {
                AlarmSpoolRecord* destination = &file->records[i];
                AlarmSpoolRecordV3* source = &legacy->records[i];
                source->id[sizeof source->id - 1] = 0;
                source->key[sizeof source->key - 1] = 0;
                source->message[sizeof source->message - 1] = 0;
                destination->used = source->used;
                destination->deadLetter = source->deadLetter;
                destination->handled = source->handled;
                destination->attempts = source->attempts;
                snprintf(destination->id, sizeof destination->id, "%s", source->id);
                snprintf(destination->key, sizeof destination->key, "%s", source->key);
                snprintf(destination->message, sizeof destination->message, "%s", source->message);
            }
            valid = 1;
        }
    }
    free(input);
    if (!valid || file->recordCount != ALARM_QUEUE_CAPACITY ||
        file->seenCount > ALARM_SEEN_CAPACITY || file->seenNext >= ALARM_SEEN_CAPACITY) {
        free(file);
        return -1;
    }
    service->seenCount = file->seenCount;
    service->seenNext = file->seenNext;
    memcpy(service->seen, file->seen, sizeof service->seen);
    for (size_t i = 0; i < service->seenCount; i++)
        service->seen[i][sizeof service->seen[i] - 1] = 0;
    int pendingRestored = 0;
    for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++) {
        AlarmSpoolRecord* record = &file->records[i];
        if (!record->used)
            continue;
        record->id[sizeof record->id - 1] = 0;
        record->key[sizeof record->key - 1] = 0;
        record->message[sizeof record->message - 1] = 0;
        if (record->handled) {
            if (!has_seen(service, record->key))
                remember(service, record->key);
            continue;
        }
        /* Old versions could persist a whole snapshot. Keep at most one
         * actionable delivery; the rest must be rediscovered by a fresh
         * active query after the retained item settles. */
        if (!record->deadLetter && pendingRestored) {
            LogPrint("[alarm] deferred legacy pending id=%s until active refresh", record->id);
            continue;
        }
        if (!record->deadLetter)
            pendingRestored = 1;
        Delivery* delivery = &service->deliveries[i];
        delivery->used = 1;
        delivery->deadLetter = record->deadLetter != 0;
        delivery->handled = record->handled != 0;
        delivery->attempts = record->attempts;
        delivery->firstDispatchEpochMs = record->firstDispatchEpochMs;
        if (delivery->firstDispatchEpochMs) {
            uint64_t currentEpoch = epoch_ms();
            uint64_t elapsed = currentEpoch >= delivery->firstDispatchEpochMs
                                   ? currentEpoch - delivery->firstDispatchEpochMs
                                   : 0;
            uint64_t remaining = elapsed >= ALARM_AGENT_DEADLINE_MS
                                     ? 0
                                     : ALARM_AGENT_DEADLINE_MS - elapsed;
            delivery->holdDeadlineMs = now_ms() + remaining;
        }
        delivery->nextMs = 0;
        snprintf(delivery->id, sizeof delivery->id, "%s", record->id);
        snprintf(delivery->notify.key, sizeof delivery->notify.key, "%s", record->key);
        snprintf(delivery->notify.message, sizeof delivery->notify.message, "%s", record->message);
        delivery->notify.active = 1;
    }
    free(file);
    return spool_save_locked(service);
}

static uint64_t now_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t) time.tv_sec * 1000 + (uint64_t) time.tv_nsec / 1000000;
}

static uint64_t epoch_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return (uint64_t) time.tv_sec * 1000 + (uint64_t) time.tv_nsec / 1000000;
}

static void wait_locked(AlarmService* service, int milliseconds) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    time.tv_sec += milliseconds / 1000;
    time.tv_nsec += (long) (milliseconds % 1000) * 1000000;
    if (time.tv_nsec >= 1000000000) {
        time.tv_sec++;
        time.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&service->wake, &service->mu, &time);
}

static int listener_pause(AlarmService* service, int milliseconds) {
    uint64_t until = now_ms() + (uint64_t) milliseconds;
    pthread_mutex_lock(&service->mu);
    while (!service->quiescing && now_ms() < until)
        wait_locked(service, (int) (until - now_ms()));
    int stop = service->quiescing;
    pthread_mutex_unlock(&service->mu);
    return stop;
}

static int has_actionable_delivery_locked(const AlarmService* service) {
    for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++)
        if (service->deliveries[i].used && !service->deliveries[i].deadLetter)
            return 1;
    return 0;
}

static int listener_wait_until_monitorable(AlarmService* service) {
    pthread_mutex_lock(&service->mu);
    while (!service->quiescing && has_actionable_delivery_locked(service))
        pthread_cond_wait(&service->wake, &service->mu);
    int stop = service->quiescing;
    pthread_mutex_unlock(&service->mu);
    return stop;
}

static int has_seen(const AlarmService* service, const char* key) {
    for (size_t i = 0; i < service->seenCount; i++)
        if (!strcmp(service->seen[i], key))
            return 1;
    return 0;
}

static int is_pending(const AlarmService* service, const char* key) {
    for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++)
        if (service->deliveries[i].used && !strcmp(service->deliveries[i].notify.key, key))
            return 1;
    return 0;
}

static void remember(AlarmService* service, const char* key) {
    size_t index;
    if (service->seenCount < ALARM_SEEN_CAPACITY) {
        index = service->seenCount++;
    } else {
        index = service->seenNext;
        service->seenNext = (service->seenNext + 1) % ALARM_SEEN_CAPACITY;
    }
    snprintf(service->seen[index], sizeof service->seen[index], "%s", key);
}

static void release_delivery(Delivery* delivery) {
    memset(delivery, 0, sizeof *delivery);
}

/* Commit HANDLED in two durable phases.  The first snapshot is a replay
 * barrier: after it reaches disk, restart recovery knows that the Skill must
 * not run again.  The second snapshot moves the key to the ACK history and
 * frees the delivery slot.  If either write fails, the handled delivery stays
 * in memory and the dispatcher retries persistence only. */
static int finalize_handled_locked(AlarmService* service, Delivery* delivery) {
    if (!delivery->handled) {
        delivery->handled = 1;
        delivery->nextMs = now_ms() + (uint64_t) service->cfg.retryDelayMs;
        if (spool_save_locked(service))
            return -1;
    }
    Delivery saved = *delivery;
    if (!has_seen(service, delivery->notify.key))
        remember(service, delivery->notify.key);
    release_delivery(delivery);
    if (spool_save_locked(service)) {
        *delivery = saved;
        delivery->handled = 1;
        delivery->nextMs = now_ms() + (uint64_t) service->cfg.retryDelayMs;
        return -1;
    }
    pthread_cond_broadcast(&service->wake);
    return 0;
}

static void dead_letter_locked(AlarmService* service, Delivery* delivery, const char* cause) {
    delivery->deadLetter = 1;
    LogPrint("[alarm] delivery dead-letter id=%s cause=%s", delivery->id, cause);
    if (spool_save_locked(service))
        LogPrint("[alarm] ERROR dead-letter persistence failed id=%s", delivery->id);
    pthread_cond_broadcast(&service->wake);
}

static int accept_single(AlarmService* service, const AlarmNotify* notify) {
    if (!notify->active || !notify->message[0])
        return 0;
    pthread_mutex_lock(&service->mu);
    if (service->quiescing || has_seen(service, notify->key) || is_pending(service, notify->key)) {
        pthread_mutex_unlock(&service->mu);
        return 0;
    }
    Delivery* delivery = NULL;
    for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++) {
        if (!service->deliveries[i].used) {
            delivery = &service->deliveries[i];
            break;
        }
    }
    if (!delivery) {
        LogPrint("[alarm] forwarding queue full; occurrence will be retried by monitoring");
        pthread_mutex_unlock(&service->mu);
        return 0;
    }
    memset(delivery, 0, sizeof *delivery);
    delivery->used = 1;
    delivery->notify = *notify;
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    snprintf(delivery->id,
             sizeof delivery->id,
             "alarm-%lx-%lx-%llu",
             (unsigned long) time.tv_sec,
             (unsigned long) time.tv_nsec,
             (unsigned long long) ++service->sequence);
    LogPrint("[alarm] occurrence queued id=%s key=%.12s", delivery->id, notify->key);
    if (spool_save_locked(service))
        LogPrint("[alarm] ERROR durable spool write failed id=%s", delivery->id);
    pthread_cond_broadcast(&service->wake);
    pthread_mutex_unlock(&service->mu);
    return 1;
}

static int accept_notify(AlarmService* service, const AlarmNotify* notify) {
    size_t length = strlen(notify->raw);
    LjToken* tokens = calloc(length + 1, sizeof *tokens);
    if (!tokens)
        return 0;
    LjParser parser;
    LjInit(&parser);
    int count = LjParse(&parser, notify->raw, length, tokens, (unsigned) length + 1);
    int list = count > 0 ? LjObjectGet(notify->raw, tokens, count, 0, "almlist") : -1;
    if (list < 0 || tokens[list].type != LJ_ARRAY) {
        free(tokens);
        return 0;
    }
    for (int i = 0; i < tokens[list].size; i++) {
        int item = LjArrayGet(tokens, count, list, i);
        AlarmNotify occurrence;
        char row[ALARM_MAX_TEXT_LEN];
        if (item < 0 || tokens[item].type != LJ_OBJECT ||
            snprintf(row,
                     sizeof row,
                     "%.*s",
                     tokens[item].end - tokens[item].start,
                     notify->raw + tokens[item].start) >= (int) sizeof row ||
            AlarmOccurrenceParse(row, &occurrence)) {
            LogPrint("[alarm] active snapshot rejected: missing or invalid occurrence fields");
            free(tokens);
            return 0;
        }
    }
    for (int i = 0; i < tokens[list].size; i++) {
        int item = LjArrayGet(tokens, count, list, i);
        AlarmNotify occurrence;
        char row[ALARM_MAX_TEXT_LEN];
        snprintf(row,
                 sizeof row,
                 "%.*s",
                 tokens[item].end - tokens[item].start,
                 notify->raw + tokens[item].start);
        AlarmOccurrenceParse(row, &occurrence);
        if (accept_single(service, &occurrence)) {
            free(tokens);
            return 1;
        }
    }
    free(tokens);
    return 0;
}

static void* listen_main(void* argument) {
    AlarmService* service = argument;
    AspAlarmClient* client = service->cfg.client;
    int beginNeeded = 1;
    int forceSnapshot = 1;
    int backoff = 1000;
    while (!listener_pause(service, 0)) {
        if (listener_wait_until_monitorable(service))
            break;
        if (beginNeeded) {
            int beginResult = forceSnapshot
                                  ? client->vtable->beginSnapshot(client, ++service->sequence)
                                  : client->vtable->begin(client, ++service->sequence);
            if (beginResult) {
                if (listener_pause(service, backoff))
                    break;
                backoff = backoff < 30000 ? backoff * 2 : 60000;
                continue;
            }
            beginNeeded = 0;
        }
        AlarmNotify notify;
        int result = client->vtable->poll(client, (uint32_t) service->cfg.ioTickMs, &notify);
        if (result == ALARM_POLL_NOTIFY) {
            int accepted = accept_notify(service, &notify);
            beginNeeded = 1;
            backoff = 1000;
            forceSnapshot = accepted;
            if (!accepted)
                listener_pause(service,
                               service->cfg.mode == ALARM_MODE_POLLING
                                   ? service->cfg.pollIntervalMs
                                   : 50);
        } else if (result == ALARM_POLL_RESPONSE_END) {
            beginNeeded = 1;
            backoff = 1000;
            listener_pause(service, 50);
        } else if (result == ALARM_POLL_NETWORK_ERROR || result == ALARM_POLL_AUTH_EXPIRED) {
            LogPrint("[alarm] reconnect cause=%d backoff_ms=%d", result, backoff);
            beginNeeded = 1;
            if (listener_pause(service, backoff))
                break;
            backoff = backoff < 30000 ? backoff * 2 : 60000;
        }
    }
    return NULL;
}

static void* dispatch_main(void* argument) {
    AlarmService* service = argument;
    pthread_mutex_lock(&service->mu);
    while (!service->stopping) {
        for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++) {
            Delivery* delivery = &service->deliveries[i];
            if (!delivery->used || delivery->deadLetter || service->quiescing ||
                now_ms() < delivery->nextMs)
                continue;
            if (delivery->handled) {
                if (finalize_handled_locked(service, delivery))
                    LogPrint("[alarm] ERROR handled ACK persistence retry id=%s", delivery->id);
                else
                    LogPrint("[alarm] handled ACK finalized without Skill replay");
                continue;
            }
            if (delivery->attempts >= 1 + ALARM_FORWARD_RETRIES) {
                LogPrint("[alarm] forwarding failed id=%s retries_exhausted", delivery->id);
                dead_letter_locked(service, delivery, "retries_exhausted");
                continue;
            }
            /* Never transfer responsibility to the scheduler until the current
             * delivery state is known to be durable. A transient storage error
             * consumes no Agent attempt and is retried on the bounded timer. */
            if (spool_save_locked(service)) {
                LogPrint("[alarm] ERROR durable spool unavailable id=%s", delivery->id);
                delivery->nextMs = now_ms() + (uint64_t) service->cfg.retryDelayMs;
                continue;
            }
            int firstDispatch = !delivery->firstDispatchEpochMs;
            if (firstDispatch) {
                delivery->firstDispatchEpochMs = epoch_ms();
                delivery->holdDeadlineMs = now_ms() + ALARM_AGENT_DEADLINE_MS;
                if (spool_save_locked(service)) {
                    LogPrint("[alarm] ERROR first-dispatch deadline persistence failed id=%s",
                             delivery->id);
                    delivery->firstDispatchEpochMs = 0;
                    delivery->holdDeadlineMs = 0;
                    delivery->nextMs = now_ms() + (uint64_t) service->cfg.retryDelayMs;
                    continue;
                }
            }
            uint64_t current = now_ms();
            if (!delivery->holdDeadlineMs)
                delivery->holdDeadlineMs = current + ALARM_AGENT_DEADLINE_MS;
            if (current >= delivery->holdDeadlineMs) {
                dead_letter_locked(service, delivery, "agent_deadline");
                continue;
            }
            delivery->attempts++;
            IngressOptions options = {.type = LITE_MSG_CHAT,
                                      .priority = LITE_PRIORITY_HIGH,
                                      .replyMode = LITE_REPLY_SYNC,
                                      .source = "alarm:asp",
                                      .userId = "alarm",
                                      .sessionId = delivery->id};
            options.deadlineMs = (int64_t) delivery->holdDeadlineMs;
            IngressResult receipt;
            int length = (int) strlen(delivery->notify.message);
            int result =
                IngressSubmit("alarm:asp", delivery->notify.message, length, &options, &receipt);
            if (!result) {
                snprintf(service->activeRequestId,
                         sizeof service->activeRequestId,
                         "%s",
                         receipt.requestId);
                spool_save_locked(service);
                pthread_mutex_unlock(&service->mu);
                char response[4096];
                LiteCompletionStatus completion = LITE_COMPLETION_RETRYABLE;
                uint64_t beforeWait = now_ms();
                uint64_t remaining = delivery->holdDeadlineMs > beforeWait
                                         ? delivery->holdDeadlineMs - beforeWait
                                         : 0;
                int handled = DispatchResponseEx(receipt.requestId,
                                                 LITE_REPLY_SYNC,
                                                 remaining > INT32_MAX ? INT32_MAX : (int) remaining,
                                                 response,
                                                 sizeof response,
                                                 &completion);
                int deadlineExpired = now_ms() >= delivery->holdDeadlineMs;
                if (handled && deadlineExpired)
                    RequestCancel(receipt.requestId);
                pthread_mutex_lock(&service->mu);
                service->activeRequestId[0] = 0;
                if (deadlineExpired && (handled || completion == LITE_COMPLETION_RETRYABLE ||
                                        completion == LITE_COMPLETION_CANCELLED)) {
                    LogPrint("[alarm] Agent hold deadline reached id=%s request_id=%s "
                             "hold_timeout_ms=%d first_dispatch_epoch_ms=%llu",
                             delivery->id,
                             receipt.requestId,
                             ALARM_AGENT_DEADLINE_MS,
                             (unsigned long long) delivery->firstDispatchEpochMs);
                    dead_letter_locked(service, delivery, "agent_deadline");
                } else if (!handled && completion == LITE_COMPLETION_HANDLED) {
                    char deliveryId[sizeof delivery->id];
                    snprintf(deliveryId, sizeof deliveryId, "%s", delivery->id);
                    if (finalize_handled_locked(service, delivery))
                        LogPrint("[alarm] ERROR handled but ACK persistence pending id=%s request_id=%s",
                                 deliveryId,
                                 receipt.requestId);
                    else
                        LogPrint("[alarm] occurrence handled id=%s request_id=%s",
                                 deliveryId,
                                 receipt.requestId);
                } else if (!handled && completion == LITE_COMPLETION_PERMANENT) {
                    LogPrint("[alarm] permanent handling failure id=%s", delivery->id);
                    dead_letter_locked(service, delivery, "permanent");
                } else {
                    LogPrint("[alarm] handling incomplete id=%s attempt=%d",
                             delivery->id,
                             delivery->attempts);
                    delivery->nextMs = now_ms() + (uint64_t) service->cfg.retryDelayMs;
                    spool_save_locked(service);
                }
            } else {
                if (firstDispatch) {
                    delivery->firstDispatchEpochMs = 0;
                    delivery->holdDeadlineMs = 0;
                }
                LogPrint("[alarm] forwarding attempt failed id=%s attempt=%d",
                         delivery->id,
                         delivery->attempts);
                delivery->nextMs = now_ms() + (uint64_t) service->cfg.retryDelayMs;
                spool_save_locked(service);
            }
        }
        uint64_t current = now_ms();
        uint64_t nearest = UINT64_MAX;
        for (int i = 0; i < ALARM_QUEUE_CAPACITY; i++) {
            Delivery* delivery = &service->deliveries[i];
            if (delivery->used && !delivery->deadLetter && !service->quiescing &&
                delivery->nextMs < nearest)
                nearest = delivery->nextMs;
        }
        if (nearest == UINT64_MAX)
            pthread_cond_wait(&service->wake, &service->mu);
        else if (nearest > current) {
            uint64_t delay = nearest - current;
            wait_locked(service, delay > INT32_MAX ? INT32_MAX : (int) delay);
        }
    }
    pthread_mutex_unlock(&service->mu);
    return NULL;
}

int AlarmServiceInit(AlarmService** out, const AlarmConfig* config) {
    if (!out || !config || !config->client || !config->client->vtable ||
        !config->client->vtable->begin || !config->client->vtable->beginSnapshot ||
        !config->client->vtable->poll ||
        !config->client->vtable->cancel)
        return -1;
    *out = NULL;
    AlarmService* service = calloc(1, sizeof *service);
    if (!service)
        return -1;
    service->cfg = *config;
    if (service->cfg.ioTickMs <= 0)
        service->cfg.ioTickMs = 100;
    if (service->cfg.pollIntervalMs <= 0)
        service->cfg.pollIntervalMs = 5000;
    if (service->cfg.retryDelayMs <= 0)
        service->cfg.retryDelayMs = 1000;
    pthread_mutex_init(&service->mu, NULL);
    pthread_condattr_t attributes;
    pthread_condattr_init(&attributes);
    pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    pthread_cond_init(&service->wake, &attributes);
    pthread_condattr_destroy(&attributes);
    if (service->cfg.spoolDirectory[0]) {
        if (ensure_directory(service->cfg.spoolDirectory) ||
            snprintf(service->spoolPath,
                     sizeof service->spoolPath,
                     "%s/alarm_spool.bin",
                     service->cfg.spoolDirectory) >= (int) sizeof service->spoolPath ||
            spool_load(service)) {
            pthread_cond_destroy(&service->wake);
            pthread_mutex_destroy(&service->mu);
            free(service);
            return -1;
        }
    }
    *out = service;
    return 0;
}

int AlarmServiceStart(AlarmService* service) {
    if (!service)
        return -1;
    if (service->listenerRunning || service->dispatcherRunning)
        return 0;
    service->quiescing = 0;
    service->stopping = 0;
    IngressInit();
    if (pthread_create(&service->dispatcher, NULL, dispatch_main, service))
        return -1;
    service->dispatcherRunning = 1;
    if (pthread_create(&service->listener, NULL, listen_main, service)) {
        AlarmServiceStop(service, 5000);
        return -1;
    }
    service->listenerRunning = 1;
    return 0;
}

static int join_thread(pthread_t thread, int* running, uint32_t timeout) {
    if (!*running)
        return 0;
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += timeout / 1000;
    end.tv_nsec += (long) (timeout % 1000) * 1000000;
    if (end.tv_nsec >= 1000000000) {
        end.tv_sec++;
        end.tv_nsec -= 1000000000;
    }
    int result = pthread_timedjoin_np(thread, NULL, &end);
    if (!result)
        *running = 0;
    return result;
}

int AlarmServiceQuiesce(AlarmService* service, uint32_t timeout) {
    if (!service)
        return 0;
    pthread_mutex_lock(&service->mu);
    service->quiescing = 1;
    char activeRequestId[64];
    snprintf(activeRequestId, sizeof activeRequestId, "%s", service->activeRequestId);
    pthread_cond_broadcast(&service->wake);
    pthread_mutex_unlock(&service->mu);
    if (activeRequestId[0])
        RequestCancel(activeRequestId);
    service->cfg.client->vtable->cancel(service->cfg.client);
    return join_thread(service->listener, &service->listenerRunning, timeout);
}

int AlarmServiceStop(AlarmService* service, uint32_t timeout) {
    if (!service)
        return 0;
    int result = AlarmServiceQuiesce(service, timeout);
    if (result)
        return result;
    pthread_mutex_lock(&service->mu);
    service->stopping = 1;
    pthread_cond_broadcast(&service->wake);
    pthread_mutex_unlock(&service->mu);
    return join_thread(service->dispatcher, &service->dispatcherRunning, timeout);
}

void AlarmServiceDestroy(AlarmService* service) {
    if (!service || service->listenerRunning || service->dispatcherRunning)
        return;
    pthread_cond_destroy(&service->wake);
    pthread_mutex_destroy(&service->mu);
    memset(service->cfg.password, 0, sizeof service->cfg.password);
    free(service);
}
