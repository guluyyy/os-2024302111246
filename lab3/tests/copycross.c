/* Lab3 自测：让 write 的源缓冲区跨越两个独立物理页。 */
#include "kernel/types.h"
#include "user/user.h"

/* 函数作用：构造跨页字符串并验证 copyin 能分段翻译两个用户页。 */
int                                      /* 以进程退出状态向测试框架报告成败。 */
main(void)                               /* 测试跨页用户缓冲区能否被内核 write 正确读取。 */
{                                        /* 开始构造和提交跨页数据。 */
  char *base = sbrk(8192);                    /* EAGER 分配两个连续虚拟页。 */
  char *cross;                            /* 保存故意靠近页尾的跨页字符串地址。 */
  const char text[] = "CROSS-PAGE-PASS\n"; /* 定义预期由 write 完整复制并输出的固定文本。 */
  int i;                                  /* 记录已复制字符数，同时作为 write 长度。 */

  if (base == SBRK_ERROR || base == 0) {  /* 若扩展地址空间失败，就无法继续进行跨页复制测试。 */
    printf("TEST-COPY FAIL: sbrk\n");    /* 输出内存申请阶段失败的可识别诊断。 */
    exit(1);                              /* 用非零状态通知 shell 本用例失败。 */
  }                                      /* 结束 sbrk 失败处理。 */
  cross = base + 4090;                        /* 前 6 字节在第一页，其余落入第二页。 */
  for (i = 0; text[i] != '\0'; i++)      /* 逐字节写入，刻意让数据从第一页延伸到第二页。 */
    cross[i] = text[i];                   /* 把第 i 个测试字符存入跨页用户缓冲区。 */
  if (write(1, cross, i) != i) {          /* 要求内核跨两页复制后一次写出全部 i 个字节。 */
    printf("TEST-COPY FAIL: write\n");   /* 输出 copyin 或串口写入阶段失败的诊断。 */
    exit(1);                              /* 用非零状态通知 shell 本用例失败。 */
  }                                      /* 结束 write 失败处理。 */
  printf("TEST-COPY PASS: controlled copy crossed page boundary\n"); /* 明确报告跨页复制测试已经通过。 */
  exit(0);                               /* 用零状态结束测试进程。 */
}                                        /* 测试程序不会返回到用户态调用者。 */
