/* Lab4 自测二：运行中 setickets 改票后，Stride CPU 份额应随之变化。 */
#include "common.h"                    /* 引入断言、采样、计时与负载函数。 */

/* 函数作用：验证 1:1 初始份额以及把第二进程改为三倍票数后的份额变化。 */
int                                      /* 用户测试程序按ABI返回整数状态。 */
main(void)                               /* 比较改票前后两个进程的CPU份额。 */
{                                        /* 开始动态Stride权重验收。 */
  unsigned long cycles = tick_cycles(); /* 实测每 tick 对应的 time CSR 增量。 */
  unsigned long change_at = time_rdtime() + cycles * 70; /* 70 tick 后改票。 */
  unsigned long finish_at = change_at + cycles * 120;    /* 留足第二阶段运行时间。 */
  int p1, p2;                          /* 两个纯计算子进程 PID。 */
  int a1, a2, b1, b2;                 /* 两阶段各自新增 CPU tick。 */

  setickets(500);                      /* 父进程醒来后尽快完成统计采样。 */
  p1 = fork();                         /* 创建始终保持 10 票的基准进程。 */
  if (p1 == 0) {                         /* 第一个子进程作为固定10票基准。 */
    setickets(10);                     /* 第一进程全程保持 10 票。 */
    burn_until(finish_at);             /* 纯用户态计算到统一截止时刻。 */
    exit(0);                           /* 正常结束供父进程回收。 */
  }                                      /* 父进程和第二子进程跳过该分支。 */
  p2 = fork();                         /* 创建中途从 10 票改为 30 票的进程。 */
  if (p2 == 0) {                         /* 第二个子进程在约定时刻动态改票。 */
    setickets(10);                     /* 第一阶段与基准进程票数相同。 */
    burn_until(change_at);             /* 运行到约定改票时刻。 */
    setickets(30);                     /* 第二阶段覆盖为三倍票数。 */
    burn_until(finish_at);             /* 继续计算以观察新配额。 */
    exit(0);                           /* 正常结束供父进程回收。 */
  }                                      /* 父进程继续执行统计采样。 */
  ASSERT(p1 > 0 && p2 > 0, "fork失败"); /* 两个子进程都必须成功创建。 */
  ASSERT(delta_cpu(p1) == 0 && delta_cpu(p2) == 0, "无法建立统计基线"); /* 首次查询只记基准。 */
  pause(55);                           /* 第一窗口完整处于 10:10 阶段。 */
  a1 = delta_cpu(p1);                  /* 读取基准进程第一阶段增量。 */
  a2 = delta_cpu(p2);                  /* 读取改票进程第一阶段增量。 */
  pause(80);                           /* 跨过改票点并积累第二阶段样本。 */
  b1 = delta_cpu(p1);                  /* 读取基准进程第二阶段增量。 */
  b2 = delta_cpu(p2);                  /* 读取高票进程第二阶段增量。 */
  printf("RESULT stride_change equal=%d:%d changed=%d:%d\n", a1, a2, b1, b2); /* 输出两阶段证据。 */
  ASSERT(a1 > 0 && a2 > 0 && a1 * 2 > a2 && a2 * 2 > a1,
         "相同票数阶段明显不公平");    /* 容许采样边界带来的小幅误差。 */
  ASSERT(b2 > b1 * 2, "改为三倍票数后CPU份额没有明显增加"); /* 验证改票真正影响调度。 */
  kill(p1);                            /* 提前结束仍在计算的第一子进程。 */
  kill(p2);                            /* 提前结束仍在计算的第二子进程。 */
  wait(0);                             /* 回收一个子进程。 */
  wait(0);                             /* 回收另一个子进程。 */
  PASS("stride change");              /* 输出规范化通过标志并退出。 */
  return 0;                            /* PASS 内部 exit，保留语义完整的兜底。 */
}                                        /* PASS宏正常已调用exit，此处是语法兜底。 */
