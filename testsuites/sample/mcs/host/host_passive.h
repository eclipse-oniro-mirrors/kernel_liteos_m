/*
 * host_passive.h — mcs host 被动服务（np socket 代理 / pfs 代理 / echo / E2E 配合）
 *
 * 生命周期模型（新架构 host_server 三线程）：
 *   host_passive_start()  常驻线程：请求 worker + UDP/TCP echo + connect-back。
 *                         进程存活期间一直在（幂等，重复调用 no-op）。
 *   host_passive_attach() rpmsg 端点挂载：测试线程每轮 mica_start 后调用，
 *                         注册 NS match（rpmsg-rpc / rpmsg-tty / rpmsg-umt），
 *                         从核 announce 到达即建端点。
 *   host_passive_detach() 端点解挂：slave_stop_delete 前调用（destroy ept +
 *                         清标志，防残留帧派发/响应到已销毁 rdev）。
 *   host_passive_tx_lock() rpmsg 发送串行化锁——host_slave 的 RUN/STOP 帧
 *                         与 worker 响应帧共用同一 vring，必须互斥（openamp
 *                         无内部锁，并发 send 竞态损坏 virtqueue）。
 */
#ifndef HOST_PASSIVE_H
#define HOST_PASSIVE_H

#include <pthread.h>
#include <openamp/rpmsg.h>
#include <mica/mica.h>

int host_passive_start(void);
int host_passive_attach(struct mica_client *c);
void host_passive_detach(void);
pthread_mutex_t *host_passive_tx_lock(void);

#endif /* HOST_PASSIVE_H */
