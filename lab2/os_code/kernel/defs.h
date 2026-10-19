#ifndef LAB2_DEFS_H /* 若本头文件尚未包含，则进入声明区域。 */
#define LAB2_DEFS_H /* 定义包含保护宏，防止函数声明被重复处理。 */

#include "types.h" /* 引入各函数原型使用的 uint64 等类型。 */

/* console.c */
void consoleinit(void);                         /* 初始化 16550 UART 和控制台输入状态。 */
void console_set_banner_mode(int enabled);      /* 打开或关闭 Lab1 个性化字符协议。 */
void console_putc(char c);                      /* 向 UART 输出一个逻辑字符。 */
int console_read(char *dst, int n);             /* 从输入环形缓冲区读取至多 n 字节。 */
int console_write(const char *src, int n);      /* 连续输出 src 中的 n 个字节。 */
void uartintr(void);                            /* 处理 UART 接收中断并生产输入字符。 */

/* printf.c */
void printf(const char *format, ...);           /* 按最小格式集合输出内核日志。 */
void panic(const char *message) __attribute__((noreturn)); /* 打印不可恢复错误后永久停机。 */

/* plic.c */
void plicinit(void);                            /* 设置 PLIC 中 UART 的全局优先级。 */
void plicinithart(void);                        /* 配置 hart 0 的 S 态中断使能与阈值。 */
int plic_claim(void);                           /* 从 PLIC 认领一个待处理外部中断。 */
void plic_complete(int irq);                    /* 通知 PLIC 指定 IRQ 已处理完成。 */

/* vm.c */
void user_pagetable_init(void);                 /* 建立本实验使用的最小 Sv39 用户页表。 */
uint64 user_satp(void);                         /* 生成切换到用户页表所需的 satp 值。 */

/* trap.c */
void trapinit(void);                            /* 初始化 trap 入口和 tick 计数。 */
uint64 usertrap(void);                          /* 分发来自 U 态的异常和中断。 */
void usertrapret(void) __attribute__((noreturn)); /* 首次构造现场并进入用户态。 */
void kerneltrap(void);                          /* 分发内核处于 S 态时发生的中断。 */
uint64 ticks_read(void);                        /* 原子读取系统累计时钟 tick。 */

/* syscall.c */
void syscall(void);                             /* 根据 a7 调用号分发系统调用。 */

/* string.c */
void *memset(void *dst, int value, uint64 count);                 /* 按字节填充内存区域。 */
void *memmove(void *dst, const void *src, uint64 count);          /* 安全复制可能重叠的内存区域。 */
int strcmp(const char *left, const char *right);                  /* 比较两个 NUL 结尾字符串。 */
uint64 strlen(const char *text);                                 /* 计算字符串字节长度。 */
char *strncpy(char *dst, const char *src, int count);             /* 有上限地复制并补零字符串。 */

#endif /* 结束 LAB2_DEFS_H 包含保护范围。 */
