/*
 * Lab1 的 M 态启动准备与 M -> S 特权级切换。
 *
 * entry.S 建立栈后调用 start()。本函数负责把 mret 所需的状态一次性
 * 配置完整：目标特权级、目标 PC、异常委托、PMP 和 satp。配置顺序的
 * 核心原则是：在 S 态入口可执行之前始终保持中断关闭。
 */

#include "types.h"
#include "riscv.h"

/* main() 是 mret 后的第一段 S 态 C 代码。 */
extern void main(void);

/* entry.S 中四字节对齐的早期陷阱入口。 */
extern void early_trap(void);

/* mstatus.MIE：M 态全局中断使能位。基线头文件未单独定义，故在此说明。 */
#define MSTATUS_MIE (1L << 3)

void
start(void)
{
  uint64 status;

  /*
   * mret 根据 mstatus.MPP 决定返回后的特权级。先清除旧值，再写入 S，
   * 同时明确清除 MIE，避免切换窗口中发生异步中断。
   */
  status = r_mstatus();
  status &= ~MSTATUS_MPP_MASK;
  status |= MSTATUS_MPP_S;
  status &= ~MSTATUS_MIE;
  w_mstatus(status);

  /* mret 会把 PC 设置为 mepc，因此目标必须是 S 态入口 main。 */
  w_mepc((uint64)main);

  /*
   * 将可委托的异常和中断交给 S 态。当前实验不开启中断，但提前明确
   * 所有权可以使启动状态与后续实验兼容。硬件会自动忽略不可委托位。
   */
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_mie(0);

  /*
   * 委托后的 S 态异常先落入 early_trap。Lab1 不恢复异常现场，只要求
   * 正常路径零异常；若有问题，CPU 会停住并在 -d int 中留下原因。
   */
  w_stvec((uint64)early_trap);

  /*
   * pmpcfg0=0x0f：R/W/X=1，A[1:0]=01 即 TOR，L=0。对条目 0 来说，
   * TOR 的下界固定为 0，上界由 pmpaddr0 给出；这里的大上界覆盖了
   * QEMU virt 使用的 RAM 与 UART 地址。它不是 NAPOT，不能将 0x0f
   * 误读为 NAPOT 模式。若缺少 PMP 授权，S 态可能在 main 首次取指
   * 时触发 instruction access fault。
   */
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0x0f);

  /*
   * Lab1 尚未创建页表，satp=0 表示 Bare 模式：S 态虚拟地址直接作为
   * 物理地址使用。sfence.vma 保证此前可能存在的地址翻译状态失效。
   */
  w_satp(0);
  asm volatile("sfence.vma zero, zero" ::: "memory");

  /*
   * mret 原子完成两件事：特权级切换到 MPP 指定的 S 态，并跳转到
   * mepc 指向的 main。该指令正常执行后不会返回到下一行。
   */
  asm volatile("mret");

  /* 防御性兜底：若模拟器异常地让 mret 返回，避免执行未知内存。 */
  for (;;)
    asm volatile("wfi");
}
