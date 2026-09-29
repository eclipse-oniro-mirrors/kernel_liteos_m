/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/* rpmsg-echo service: echoes every received message back verbatim.
 * Used by the master-side demo (rpmsg_demo -e rpmsg-echo) as the
 * standard round-trip verification endpoint (openamp echo pattern).
 */
#include <mica/mica.h>
#include <mica/service.h>
#include <mica/platform/log.h>
#include <mica/platform/delay.h>
#include "mica_service_internal.h"

#define RPMSG_ECHO_EPT_NAME "rpmsg-echo"

static struct rpmsg_endpoint g_echo_ept;

static int rx_echo_callback(struct rpmsg_endpoint *ept, void *data,
                            size_t len, uint32_t src, void *priv)
{
    (void)src;
    (void)priv;
    /* echo back verbatim */
    return rpmsg_send(ept, data, len);
}

int mica_echo_is_ready(void)
{
    return is_rpmsg_ept_ready(&g_echo_ept);
}

void *echo_service_thread(void *arg)
{
    struct rpmsg_device *rpdev = mica_get_rpdev();
    int ret;

    (void)arg;
    if (!rpdev) {
        mica_log("ECHO: rpmsg device not initialized\n");
        goto err_init;
    }

    ret = rpmsg_create_ept(&g_echo_ept, rpdev, RPMSG_ECHO_EPT_NAME,
                           RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                           rx_echo_callback, NULL);
    if (ret != 0) {
        mica_log("ECHO: failed to create endpoint: %d\n", ret);
        goto err_init;
    }

    /* all rx processing happens in the callback context (receiver
     * thread); this thread just keeps the service alive */
    for (;;) {
        mica_delay_tick(1000);
    }

err_init:
    pthread_exit(NULL);
}

int mica_echo_init_service(pthread_attr_t *attr)
{
    pthread_t thread;

    return pthread_create(&thread, attr, echo_service_thread, NULL);
}
