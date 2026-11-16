/*
 * host_slave.h — host 侧从核测试调度（2026-11-09 新架构）
 * mica 客户端生命周期（create/start/stop/delete）+ rpmsg-mcs-test
 * 协议命令帧收发 + 单轮调度执行（等 ACK 受理 + 等 DONE 完成）。
 */
#ifndef HOST_SLAVE_H
#define HOST_SLAVE_H

#include <stdint.h>
#include <semaphore.h>
#include <openamp/rpmsg.h>
#include <mica/mica.h>
#include "mcs_test_proto.h"

struct slave_ctx {
    struct mica_client *cli;
    struct rpmsg_endpoint ept;
    volatile int ept_ok;
    volatile int started;
    sem_t ept_ready;
    sem_t ack_ready;
    sem_t done_ready;
    struct mcs_test_msg ack;   /* 最近 ACK */
    struct mcs_test_msg done;  /* 最近 DONE */
    uint32_t last_seq;
};

/* 生命周期（测试线程五步：create->start->回调->stop->delete） */
struct slave_ctx *slave_create_start(const char *fw_path, int wait_sec);
void slave_stop_delete(struct slave_ctx *s);   /* 完成与被杀路径统一走这里 */

/* 命令：发帧并等受理 ACK；返回 0=受理，<0=错误码/超时 */
int slave_cmd_wait_ack(struct slave_ctx *s, struct mcs_test_msg *msg, int timeout_ms);

/* 等 DONE（RUN_* 执行完成汇总）；timeout_ms<0 表示无限等待 */
int slave_wait_done(struct slave_ctx *s, struct mcs_test_msg *out, int timeout_ms);

/* 发 STOP 帧（从核在用例间停）；异步不等 DONE */
int slave_send_stop(struct slave_ctx *s);

#endif /* HOST_SLAVE_H */
