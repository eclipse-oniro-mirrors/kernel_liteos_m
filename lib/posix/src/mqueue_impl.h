/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MQUEUE_IMPL_H
#define _MQUEUE_IMPL_H

#define _GNU_SOURCE

#include <mqueue.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <securec.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/select.h>
#include "los_queue.h"
#include "los_queue_pri.h"
#include "los_memory.h"
#include "los_task.h"
#include "los_compiler.h"

/**
 * @ingroup mqueue
 * Maximum number of messages in a message queue
 */
#define MQ_MAX_MSG_NUM    16

/**
 * @ingroup mqueue
 * Maximum size of a single message in a message queue
 */
#define MQ_MAX_MSG_LEN    64

#include "map_error.h"
/* CONSTANTS */

#define MQ_USE_MAGIC  0x89abcdef
#define MQ_PRIO_MAX 1

#ifndef FNONBLOCK
#define FNONBLOCK   O_NONBLOCK
#endif

#define STATIC static
#define INLINE inline

typedef union send_receive_t {
    unsigned oth : 3;
    unsigned grp : 6;
    unsigned usr : 9;
    short data;
} mode_s;

/* TYPE DEFINITIONS */
struct mqarray {
    UINT32 mq_id : 31;
    UINT32 unlinkflag : 1;
    char *mq_name;
    UINT32 unlink_ref;
    mode_s mode_data; /* mode data of mqueue */
    uid_t euid; /* euid of mqueue */
    gid_t egid; /* egid of mqueue */
    fd_set mq_fdset; /* mqueue sysFd bit map */
    LosQueueCB *mqcb;
    struct mqpersonal *mq_personal;
};

struct mqpersonal {
    struct mqarray *mq_posixdes;
    struct mqpersonal *mq_next;
    int mq_flags;
    int mq_mode;  /* Mode of mqueue */
    UINT32 mq_status;
    UINT32 mq_refcount;
};

#endif
