/*
 * 用户态/内核态陷阱分发。赠送的 trampoline.S 只负责保存恢复寄存器，
 * 本文件负责识别 ecall、UART 外部中断和 S 态时钟中断。
 */
#include "types.h"       /* 引入 uint64 和 uint 等基本整数类型。 */
#include "course_sid.h"  /* 引入个人时间片参数 LAB2_TICK。 */
#include "memlayout.h"   /* 引入 UART IRQ 等平台内存布局常量。 */
#include "riscv.h"       /* 引入 CSR 读写函数和 sstatus 位定义。 */
#include "proc.h"        /* 引入 trapframe、进程切换和退出接口。 */
#include "defs.h"        /* 引入 PLIC、控制台、页表和系统调用原型。 */

extern char uservec[];                /* 引用 trampoline.S 导出的用户态 trap 入口。 */
extern char userret[];                /* 引用 trampoline.S 导出的用户态返回入口。 */
extern void kernelvec(void);          /* 引用 kernelvec.S 导出的内核态 trap 入口。 */

static volatile uint64 ticks;         /* 保存系统启动以来累计处理的定时器 tick。 */
static uint quantum_ticks;            /* 保存当前个人时间片内已经累计的 tick 数。 */

/* 函数作用：初始化 tick 计数，并安装内核态默认 trap 入口。 */
void                                  /* trap 初始化只修改全局状态，不返回结果。 */
trapinit(void)                        /* 初始化计时状态并设置默认 S 态 trap 入口。 */
{                                     /* 开始初始化 trap 子系统。 */
  ticks = 0;                          /* 把系统累计 tick 清零。 */
  quantum_ticks = 0;                  /* 把当前时间片内 tick 清零。 */
  w_stvec((uint64)kernelvec);         /* 内核初始化期间若发生 trap，统一进入 kernelvec。 */
}                                     /* 结束 trap 子系统初始化。 */

/* 函数作用：为即将执行的 sret 以及下一次用户陷入准备完整环境。 */
static void                           /* 该帮助函数只准备硬件与现场，不返回结果。 */
prepare_user_return(void)             /* 为下一次 sret 和再下一次用户陷入准备环境。 */
{                                     /* 开始构造可信的用户返回现场。 */
  struct proc *p = myproc();          /* 取得即将运行的当前进程。 */
  struct trapframe *tf = active_trapframe(); /* 取得 trampoline 访问的活动现场页。 */
  uint64 status;                      /* 暂存即将修改的 sstatus 寄存器值。 */

  /* 切换 stvec 的窗口不能被 S 态中断打断。 */
  intr_off();                         /* 关闭 S 态全局中断，避免 stvec 切换窗口被打断。 */
  w_stvec((uint64)uservec);           /* 规定下一次来自 U 态的 trap 先进入 uservec。 */

  /* uservec 下次陷入时从这五项建立可信内核运行环境。 */
  tf->kernel_satp = 0;                /* 告诉 uservec 陷入后切回 Bare 内核地址空间。 */
  tf->kernel_sp = (uint64)(p->kstack + LAB2_KSTACK_SIZE); /* 把当前进程内核栈顶交给 uservec。 */
  tf->kernel_trap = (uint64)usertrap; /* 把保存现场后应调用的 C 入口交给 uservec。 */
  tf->kernel_hartid = 0;              /* 本实验固定单核，把 hart 0 编号交给 uservec 恢复 tp。 */

  status = r_sstatus();               /* 读取当前 S 态状态，避免覆盖无关标志位。 */
  status &= ~SSTATUS_SPP;  /* sret 的目标特权级为 U 态。 */
  status |= SSTATUS_SPIE;  /* sret 后恢复用户态中断使能。 */
  w_sstatus(status);                  /* 写回为 sret 准备好的特权级和中断状态。 */
  w_sepc(tf->epc);                    /* 指定 sret 返回到 trapframe 保存的用户 PC。 */
}                                     /* 完成用户返回环境准备。 */

/* 函数作用：分发来自用户态的系统调用、外部中断和软件时钟中断。 */
uint64                                /* 返回 userret 要安装的用户 satp 值。 */
usertrap(void)                        /* 分发由 U 态 ecall 或设备中断触发的 trap。 */
{                                     /* 开始处理一次用户态 trap。 */
  uint64 cause;                       /* 保存 scause 中的同步/异步原因编码。 */
  int device;                         /* 用 1 表示外部中断、2 表示软件时钟中断。 */
  struct trapframe *tf = active_trapframe(); /* 取得 uservec 已保存好的用户现场。 */

  if ((r_sstatus() & SSTATUS_SPP) != 0) /* SPP 非零说明 trap 前并不在 U 态。 */
    panic("usertrap entered from supervisor mode"); /* 拒绝错误入口，避免按用户现场误处理。 */

  /* 现在已经进入 S 态，后续嵌套中断必须走 kernelvec。 */
  w_stvec((uint64)kernelvec);         /* 处理期间若嵌套中断，应使用内核栈版入口。 */
  tf->epc = r_sepc();                 /* 保存用户 trap 前的 PC，稍后用于 sret。 */
  cause = r_scause();                 /* 读取本次 trap 的精确原因。 */

  if (cause == 8) {                   /* scause=8 表示来自 U 态的 ecall。 */
    /* ecall 的 sepc 指向 ecall 本身，返回前必须跳过这条 4 字节指令。 */
    tf->epc += 4;                     /* 跳过 4 字节 ecall，避免返回后再次陷入。 */
    intr_on();                        /* 现场和入口已经安全，允许系统调用期间响应设备中断。 */
    syscall();                        /* 根据 a7 调用号执行对应内核服务。 */
  } else if ((device = (int)((cause == 0x8000000000000009ULL) ? 1 : /* 把外部中断先编码为设备 1。 */
                             (cause == 0x8000000000000001ULL) ? 2 : 0)) != 0) { /* 把两种已知异步原因归一化。 */
    if (device == 1) {                /* 设备 1 表示 S 态外部中断。 */
      int irq = plic_claim();         /* 从 PLIC 取得具体外设中断号。 */
      if (irq == UART0_IRQ)           /* UART0 中断号说明有串口字符到达。 */
        uartintr();                   /* 读取 UART 并把字符送入输入缓冲区。 */
      else if (irq != 0)              /* 非零但不是 UART 的 IRQ 不应在本实验出现。 */
        printf("unexpected external irq=%d\n", irq); /* 打印异常 IRQ，保留内核运行能力。 */
      if (irq != 0)                   /* 只有成功认领了中断才需要完成通知。 */
        plic_complete(irq);           /* 告诉 PLIC 当前 IRQ 已经处理完毕。 */
    } else {                          /* 设备 2 表示 M 态转发来的 S 态软件时钟中断。 */
      w_sip(r_sip() & ~2ULL); /* 清除 timervec 置起的 SSIP。 */
      ticks++;                        /* 累加系统总运行 tick。 */
      quantum_ticks++;                /* 累加当前个人时间片内的 tick。 */
      if (quantum_ticks >= LAB2_TICK) /* 达到个人时间片长度时执行一次时间片结算。 */
        quantum_ticks = 0; /* lab2 仅记时间片，不做真正抢占调度。 */
    }                                 /* 结束软件时钟中断处理。 */
  } else {                            /* 进入实验未预期的用户异常处理。 */
    printf("user trap: scause=%p sepc=%p stval=%p\n", /* 组织用户异常诊断三元组。 */
           cause, r_sepc(), r_stval()); /* 输出原因、异常 PC 和附加地址用于定位。 */
    proc_exit(-1);                    /* 以失败状态结束出错用户进程并恢复父进程。 */
  }                                   /* 结束用户 trap 原因分派。 */

  /* syscall 可能通过 wait/exit 改变当前进程，因此此处重新取活动现场。 */
  prepare_user_return();              /* 为当前活动进程重新填写返回与下次陷入环境。 */
  return user_satp(); /* 作为 a0 返回给紧随其后的 trampoline userret。 */
}                                     /* 结束用户态 trap 处理并回到 userret。 */

/* 函数作用：处理内核在 S 态运行期间发生的 UART 和软件时钟中断。 */
void                                  /* 处理完成后由 kernelvec 使用 sret 返回。 */
kerneltrap(void)                      /* 分发内核在 S 态运行期间发生的异步中断。 */
{                                     /* 开始处理一次内核态 trap。 */
  uint64 saved_sepc = r_sepc();       /* 保存被中断的内核 PC，避免 C 调用覆盖返回目标。 */
  uint64 saved_status = r_sstatus();  /* 保存原 S 态状态，稍后原样恢复。 */
  uint64 cause = r_scause();          /* 读取本次内核 trap 原因。 */

  if ((saved_status & SSTATUS_SPP) == 0) /* kerneltrap 只应处理 trap 前位于 S 态的情况。 */
    panic("kerneltrap not from supervisor mode"); /* 入口级别不符说明 trap 路径已经损坏。 */

  if (cause == 0x8000000000000009ULL) { /* 最高位为 1 且编号 9 表示 S 态外部中断。 */
    int irq = plic_claim();            /* 从 PLIC 认领实际外设 IRQ。 */
    if (irq == UART0_IRQ)              /* 判断中断是否来自本实验启用的 UART0。 */
      uartintr();                      /* 消费串口字符并更新输入缓冲区。 */
    else if (irq != 0)                 /* 记录所有非零未知外设 IRQ。 */
      printf("unexpected external irq=%d\n", irq); /* 打印中断号帮助检查 PLIC 配置。 */
    if (irq != 0)                      /* IRQ 为 0 表示没有可认领的外部中断。 */
      plic_complete(irq);              /* 完成非零 IRQ，允许 PLIC 继续投递。 */
  } else if (cause == 0x8000000000000001ULL) { /* 编号 1 表示 S 态软件中断。 */
    w_sip(r_sip() & ~2ULL);            /* 清除 SSIP，确认已接收转发来的时钟事件。 */
    ticks++;                           /* 增加全局 tick 计数。 */
    quantum_ticks++;                   /* 增加当前时间片 tick 计数。 */
    if (quantum_ticks >= LAB2_TICK)    /* 检查是否已经达到个人时间片长度。 */
      quantum_ticks = 0;               /* Lab2 不调度，只把时间片计数重新开始。 */
  } else {                             /* 其他原因不是本实验允许的内核异步事件。 */
    printf("kernel trap: scause=%p sepc=%p stval=%p\n", /* 组织内核异常诊断三元组。 */
           cause, saved_sepc, r_stval()); /* 输出三项关键 CSR 供异常定位。 */
    panic("unexpected kernel trap");   /* 内核自身异常不可安全恢复，立即停机。 */
  }                                    /* 结束内核 trap 原因分派。 */

  /* sret 必须返回被中断的内核指令，而不是处理函数内部。 */
  w_sepc(saved_sepc);                  /* 恢复 kernelvec 最终 sret 的目标 PC。 */
  w_sstatus(saved_status);             /* 恢复中断前的 S 态状态和中断位。 */
}                                      /* 结束内核态 trap 处理。 */

/* 函数作用：在单核环境中原子读取系统累计时钟 tick。 */
uint64                                 /* 返回一致的 64 位累计 tick 快照。 */
ticks_read(void)                       /* 在单核环境通过短暂关中断原子读取 ticks。 */
{                                      /* 开始保护 tick 读取临界区。 */
  uint64 value;                        /* 暂存读取出的 tick，离开临界区后返回。 */
  int enabled = intr_get();            /* 记录调用前是否允许 S 态中断。 */
  intr_off();                          /* 禁止定时器处理在 64 位读取期间修改 ticks。 */
  value = ticks;                       /* 复制当前累计 tick。 */
  if (enabled)                         /* 只有调用前已开中断才需要重新开启。 */
    intr_on();                         /* 恢复调用者原来的中断使能状态。 */
  return value;                        /* 返回稳定的 tick 快照。 */
}                                      /* 结束 tick 原子读取。 */

/* 首次启动 Shell 时没有 uservec 调用者，C 代码主动跳进 userret。 */
/* 函数作用：构造首次用户返回环境，并经 userret/sret 进入当前用户程序。 */
void                                   /* 函数成功后进入 U 态，因此不会按 C 调用返回。 */
usertrapret(void)                      /* 首次启动 Shell 时主动构造并跳入 userret。 */
{                                      /* 开始首次用户态切换。 */
  prepare_user_return();               /* 填写 trapframe 头部、sstatus、sepc 和 stvec。 */
  ((void (*)(uint64))userret)(user_satp()); /* 把用户 satp 传给汇编入口并恢复用户现场。 */
  panic("userret returned");           /* 防御性兜底：正常 userret 执行 sret，绝不会返回。 */
}                                      /* 仅在异常实现错误时才能到达函数结束。 */
