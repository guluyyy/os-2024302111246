/* QEMU virt 平台级中断控制器，只启用 hart 0 的 UART 外部中断。 */
#include "types.h"      /* 引入 uint32 等固定宽度整数类型。 */
#include "memlayout.h"  /* 引入 PLIC 寄存器地址和 UART 中断号。 */

/* 函数作用：设置 UART 外部中断在 PLIC 中的全局优先级。 */
void                    /* 本函数只执行初始化操作，不返回结果。 */
plicinit(void)          /* 初始化整个 PLIC 的 UART 中断优先级。 */
{                       /* 开始执行全局 PLIC 初始化。 */
  /* 优先级 0 表示禁用；设为 1 后 UART 才能参与仲裁。 */
  *(volatile uint32 *)(PLIC_PRIORITY + UART0_IRQ * 4) = 1; /* 把 UART IRQ 对应的优先级寄存器写为 1。 */
}                       /* 结束全局 PLIC 初始化。 */

/* 函数作用：允许 hart 0 的 S 态上下文接收 UART 外部中断。 */
void                    /* 本函数只配置当前 hart，不返回结果。 */
plicinithart(void)      /* 初始化 hart 0 的 S 态 PLIC 上下文。 */
{                       /* 开始配置 hart 0 的中断接收条件。 */
  /* lab2 固定单核，因此只配置 hart 0 的 S 态上下文。 */
  *(volatile uint32 *)PLIC_SENABLE(0) = 1U << UART0_IRQ; /* 只允许 UART 中断送往 hart 0 的 S 态。 */
  *(volatile uint32 *)PLIC_SPRIORITY(0) = 0;             /* 把接收阈值设为 0，使优先级 1 的 UART 可通过。 */
}                       /* 结束 hart 0 的 PLIC 配置。 */

/* 函数作用：从 PLIC 认领当前最高优先级的待处理外部中断。 */
int                     /* 返回 PLIC 给出的外部中断编号。 */
plic_claim(void)        /* 查询并认领 hart 0 当前最高优先级中断。 */
{                       /* 开始一次中断认领操作。 */
  return *(volatile uint32 *)PLIC_SCLAIM(0); /* 读取 claim 寄存器；读取动作同时认领该 IRQ。 */
}                       /* 结束中断认领操作。 */

/* 函数作用：通知 PLIC 指定外部中断已经处理完毕。 */
void                    /* 完成通知不需要返回值。 */
plic_complete(int irq)  /* 接收已经处理完毕的 IRQ 编号。 */
{                       /* 开始向 PLIC 报告处理完成。 */
  *(volatile uint32 *)PLIC_SCLAIM(0) = (uint32)irq; /* 把 IRQ 写回 complete 寄存器，允许后续同类中断。 */
}                       /* 结束中断完成通知。 */
