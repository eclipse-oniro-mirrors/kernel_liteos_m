/*
 * voice_service.c - 语音唤醒 + 阿里云 Qwen 实时对话服务实现
 *
 * 结构(与其它 service 一致): 配置在 voice_service.h -> 语义接口 -> Cmd 入口。
 * 常驻后台线程 + 启停状态机:
 *   - 首次 start: 创建常驻线程 -> 初始化采集/播放/唤醒词(失败则报错待命)
 *   - 运行中:     VoiceDialogLoop 阻塞跑 唤醒<->对话(细节在 voice_dialog.c)
 *   - stop:       VoiceDialogRequestStop -> 循环退出 -> 线程回待命(设备复用)
 *   - 再 start:   直接复用初始化(不重复 HI_MPI/TFLM 初始化, 避免重入风险)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "voice_service.h"
#include "voice_dialog.h"
#include "wake_capture.h"
#include "wake_playback.h"
#include "wake_word.h"

/* ---- 状态 ---- */
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_tid;
static volatile int g_threadOn = 0;  /* 常驻线程已创建 */
static volatile int g_wantOn = 0;    /* 1=要求循环运行(g_mtx 保护) */
static volatile int g_loopOn = 0;    /* 循环当前是否在跑(status 展示用) */
static volatile int g_inited = 0;    /* 采集/播放/唤醒词已初始化 */

/* 初始化采集+播放+唤醒词(一次性; 全部成功返回 0)。
 * 开机早期音频 HDI 可能未就绪: 失败打印降频(每 12 次=1 分钟打一条),
 * 重试由 VoiceThreadMain 做(各阶段可安全重入) */
static int VoiceInitOnce(void)
{
    static int sFail = 0;
    int loud = (sFail % 12 == 0);

    if (WakeCaptureInit() != 0) {
        sFail++;
        if (loud) {
            printf("[voice] 采集初始化失败(开机早期未就绪/被 my_wake 占用?), 自动重试中\r\n");
        }
        return -1;
    }
    if (WakePlaybackInit() != 0) {
        if (loud) {
            printf("[voice] 播放初始化失败, 自动重试中\r\n");
        }
        sFail++;
        WakeCaptureDeinit();
        return -1;
    }
    if (wake_word_init() != 0) {
        if (loud) {
            printf("[voice] 唤醒词初始化失败, 自动重试中\r\n");
        }
        sFail++;
        WakePlaybackDeinit();
        WakeCaptureDeinit();
        return -1;
    }
    printf("[voice] 初始化完成: 16k 采集 + 24k->16k 播放 + 唤醒词(%s)\r\n",
           VOICE_CFG_WAKE_WORD);
    return 0;
}

/* 常驻线程: 等 start 信号 -> (首次)初始化 -> 跑 唤醒<->对话 循环 -> 待命 */
static void *VoiceThreadMain(void *arg)
{
    (void)arg;
    for (;;) {
        (void)pthread_mutex_lock(&g_mtx);
        while (!g_wantOn) {
            (void)pthread_cond_wait(&g_cond, &g_mtx);
        }
        (void)pthread_mutex_unlock(&g_mtx);

        if (!g_inited) {
            /* 初始化(含自动重试): 开机早期音频设备未就绪属正常,
             * 每 5 秒重试直到成功; 期间收到 voice stop 则放弃回待命 */
            int retry = 0;

            for (;;) {
                (void)pthread_mutex_lock(&g_mtx);
                int want = g_wantOn;
                (void)pthread_mutex_unlock(&g_mtx);
                if (!want) {
                    break;
                }
                if (VoiceInitOnce() == 0) {
                    g_inited = 1;
                    break;
                }
                retry++;
                if (retry % 12 == 1) {
                    printf("[voice] 初始化重试中(已试 %d 次, 每 5s 一次)\r\n", retry);
                }
                sleep(5);
            }
            if (!g_inited) {
                continue;   /* 被 stop 打断初始化: 回挂起 */
            }
        }

        /* 初始化耗时数秒, 期间到达的 voice stop 在这里生效 */
        (void)pthread_mutex_lock(&g_mtx);
        if (!g_wantOn) {
            (void)pthread_mutex_unlock(&g_mtx);
            continue;
        }
        (void)pthread_mutex_unlock(&g_mtx);

        g_loopOn = 1;
        VoiceDialogArm();
        (void)VoiceDialogLoop(VOICE_CFG_GATE_DBFS, VOICE_CFG_MIC_VOL);
        g_loopOn = 0;

        /* stop 后立即 start 的场景: wantOn 已被置 1, 直接再进循环 */
        (void)pthread_mutex_lock(&g_mtx);
        if (g_wantOn) {
            (void)pthread_mutex_unlock(&g_mtx);
            continue;
        }
        (void)pthread_mutex_unlock(&g_mtx);
        printf("[voice] 循环已停, 待命(voice start 重启)\r\n");
    }
    return NULL;
}

int VoiceSvcStart(void)
{
    pthread_attr_t attr;

    (void)pthread_mutex_lock(&g_mtx);
    if (g_wantOn) {
        (void)pthread_mutex_unlock(&g_mtx);
        printf("[voice] 已在运行\r\n");
        return 0;
    }
    g_wantOn = 1;
    if (!g_threadOn) {
        (void)pthread_attr_init(&attr);
        (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        /* agent 段(LiteCrab agent 内核)栈帧极大(实测 ~612KB):
         *   run_round    kernel.c:588      llmToolCalls[65536]      64KB
         *   AgentTraceLogLlm observability.c:419  b[98304]         96KB(+clipped 4KB)
         * 两处都在原始树不可改, 只能加栈。512KB 实测仍溢出(data abort),
         * 给 2MB(LiteOS-A 线程栈懒分配, RAM 只按实际触碰页计) */
        (void)pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
        if (pthread_create(&g_tid, &attr, VoiceThreadMain, NULL) != 0) {
            (void)pthread_attr_destroy(&attr);
            g_wantOn = 0;
            (void)pthread_mutex_unlock(&g_mtx);
            printf("[voice] 线程创建失败\r\n");
            return -1;
        }
        (void)pthread_attr_destroy(&attr);
        g_threadOn = 1;
    }
    (void)pthread_cond_signal(&g_cond);
    (void)pthread_mutex_unlock(&g_mtx);
    return 0;
}

int VoiceSvcStop(void)
{
    if (!g_wantOn && !g_loopOn) {
        printf("[voice] 未在运行\r\n");
        return -1;
    }
    (void)pthread_mutex_lock(&g_mtx);
    g_wantOn = 0;
    (void)pthread_mutex_unlock(&g_mtx);
    VoiceDialogRequestStop();  /* 循环收尾后线程回待命(设备复用) */
    return 0;
}

int VoiceSvcIsRunning(void)
{
    return g_loopOn;
}

/* ---- shell 指令 ---- */

int VoiceCmdVoice(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
        (void)VoiceSvcStart();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        (void)VoiceSvcStop();
        return 0;
    }
    printf("[voice] %s (喊 \"%s\" 对话; 云端指令->表情/动作见 [agent] 日志)\r\n",
           VoiceSvcIsRunning() ? "运行中" : "待命/未启动", VOICE_CFG_WAKE_WORD);
    printf("[voice] 用法: voice start | voice stop | voice status\r\n");
    return 0;
}

/* vol [0~100]: 设置/查看播放音量(软件缩放, 对话播放立即生效) */
int VoiceCmdVol(int argc, char **argv)
{
    if (argc >= 2) {
        int v = atoi(argv[1]);

        v = VoiceDialogSetVol(v);
        printf("[voice] 播放音量 -> %d%% (0=静音, 100=最大)\r\n", v);
    } else {
        printf("[voice] 当前播放音量 %d%% (用 vol <0~100> 修改)\r\n",
               VoiceDialogGetVol());
    }
    return 0;
}
