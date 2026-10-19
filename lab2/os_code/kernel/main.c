/*
 * Lab2 的 S 态主函数：先回归 lab1 Banner，再建立 PLIC、陷阱与最小
 * 进程环境，装载内嵌 sh，最后第一次通过 sret 进入用户态。
 */

#include "types.h"       /* 引入 uint 等 Banner 格式参数类型。 */
#include "course_sid.h"  /* 引入学号与 Lab1/Lab2 个性化参数。 */
#include "defs.h"        /* 引入控制台、trap、PLIC、页表等初始化原型。 */
#include "proc.h"        /* 引入进程初始化和内嵌程序装载接口。 */

/* 两级宏展开：先展开 COURSE_SID，再把最终数字转换成字符串常量。 */
#define STRINGIFY_INNER(value) #value           /* 用 # 把已经传入的宏值转成字符串。 */
#define STRINGIFY(value) STRINGIFY_INNER(value) /* 先展开 COURSE_SID，再调用内层字符串化。 */

/*
 * 连续超长字符串跨过多个节流周期，用于确认 printf 不依赖短缓冲区，
 * console.c 的计数和 UART 忙等也能在长输出过程中保持稳定。
 */
static const char long_test_text[] =             /* 定义只读长字符串用于 printf 边界回归。 */
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                  /* 放入第一组大写英文字母。 */
  "abcdefghijklmnopqrstuvwxyz"                  /* 紧接第一组小写英文字母。 */
  "0123456789"                                  /* 紧接第一组十进制数字。 */
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                  /* 放入第二组大写英文字母。 */
  "abcdefghijklmnopqrstuvwxyz"                  /* 紧接第二组小写英文字母。 */
  "0123456789";                                 /* 以第二组数字和编译器自动 NUL 结束。 */

/* 函数作用：组织 S 态各模块初始化、装载 Shell 并首次进入用户态。 */
void                                             /* 裸机内核主函数不向调用者返回值。 */
main(void)                                       /* 组织 S 态初始化并首次进入用户 Shell。 */
{                                                /* 开始 Lab2 的 S 态主流程。 */
  /* 先把 UART 配置为确定的 8N1 状态，并打开接收中断源。 */
  consoleinit();                                 /* 配置 UART 8N1、FIFO 和接收中断源。 */

  /*
   * 个性化 Banner。学号来自 course_sid.h；mod97 由编译期公式得到。
   * 仅这三行启用 lab1 个人行协议，Shell 与用户程序输出保持普通文本。
   */
  console_set_banner_mode(1);                    /* 临时开启个人协议，只包围 Lab1 三行输出。 */
  printf("OSLAB1 sid=%s mod97=0x%x\n",          /* 按课程规定的首行格式组织 Banner。 */
         STRINGIFY(COURSE_SID), (uint)(COURSE_SID % 97U)); /* 输出学号和模 97 个性值。 */

  /* printf 边界自检：0、负数、最大 32 位正整数、十六进制和空字符串。 */
  printf("SELFTEST zero=%d neg=%d max=%d hex=0x%x empty=[%s]\n", /* 组织五类 printf 边界字段。 */
         0, -12345, 2147483647, (uint)(COURSE_SID % 97U), ""); /* 覆盖格式化边界组合。 */

  /* 长字符串自检：输出长度超过常见的 32/64 字节临时缓冲区。 */
  printf("SELFTEST long=%s\n", long_test_text); /* 验证长字符串无需固定小缓冲区。 */
  console_set_banner_mode(0);                   /* 后续 Shell 与用户程序恢复普通明文。 */

  /* 先装陷阱入口，再打开 PLIC 的 hart 0 UART 通道。 */
  trapinit();                                   /* 初始化 stvec 与 tick 计数。 */
  plicinit();                                   /* 设置 UART 外部中断优先级。 */
  plicinithart();                               /* 允许 UART IRQ 投递到 hart 0 S 态。 */
  user_pagetable_init();                        /* 建立用户区、trapframe 和跳板映射。 */

  procinit();                                   /* 清空进程资源并建立首个活动进程槽。 */
  if (proc_exec("sh") < 0)                     /* 从内嵌程序表装载最小 Shell。 */
    panic("cannot load embedded sh");           /* Shell 缺失时无法继续验收，打印并停机。 */

  printf("lab2: entering user shell\n");        /* 提示即将发生第一次 S 态到 U 态切换。 */
  usertrapret();                                /* 构造现场并经 userret/sret 进入 Shell。 */
}                                                /* 正常情况下 usertrapret 永不返回到这里。 */

