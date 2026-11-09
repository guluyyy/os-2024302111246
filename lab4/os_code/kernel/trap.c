/* Lab4 用户/内核陷阱：时钟唤醒、用户 tick 统计和每 tick 抢占都在这里完成。 */
#include "types.h"                     /* 引入 uint64 等陷阱处理使用的整数类型。 */
#include "memlayout.h"                 /* 引入 UART IRQ、TRAMPOLINE 等平台常量。 */
#include "riscv.h"                     /* 引入 scause、sepc、sstatus 等 CSR 接口。 */
#include "spinlock.h"                  /* 引入全局 ticks 条件锁结构。 */
#include "proc.h"                      /* 引入当前进程、调度和退出接口。 */
#include "defs.h"                      /* 引入 PLIC、系统调用和页表函数声明。 */

extern char uservec[];                 /* trampoline.S 的用户陷入入口。 */
extern char userret[];                 /* trampoline.S 的用户返回入口。 */
extern char trampoline[];              /* 跳板页链接地址。 */
extern void kernelvec(void);           /* kernelvec.S 的内核陷入入口。 */

static struct spinlock tickslock;      /* 保护 ticks 并作为 pause 的条件锁。 */
static uint64 ticks;                   /* 启动以来处理的时钟中断次数。 */

/* 函数作用：初始化全局时钟条件锁并安装内核陷阱向量。 */
void
trapinit(void)
{
  initlock(&tickslock, "ticks");       /* pause 与时钟中断共享这把锁。 */
  ticks = 0;                           /* 系统时钟从零开始。 */
  w_stvec((uint64)kernelvec);          /* S 态期间统一由 kernelvec 保存现场。 */
}

/* 函数作用：结算一个全局 tick，并唤醒所有 pause 等待者。 */
static void
clockintr(void)
{
  acquire(&tickslock);                 /* tick 更新和睡眠条件检查必须互斥。 */
  ticks++;                             /* 推进单调全局时钟。 */
  wakeup(&ticks);                      /* chan 使用 ticks 地址作为唯一条件键。 */
  release(&tickslock);                 /* 允许醒来的进程重新检查截止时间。 */
}

/* 函数作用：原子读取当前累计 tick。 */
uint64
ticks_read(void)
{
  uint64 value;                        /* 锁内稳定快照。 */
  acquire(&tickslock);                 /* 与时钟中断的写入互斥。 */
  value = ticks;                       /* 读取完整 64 位计数。 */
  release(&tickslock);                 /* 快照完成后允许下一次时钟更新。 */
  return value;                        /* 返回锁内取得的一致累计tick。 */
}

/* 函数作用：让当前进程睡眠指定 tick，而不是占用 CPU 忙等。 */
int
ticks_pause(int count)
{
  uint64 start;                        /* 等待区间起点。 */
  struct proc *p = myproc();           /* 需要检查 kill 状态的调用进程。 */
  if (count < 0)                       /* 负数无法表示合法的未来等待区间。 */
    return -1;                         /* 负时长没有定义。 */
  acquire(&tickslock);                 /* 固定 ticks 与入睡之间的竞态窗口。 */
  start = ticks;                       /* 在同一把锁下记录起点。 */
  while (ticks - start < (uint64)count) { /* 未达到请求tick数就继续等待。 */
    if (p->killed) {                   /* kill 应能打断长时间 pause。 */
      release(&tickslock);             /* kill打断等待时先交还全局时钟锁。 */
      return -1;                       /* 向用户报告pause未正常到期。 */
    }
    sleep(&ticks, &tickslock);         /* 原子释放锁、睡眠、醒后重获锁。 */
  }
  release(&tickslock);                 /* 到达截止 tick。 */
  return 0;                            /* 请求的tick区间已完整经过。 */
}

/* 函数作用：为 sret 和下一次用户陷入填写可信运行环境。 */
static void
prepare_user_return(void)
{
  struct proc *p = myproc();           /* 当前即将返回用户态的进程。 */
  struct trapframe *tf = p->trapframe; /* 用户寄存器与内核入口参数。 */
  uint64 status;                       /* 待修改的 sstatus。 */
  intr_off();                          /* stvec 切换窗口不能被 S 态中断打断。 */
  w_stvec(TRAMPOLINE + (uint64)(uservec - trampoline)); /* 下次 U trap 进 uservec。 */
  tf->kernel_satp = kernel_satp();     /* uservec 保存完寄存器后切回内核页表。 */
  tf->kernel_sp = (uint64)(p->kstack + LAB4_KSTACK_SIZE); /* 切换到可信内核栈。 */
  tf->kernel_trap = (uint64)usertrap;  /* 跳到当前 C 分发入口。 */
  tf->kernel_hartid = 0;              /* 基础验收固定 hart 0。 */
  status = r_sstatus();                /* 保留无关硬件状态位。 */
  status &= ~SSTATUS_SPP;              /* sret 的目标特权级选择 U 态。 */
  status |= SSTATUS_SPIE;              /* sret 后允许下一次用户态中断。 */
  w_sstatus(status);                   /* 提交返回特权级与中断状态。 */
  w_sepc(tf->epc);                     /* 返回保存的用户 PC。 */
}

/* 函数作用：处理来自用户态的系统调用、UART 和时钟中断。 */
uint64
usertrap(void)
{
  struct proc *p = myproc();           /* trap 期间当前进程不会无故改变。 */
  struct trapframe *tf = p->trapframe; /* uservec 已保存好的用户现场。 */
  uint64 cause = r_scause();           /* 精确异常/中断原因。 */
  int timer_tick = 0;                  /* 标记是否需要在返回前抢占。 */
  w_stvec((uint64)kernelvec);          /* 进入内核后嵌套 trap 必须走内核向量。 */
  if ((r_sstatus() & SSTATUS_SPP) != 0) /* SPP=1说明陷入前并非用户态。 */
    panic("usertrap: not from user");  /* 拒绝把 S 态现场误当用户现场。 */
  tf->epc = r_sepc();                  /* 保存用户被打断的 PC。 */

  if (cause == 8) {                    /* 8 表示 U 态 ecall。 */
    if (p->killed)                     /* 已被kill的进程不得继续发起服务。 */
      proc_exit(-1);                   /* 被 kill 的进程不再执行新系统调用。 */
    tf->epc += 4;                      /* 返回时跳过产生陷入的 ecall 指令。 */
    intr_on();                         /* 现场安全后允许系统调用期间响应设备。 */
    syscall();                         /* 按 a7 分发并把结果写回 a0。 */
  } else if (cause == 0x8000000000000009ULL) { /* S 态外部中断。 */
    int irq = plic_claim();            /* 从 PLIC 认领具体来源。 */
    if (irq == UART0_IRQ)              /* IRQ 10 对应 QEMU virt 的 16550 UART。 */
      uartintr();                      /* UART 生产字符并唤醒 console_read。 */
    else if (irq != 0)                 /* 非零未知IRQ需要记录但不击穿内核。 */
      printf("unexpected external irq=%d\n", irq); /* 保留诊断。 */
    if (irq != 0)                      /* PLIC只要求完成实际认领到的中断。 */
      plic_complete(irq);              /* 允许 PLIC 再次投递该设备。 */
  } else if (cause == 0x8000000000000001ULL) { /* timervec 转发的 SSIP。 */
    w_sip(r_sip() & ~2ULL);            /* 先确认并清除软件中断挂起位。 */
    clockintr();                       /* 推进 ticks 并唤醒 pause 等待者。 */
    acquire(&p->lock);                 /* 统计字段由进程锁保护。 */
    p->cpu_ticks++;                    /* 只统计“从用户态进入”的时钟 tick。 */
    p->last_slice_ticks++;             /* 增加当前调度片的观测值。 */
    release(&p->lock);                 /* 两项统计已原子更新，可以进行yield。 */
    timer_tick = 1;                    /* Lab4 在本 tick 结束后执行抢占。 */
  } else {
    printf("user trap: scause=%p sepc=%p stval=%p\n", /* 输出异常种类和定位信息。 */
           cause, r_sepc(), r_stval()); /* 输出非法用户访问定位信息。 */
    proc_exit(-1);                     /* 异常只终止犯错进程，不击穿内核。 */
  }

  if (p->killed)                       /* 系统调用或中断处理中可能刚收到kill。 */
    proc_exit(-1);                     /* 在安全返回点落实 kill。 */
  if (timer_tick)                      /* 只有用户态时钟中断触发抢占重选。 */
    yield();                           /* 每个用户 tick 强制重新进行 Stride 选择。 */
  prepare_user_return();               /* 重新填写可能已跨调度片的返回环境。 */
  return user_satp(p->pagetable);      /* trampoline userret 安装该用户页表。 */
}

/* 函数作用：处理内核态期间到达的 UART 和时钟中断，但不在内核中抢占。 */
void
kerneltrap(void)
{
  uint64 saved_sepc = r_sepc();        /* 保存被中断的内核 PC。 */
  uint64 saved_status = r_sstatus();   /* 保存返回所需的 sstatus。 */
  uint64 cause = r_scause();           /* 读取陷阱原因。 */
  if ((saved_status & SSTATUS_SPP) == 0) /* SPP=0说明错误地从用户态进了kernelvec。 */
    panic("kerneltrap: not supervisor"); /* 该入口只接受 S 态 trap。 */
  if (cause == 0x8000000000000009ULL) { /* 最高位表示中断，编号9表示S态外部中断。 */
    int irq = plic_claim();            /* 认领外部设备中断。 */
    if (irq == UART0_IRQ)              /* UART到达时生产输入并唤醒读者。 */
      uartintr();                      /* 输入到达时唤醒睡眠读者。 */
    else if (irq != 0)                 /* 记录实验未启用的其他设备中断。 */
      printf("unexpected external irq=%d\n", irq); /* 打印具体IRQ便于排查PLIC。 */
    if (irq != 0)                      /* 已认领IRQ必须完成，否则设备会停止再投递。 */
      plic_complete(irq);              /* 完成非零 IRQ。 */
  } else if (cause == 0x8000000000000001ULL) {
    w_sip(r_sip() & ~2ULL);            /* 清除转发来的时钟中断。 */
    clockintr();                       /* 内核 tick 推进全局时间但不计进程 CPU。 */
  } else {
    printf("kernel trap: scause=%p sepc=%p stval=%p\n", /* 输出内核异常三元组。 */
           cause, saved_sepc, r_stval()); /* 输出不可恢复的内核异常。 */
    panic("unexpected kernel trap");  /* 内核同步异常无法安全恢复，立即停止。 */
  }
  w_sepc(saved_sepc);                  /* kernelvec 最终 sret 回到原指令。 */
  w_sstatus(saved_status);             /* 恢复被中断前的状态。 */
}

/* 函数作用：从新进程的 forkret 主动经 userret/sret 首次进入用户态。 */
void
usertrapret(void)
{
  struct proc *p = myproc();           /* 当前已由 scheduler 标为 RUNNING。 */
  prepare_user_return();               /* 准备陷入头和硬件返回 CSR。 */
  ((void (*)(uint64))(TRAMPOLINE + (uint64)(userret - trampoline))) /* 计算高地址userret入口。 */
    (user_satp(p->pagetable));         /* 高地址跳板切用户页表并执行 sret。 */
  panic("userret returned");           /* 正常路径绝不会作为 C 函数返回。 */
}
