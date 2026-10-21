/*
 * servo_service.c - PCA9685 舵机 / Otto 动作服务实现
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "servo_service.h"
#include "app_console.h"
#include "wifi_service.h"
#include "pca9685.h"
#include "otto_pca.h"

int ServoEnsureInit(void)
{
    uint8_t bus, addr;
    int32_t ready;

    PcaGetStatus(&bus, &addr, &ready);
    if (ready) {
        return 0;
    }
    if (PcaInit() != HDF_SUCCESS) {
        printf("[pca] 初始化失败(没接/没上电? 先 scan 排查接线)\r\n");
        return -1;
    }
    return 0;
}

int ServoScanBus(int bus)
{
    return PcaScanBus((uint8_t)bus);
}

int ServoInit(uint8_t bus, uint8_t addr)
{
    return (PcaInitEx(bus, addr) == HDF_SUCCESS) ? 0 : -1;
}

int ServoSetAngle(int ch, int deg)
{
    return (PcaServoAngle((uint8_t)ch, (uint32_t)deg) == HDF_SUCCESS) ? 0 : -1;
}

int ServoSetUs(int ch, int us)
{
    return (PcaServoUs((uint8_t)ch, (uint32_t)us) == HDF_SUCCESS) ? 0 : -1;
}

int ServoAllOff(void)
{
    return (PcaAllChannelsOff() == HDF_SUCCESS) ? 0 : -1;
}

/* ---------------- shell 指令 ---------------- */

int ServoCmdScan(int argc, char **argv)
{
    int32_t bus = SERVO_CFG_BUS;
    int32_t found;

    if (argc >= 2 && ShellParseInt(argv[1], &bus) != 0) {
        printf("[pca] 用法: scan [bus]  (默认 %d)\r\n", SERVO_CFG_BUS);
        return 0;
    }
    printf("[pca] 扫描 I2C-%d (0x03~0x77)...\r\n", (int)bus);
    found = ServoScanBus((int)bus);
    if (found < 0) {
        printf("[pca] 总线打不开\r\n");
    } else {
        printf("[pca] 共 %d 个器件有应答%s\r\n", (int)found,
               (found == 0) ? "(接线/共地/上电检查)" : "");
    }
    return 0;
}

int ServoCmdInit(int argc, char **argv)
{
    uint8_t bus = SERVO_CFG_BUS;
    uint8_t addr = SERVO_CFG_ADDR;
    int32_t x;

    if (argc >= 2 && ShellParseInt(argv[1], &x) == 0) {
        if (x < 0 || x > 0xFF) {
            printf("[pca] bus 非法\r\n");
            return 0;
        }
        bus = (uint8_t)x;
    }
    if (argc >= 3 && ShellParseInt(argv[2], &x) == 0) {
        if (x < 0 || x > 0x7F) {
            printf("[pca] addr 非法(7位, 0x40=64)\r\n");
            return 0;
        }
        addr = (uint8_t)x;
    }
    if (ServoInit(bus, addr) == 0) {
        printf("[pca] 初始化 OK: bus=%u addr=0x%02x\r\n", bus, addr);
    } else {
        printf("[pca] 初始化 FAIL (先 scan 排查)\r\n");
    }
    return 0;
}

int ServoCmdStatus(int argc, char **argv)
{
    uint8_t bus, addr;
    int32_t ready;

    (void)argc;
    (void)argv;
    PcaGetStatus(&bus, &addr, &ready);
    printf("[pca] bus=%u addr=0x%02x ready=%d (本进程状态)\r\n", bus, addr, ready);
    return 0;
}

int ServoCmdAngle(int argc, char **argv)
{
    int32_t ch, deg;

    if (argc < 3 || ShellParseInt(argv[1], &ch) != 0 ||
        ShellParseInt(argv[2], &deg) != 0) {
        printf("[pca] 用法: angle <通道0~15> <角度0~180>\r\n");
        return 0;
    }
    if (ServoEnsureInit() != 0) {
        return 0;
    }
    if (ServoSetAngle((int)ch, (int)deg) == 0) {
        printf("[pca] CH%d -> %d 度\r\n", (int)ch, (int)deg);
    } else {
        printf("[pca] FAIL (ch 0~15 / deg 0~180)\r\n");
    }
    return 0;
}

int ServoCmdUs(int argc, char **argv)
{
    int32_t ch, us;

    if (argc < 3 || ShellParseInt(argv[1], &ch) != 0 ||
        ShellParseInt(argv[2], &us) != 0) {
        printf("[pca] 用法: us <通道0~15> <脉宽500~2500>\r\n");
        return 0;
    }
    if (ServoEnsureInit() != 0) {
        return 0;
    }
    if (ServoSetUs((int)ch, (int)us) == 0) {
        printf("[pca] CH%d -> %d us\r\n", (int)ch, (int)us);
    } else {
        printf("[pca] FAIL (ch 0~15 / us 500~2500)\r\n");
    }
    return 0;
}

/* 动作参数收集: argv[1]=动作名, argv[2..] 最多 4 个参数 */
static int servo_action(const char *name, int argc, char **argv, int post)
{
    int32_t par[4] = {0};
    int32_t npar = 0;
    int32_t i;
    int32_t ret;

    if (ServoEnsureInit() != 0) {
        return 0;
    }
    for (i = 2; i < argc; i++) {
        if (npar >= 4) {
            printf("[pca] 参数最多 4 个\r\n");
            return 0;
        }
        if (ShellParseInt(argv[i], &par[npar]) != 0) {
            printf("[pca] 参数不是数字: %s\r\n", argv[i]);
            return 0;
        }
        npar++;
    }
    printf("[pca] %s %s(%d 参数)...\r\n", post ? "post" : "run", name, npar);
    WifiSvcMonPause();
    ret = post ? OttoPcaPost(name, par, npar) : OttoPcaRun(name, par, npar);
    if (!post) {
        WifiSvcMonResume();
    }
    if (post) {
        printf("[pca] 已投后台任务 (stop 可中止/归位, off 再断力)\r\n");
    } else if (ret == 0) {
        printf("[pca] %s 完成\r\n", name);
    } else {
        printf("[pca] %s ret=%d (0完成 -1中止/未就绪 -3动作名未知)\r\n",
               name, ret);
    }
    return 0;
}

int ServoCmdRun(int argc, char **argv)
{
    if (argc < 2) {
        printf("[pca] 用法: run <动作> [参数...], list 看动作表\r\n");
        return 0;
    }
    return servo_action(argv[1], argc, argv, 0);
}

int ServoCmdPost(int argc, char **argv)
{
    if (argc < 2) {
        printf("[pca] 用法: post <动作> [参数...], list 看动作表\r\n");
        return 0;
    }
    return servo_action(argv[1], argc, argv, 1);
}

int ServoCmdStop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    OttoPcaStop();
    printf("[pca] 已请求中止(动作任务归位)\r\n");
    return 0;
}

int ServoCmdOff(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (ServoEnsureInit() != 0) {
        return 0;
    }
    OttoPcaStop();
    if (ServoAllOff() == 0) {
        printf("[pca] 全通道停脉冲(舵机失力松弛)\r\n");
    } else {
        printf("[pca] off FAIL\r\n");
    }
    return 0;
}

int ServoCmdList(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    OttoPcaList();
    return 0;
}

int ServoCmdMap(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    OttoPcaMap();
    return 0;
}

int ServoCmdPose(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    OttoPcaPose();
    return 0;
}

static int limb_by_name(const char *s)
{
    static const char *names[6] = { "ll", "rl", "lf", "rf", "lh", "rh" };
    int32_t x;
    int i;

    for (i = 0; i < 6; i++) {
        if (strcmp(s, names[i]) == 0) {
            return i;
        }
    }
    if (ShellParseInt(s, &x) == 0 && x >= 0 && x <= 5) {
        return (int)x;
    }
    return -1;
}

int ServoCmdTrim(int argc, char **argv)
{
    int limb, val;

    if (argc < 3 || (limb = limb_by_name(argv[1])) < 0 ||
        ShellParseInt(argv[2], &val) != 0) {
        printf("[pca] 用法: trim <ll|rl|lf|rf|lh|rh|0~5> <-50~50>\r\n");
        return 0;
    }
    if (OttoPcaSetTrim(limb, val) != 0) {
        printf("[pca] trim FAIL (val 超 -50~50)\r\n");
        return 0;
    }
    printf("[pca] trim ok (仅本次运行生效, 固化改 otto_pca.h 的 "
           "OTTO_LIMB_TRIM_TAB 重编)\r\n");
    return 0;
}
