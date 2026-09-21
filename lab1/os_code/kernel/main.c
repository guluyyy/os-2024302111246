/*
 * Lab1 的 S 态主函数。
 *
 * start() 通过 mret 进入这里。main() 只组织控制台输出与 printf 边界
 * 自检，不建立中断、输入、进程或文件系统；这些能力属于后续实验。
 */

#include "types.h"
#include "course_sid.h"

void consoleinit(void);
void printf(const char *format, ...);

/* 两级宏展开：先展开 COURSE_SID，再把最终数字转换成字符串常量。 */
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)

/*
 * 连续超长字符串跨过多个节流周期，用于确认 printf 不依赖短缓冲区，
 * console.c 的计数和 UART 忙等也能在长输出过程中保持稳定。
 */
static const char long_test_text[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789";

void
main(void)
{
  /* 先把 UART 配置为确定的 8N1 轮询输出状态。 */
  consoleinit();

  /*
   * 个性化 Banner。学号来自 course_sid.h；mod97 由编译期公式得到。
   * 当前结果是 0x24，%x 会保证小写且没有前导零。
   */
  printf("OSLAB1 sid=%s mod97=0x%x\n",
         STRINGIFY(COURSE_SID), (uint)(COURSE_SID % 97U));

  /* printf 边界自检：0、负数、最大 32 位正整数、十六进制和空字符串。 */
  printf("SELFTEST zero=%d neg=%d max=%d hex=0x%x empty=[%s]\n",
         0, -12345, 2147483647, (uint)(COURSE_SID % 97U), "");

  /* 长字符串自检：输出长度超过常见的 32/64 字节临时缓冲区。 */
  printf("SELFTEST long=%s\n", long_test_text);

  /*
   * 裸机 main 没有调用者可以返回。输出结束后永久等待；中断尚未开启，
   * 因此不会产生额外串口字符，QEMU 仍可由宿主终止。
   */
  for (;;)
    asm volatile("wfi");
}

