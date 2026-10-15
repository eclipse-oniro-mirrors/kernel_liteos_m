#ifndef SERVO_SERVICE_H
#define SERVO_SERVICE_H
/*
 * servo_service.h - PCA9685 舵机 / Otto 动作服务
 * 依赖: pca9685_sdk(libpca9685.a: PcaXxx / OttoPcaXxx) + wifi_service(监控暂停)
 *
 * 后续接入方法: include 本头文件, 调 ServoXxx 语义接口即可;
 * shell 新指令在 servo_service.c 加 ServoCmdXxx, 声明放这里, 主程序命令表绑定。
 */

/* ============【PCA9685/舵机 配置区】============ */
#define SERVO_CFG_BUS  6       /* I2C 总线号 */
#define SERVO_CFG_ADDR 0x40    /* PCA9685 7 位地址(地址跳线全开) */

/* 懒初始化(首次舵机命令时自动调); 0=就绪 */
int ServoEnsureInit(void);

/* ---- 语义接口(供后续新功能直接调用) ---- */
int ServoScanBus(int bus);              /* 扫描总线, 返回应答器件数(<0 总线异常) */
int ServoInit(uint8_t bus, uint8_t addr);
int ServoSetAngle(int ch, int deg);     /* ch 0~15, deg 0~180 */
int ServoSetUs(int ch, int us);         /* ch 0~15, us 500~2500 */
int ServoAllOff(void);                  /* 全通道停脉冲(失力) */

/* ---- shell 指令入口(主程序命令表绑定) ---- */
int ServoCmdScan(int argc, char **argv);     /* scan [bus]              */
int ServoCmdInit(int argc, char **argv);     /* pinit [bus] [addr]      */
int ServoCmdStatus(int argc, char **argv);   /* pstatus                 */
int ServoCmdAngle(int argc, char **argv);    /* angle <ch> <deg>        */
int ServoCmdUs(int argc, char **argv);       /* us <ch> <us>            */
int ServoCmdRun(int argc, char **argv);      /* run <动作> [参数...]    */
int ServoCmdPost(int argc, char **argv);     /* post <动作> [参数...]   */
int ServoCmdStop(int argc, char **argv);     /* stop                    */
int ServoCmdOff(int argc, char **argv);      /* off                     */
int ServoCmdList(int argc, char **argv);     /* list                    */
int ServoCmdMap(int argc, char **argv);      /* map                     */
int ServoCmdPose(int argc, char **argv);     /* pose                    */
int ServoCmdTrim(int argc, char **argv);     /* trim <肢体> <-50~50>    */

#endif /* SERVO_SERVICE_H */
