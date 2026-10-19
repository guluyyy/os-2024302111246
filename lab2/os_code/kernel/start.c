/*
 * Lab2 的 M 态启动准备与 M -> S 特权级切换。
 *
 * entry.S 建立栈后调用 start()。本函数负责把 mret 所需的状态一次性
 * 配置完整：目标特权级、目标 PC、异常委托、PMP 和 satp。配置顺序的
 * 核心原则是：在 S 态入口可执行之前始终保持中断关闭。
 */

#include "types.h"      /* 引入 uint64 等启动代码需要的整数类型。 */
#include "memlayout.h"  /* 引入 CLINT mtime/mtimecmp 的物理地址。 */
#include "riscv.h"      /* 引入 M/S 态 CSR 读写函数和位掩码。 */

/* main() 是 mret 后的第一段 S 态 C 代码。 */
extern void main(void);             /* 引用 mret 后在 S 态执行的内核主函数。 */

/* entry.S 中四字节对齐的早期陷阱入口。 */
extern void early_trap(void);       /* 引用 entry.S 的早期异常自旋入口。 */
extern void timervec(void);         /* 引用 timervec.S 的 M 态时钟入口。 */

/* timervec 不使用当前栈，通过 mscratch 中的五个槽保存临时寄存器。 */
static uint64 timer_scratch[5];     /* 为 timervec 预留 a1-a3、比较器地址和间隔五槽。 */

/* 函数作用：配置 hart 0 的周期性 CLINT 机器时钟中断及转发环境。 */
static void                         /* 定时器初始化只配置硬件和 CSR。 */
timerinit(void)                     /* 配置 hart 0 周期性 CLINT 机器时钟中断。 */
{                                   /* 开始建立 M 态时钟转发环境。 */
  uint64 interval = 1000000;        /* 选择一百万个 mtime 计数作为中断间隔。 */

  *(volatile uint64 *)CLINT_MTIMECMP(0) = /* 直接访问 hart 0 的 64 位比较寄存器。 */
    *(volatile uint64 *)CLINT_MTIME + interval; /* 用当前时间加间隔预约第一次中断。 */

  timer_scratch[3] = CLINT_MTIMECMP(0); /* 告诉汇编 mtimecmp 的 MMIO 地址。 */
  timer_scratch[4] = interval;          /* 告诉汇编每次应推进的计时间隔。 */
  w_mscratch((uint64)timer_scratch);    /* 让 timervec 可无栈定位临时保存区。 */
  w_mtvec((uint64)timervec);            /* 让机器时钟中断直接跳入 timervec。 */

  /* M 态只接收不可委托的机器时钟中断，其余工作转交 S 态。 */
  w_mie(r_mie() | MIE_MTIE);            /* 打开机器级时钟中断源。 */
  w_mstatus(r_mstatus() | MSTATUS_MIE);  /* 打开 M 态全局中断开关。 */
}                                       /* 结束 CLINT 定时器初始化。 */

/* 函数作用：在 M 态准备 S 态运行环境，并通过 mret 跳入内核 main。 */
void                                   /* start 完成后通过 mret 跳走，不向 entry.S 返回值。 */
start(void)                            /* 在 M 态准备 S 态内核运行环境。 */
{                                      /* 开始机器态启动配置。 */
  uint64 status;                       /* 暂存 mstatus，便于只修改目标位。 */

  /*
   * mret 根据 mstatus.MPP 决定返回后的特权级。先清除旧值，再写入 S，
   * 同时明确清除 MIE，避免切换窗口中发生异步中断。
   */
  status = r_mstatus();                /* 读取 QEMU 当前机器状态寄存器。 */
  status &= ~MSTATUS_MPP_MASK;         /* 清除上一次特权级字段的旧值。 */
  status |= MSTATUS_MPP_S;             /* 指定 mret 后进入 Supervisor 态。 */
  status &= ~MSTATUS_MIE;              /* 暂时关闭 M 态全局中断，保护启动窗口。 */
  w_mstatus(status);                   /* 一次性写回准备好的 mstatus。 */

  /* mret 会把 PC 设置为 mepc，因此目标必须是 S 态入口 main。 */
  w_mepc((uint64)main);                /* 指定 mret 后从 S 态 main 第一条指令继续。 */

  /*
   * 将可委托的异常和中断交给 S 态。当前实验不开启中断，但提前明确
   * 所有权可以使启动状态与后续实验兼容。硬件会自动忽略不可委托位。
   */
  w_medeleg(0xffff);                   /* 尝试把所有可委托同步异常交给 S 态。 */
  w_mideleg(0xffff);                   /* 尝试把所有可委托中断交给 S 态。 */
  w_mie(0);                            /* 在配置 timervec 前先关闭所有 M 态中断源。 */
  w_sie(SIE_SEIE | SIE_SSIE);          /* 允许 S 态接收 PLIC 外部中断和软件时钟中断。 */

  /*
   * 委托后的 S 态异常先落入 early_trap。Lab1 不恢复异常现场，只要求
   * 正常路径零异常；若有问题，CPU 会停住并在 -d int 中留下原因。
   */
  w_stvec((uint64)early_trap);         /* 正式 trap 初始化前用自旋入口捕获启动异常。 */

  /*
   * pmpcfg0=0x0f：R/W/X=1，A[1:0]=01 即 TOR，L=0。对条目 0 来说，
   * TOR 的下界固定为 0，上界由 pmpaddr0 给出；这里的大上界覆盖了
   * QEMU virt 使用的 RAM 与 UART 地址。它不是 NAPOT，不能将 0x0f
   * 误读为 NAPOT 模式。若缺少 PMP 授权，S 态可能在 main 首次取指
   * 时触发 instruction access fault。
   */
  w_pmpaddr0(0x3fffffffffffffull);     /* 设置 TOR 条目上界，覆盖实验需要的物理地址。 */
  w_pmpcfg0(0x0f);                    /* 以 TOR 模式向 S/U 态授予读写执行权限。 */

  /*
   * Lab2 尚未创建页表，satp=0 表示 Bare 模式：S 态虚拟地址直接作为
   * 物理地址使用。sfence.vma 保证此前可能存在的地址翻译状态失效。
   */
  w_satp(0);                           /* 启动 S 态内核使用 Bare 地址转换模式。 */
  asm volatile("sfence.vma zero, zero" ::: "memory"); /* 清除可能残留的地址翻译缓存。 */

  /* 兼容不支持 Sstc 的 QEMU：M 态 CLINT 定时器转发为 S 态软件中断。 */
  timerinit();                         /* 配置 M 态 CLINT 周期中断及 S 态转发。 */

  /* trampoline 把 hartid 从 trapframe 恢复到 tp；本实验固定 hart 0。 */
  w_tp(0);                             /* 把线程指针初始化为当前 hart 0 编号。 */

  /*
   * mret 原子完成两件事：特权级切换到 MPP 指定的 S 态，并跳转到
   * mepc 指向的 main。该指令正常执行后不会返回到下一行。
   */
  asm volatile("mret");               /* 根据 MPP/mepc 原子进入 S 态 main。 */

  /* 防御性兜底：若模拟器异常地让 mret 返回，避免执行未知内存。 */
  for (;;)                             /* 正常路径不可达；异常返回时永久循环。 */
    asm volatile("wfi");              /* 低功耗等待中断，防止执行未知后续内存。 */
}                                      /* 仅在不符合架构的异常情况下到达。 */
