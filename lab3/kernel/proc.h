#ifndef LAB3_PROC_H /* 若尚未包含本文件，则进入进程结构声明。 */
#define LAB3_PROC_H /* 定义保护宏，避免结构体和枚举重复定义。 */

#include "types.h"      /* 引入 uint64 和 uchar 等字段类型。 */
#include "riscv.h"      /* 引入 pagetable_t、页大小和页表项类型。 */
#include "memlayout.h"  /* 引入用户高地址特殊映射与内核栈大小。 */

/*
 * trapframe 的字段顺序是 trampoline.S 的二进制接口，不能随意调整。
 * 右侧偏移必须与汇编中的 sd/ld 常量完全一致。
 */
struct trapframe {                         /* 定义用户寄存器现场与陷入所需内核环境。 */
  /*   0 */ uint64 kernel_satp;            /* uservec 要安装的内核 SV39 satp。 */
  /*   8 */ uint64 kernel_sp;              /* 当前进程可信内核栈的栈顶地址。 */
  /*  16 */ uint64 kernel_trap;            /* 保存完寄存器后要跳转的 usertrap C 入口。 */
  /*  24 */ uint64 epc;                    /* 用户程序下一次继续执行的虚拟 PC。 */
  /*  32 */ uint64 kernel_hartid;          /* 当前硬件线程编号，陷入后恢复到 tp。 */
  /*  40 */ uint64 ra;                     /* 用户返回地址寄存器 x1。 */
  /*  48 */ uint64 sp;                     /* 用户栈指针寄存器 x2。 */
  /*  56 */ uint64 gp;                     /* 用户全局指针寄存器 x3。 */
  /*  64 */ uint64 tp;                     /* 用户线程指针寄存器 x4。 */
  /*  72 */ uint64 t0;                     /* 用户临时寄存器 x5。 */
  /*  80 */ uint64 t1;                     /* 用户临时寄存器 x6。 */
  /*  88 */ uint64 t2;                     /* 用户临时寄存器 x7。 */
  /*  96 */ uint64 s0;                     /* 用户被调用者保存寄存器 x8。 */
  /* 104 */ uint64 s1;                     /* 用户被调用者保存寄存器 x9。 */
  /* 112 */ uint64 a0;                     /* 用户参数 0 与系统调用返回值寄存器 x10。 */
  /* 120 */ uint64 a1;                     /* 用户参数寄存器 x11。 */
  /* 128 */ uint64 a2;                     /* 用户参数寄存器 x12。 */
  /* 136 */ uint64 a3;                     /* 用户参数寄存器 x13。 */
  /* 144 */ uint64 a4;                     /* 用户参数寄存器 x14。 */
  /* 152 */ uint64 a5;                     /* 用户参数寄存器 x15。 */
  /* 160 */ uint64 a6;                     /* 用户参数寄存器 x16。 */
  /* 168 */ uint64 a7;                     /* 用户参数 7 与系统调用号寄存器 x17。 */
  /* 176 */ uint64 s2;                     /* 用户被调用者保存寄存器 x18。 */
  /* 184 */ uint64 s3;                     /* 用户被调用者保存寄存器 x19。 */
  /* 192 */ uint64 s4;                     /* 用户被调用者保存寄存器 x20。 */
  /* 200 */ uint64 s5;                     /* 用户被调用者保存寄存器 x21。 */
  /* 208 */ uint64 s6;                     /* 用户被调用者保存寄存器 x22。 */
  /* 216 */ uint64 s7;                     /* 用户被调用者保存寄存器 x23。 */
  /* 224 */ uint64 s8;                     /* 用户被调用者保存寄存器 x24。 */
  /* 232 */ uint64 s9;                     /* 用户被调用者保存寄存器 x25。 */
  /* 240 */ uint64 s10;                    /* 用户被调用者保存寄存器 x26。 */
  /* 248 */ uint64 s11;                    /* 用户被调用者保存寄存器 x27。 */
  /* 256 */ uint64 t3;                     /* 用户临时寄存器 x28。 */
  /* 264 */ uint64 t4;                     /* 用户临时寄存器 x29。 */
  /* 272 */ uint64 t5;                     /* 用户临时寄存器 x30。 */
  /* 280 */ uint64 t6;                     /* 用户临时寄存器 x31。 */
};                                           /* trapframe 总大小为 288 字节。 */

enum procstate {                              /* 列出最小进程生命周期中的所有状态。 */
  PROC_UNUSED,                               /* 槽位空闲，可由 allocproc 重新分配。 */
  PROC_RUNNABLE,                             /* 进程已准备好但尚未成为当前进程。 */
  PROC_RUNNING,                              /* 进程用户镜像和现场当前处于活动状态。 */
  PROC_WAITING,                              /* 父进程正在 wait，等待子进程结束。 */
  PROC_ZOMBIE                                /* 子进程已退出，等待父进程回收状态。 */
};                                           /* 结束进程状态枚举。 */

#define LAB2_NPROC       8                   /* 固定进程表最多容纳 8 个进程。 */
#define LAB2_KSTACK_SIZE (16 * 1024)         /* 每个进程预留 16KB 内核栈。 */

/*
 * Lab3 为每个进程维护独立页表、独立 trapframe 物理页和独立用户物理页。
 * 内核栈继续静态放在 PCB 内，依靠内核页表对 RAM 的恒等映射访问；本实验
 * 未采用 KSTACK 高地址映射，这一取舍在设计笔记中与 Lab0 图纸逐项说明。
 */
struct proc {                                  /* 定义最小进程控制块及其私有存储。 */
  enum procstate state;                       /* 记录槽位当前生命周期状态。 */
  int pid;                                    /* 保存内核分配的正整数进程号。 */
  int xstate;                                 /* 保存 exit 提供给父进程的退出状态。 */
  struct proc *parent;                        /* 指向通过 fork 创建本进程的父进程。 */
  uint64 wait_status_addr;                    /* 保存父进程 wait 的用户状态地址。 */
  uint64 image_size;                          /* 记录当前内嵌程序二进制的实际字节数。 */
  uint64 sz;                                  /* 记录当前用户低地址空间的有效上边界。 */
  uint64 heap_base;                           /* 记录 sbrk 不得收缩越过的初始堆边界。 */
  char name[16];                              /* 保存用于诊断的短程序名。 */
  pagetable_t pagetable;                      /* 指向本进程独立的 SV39 根页表物理页。 */
  struct trapframe *trapframe;                /* 指向本进程独立的 trapframe 物理页。 */
  __attribute__((aligned(16))) uchar kstack[LAB2_KSTACK_SIZE]; /* 为本进程提供 ABI 对齐的内核栈。 */
};                                             /* 结束最小进程控制块定义。 */

void procinit(void);                              /* 初始化进程表并建立最初的 Shell 进程槽。 */
struct proc *myproc(void);                        /* 返回当前活动进程控制块。 */
struct trapframe *active_trapframe(void);         /* 返回 trampoline 映射的活动 trapframe。 */
int proc_exec(const char *name);                  /* 从内嵌表装载程序并重建用户现场。 */
int proc_fork(void);                              /* 复制当前进程镜像和现场并返回子 PID。 */
int proc_wait(uint64 status_addr);                /* 等待或回收子进程并切换到可运行子进程。 */
void proc_exit(int status);                       /* 结束当前子进程并恢复等待父进程。 */
uint64 proc_sbrk(long amount, int policy);         /* 按 EAGER 语义调整当前进程堆边界。 */

#endif /* 结束 LAB3_PROC_H 包含保护范围。 */
