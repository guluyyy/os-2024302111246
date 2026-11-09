#ifndef LAB4_PROC_H                     /* 仅在尚未声明进程结构时进入本文件。 */
#define LAB4_PROC_H                     /* 建立包含保护，避免结构与原型重复。 */

#include "types.h"                     /* 引入 uint、uint64 和 uchar。 */
#include "riscv.h"                     /* 引入 pagetable_t 与页表常量。 */
#include "memlayout.h"                 /* 引入用户高地址布局和页大小。 */
#include "spinlock.h"                  /* 引入每进程锁的完整结构。 */

/* trapframe 字段和偏移必须与 trampoline.S 的 sd/ld 顺序严格一致。 */
struct trapframe {                      /* 保存用户全寄存器现场和陷入内核参数。 */
  uint64 kernel_satp;                  /* 0：陷入后安装的内核页表。 */
  uint64 kernel_sp;                    /* 8：当前进程可信内核栈顶。 */
  uint64 kernel_trap;                  /* 16：保存寄存器后进入的 C 函数。 */
  uint64 epc;                          /* 24：返回用户态时写入 sepc 的 PC。 */
  uint64 kernel_hartid;                /* 32：陷入后恢复到 tp 的 hart 编号。 */
  uint64 ra, sp, gp, tp, t0, t1, t2;   /* 40..88：用户通用寄存器。 */
  uint64 s0, s1, a0, a1, a2, a3, a4, a5, a6, a7; /* 96..168。 */
  uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11; /* 176..248。 */
  uint64 t3, t4, t5, t6;               /* 256..280：其余用户临时寄存器。 */
};                                      /* trapframe 总大小保持 288 字节。 */

/* swtch.S 只保存被调用者保存寄存器；字段顺序对应 0..104 的固定偏移。 */
struct context {                        /* 保存内核执行流之间切换所需的最小现场。 */
  uint64 ra;                           /* 恢复后由 ret 跳转到的地址。 */
  uint64 sp;                           /* 对应进程栈或调度器栈的栈指针。 */
  uint64 s0, s1, s2, s3, s4, s5;      /* RISC-V ABI 被调用者保存寄存器。 */
  uint64 s6, s7, s8, s9, s10, s11;    /* RISC-V ABI 被调用者保存寄存器。 */
};                                      /* 字段布局与赠送的 swtch.S 严格对应。 */

enum procstate {                        /* 列出 PCB 从空闲到僵尸的全部合法状态。 */
  PROC_UNUSED,                         /* PCB 槽位可重新分配。 */
  PROC_USED,                           /* 已占用但尚未对调度器可见。 */
  PROC_SLEEPING,                       /* 正在指定 chan 上睡眠。 */
  PROC_RUNNABLE,                       /* 已就绪，等待调度器选择。 */
  PROC_RUNNING,                        /* 当前正在 CPU 上执行。 */
  PROC_ZOMBIE                          /* 已退出，等待父进程 wait 回收。 */
};                                      /* 完成进程状态机枚举。 */

#define LAB4_NPROC       64            /* bench 需要父进程和至少 8 个并发子进程。 */
#define LAB4_KSTACK_SIZE (16 * 1024)   /* 每进程独立 16 KiB 内核栈。 */
#define LAB4_STRIDE_BIG  1000U         /* stride=1000/tickets 的课程约定分子。 */

struct proc {                           /* 描述一个进程的同步、调度和地址空间状态。 */
  struct spinlock lock;                /* 保护 state、chan、killed 和调度统计。 */
  enum procstate state;                /* 当前生命周期状态。 */
  void *chan;                          /* 非空时表示睡眠等待的条件地址。 */
  int killed;                          /* kill 请求标志，由安全返回点处理。 */
  int xstate;                          /* exit 状态，供父进程 wait 读取。 */
  int pid;                             /* 单调分配的正整数进程号。 */
  struct proc *parent;                 /* 只在持有 wait_lock 时读写。 */
  struct context context;              /* swtch 保存的内核执行上下文。 */
  uint cpu_ticks;                      /* 用户态被时钟打断的累计次数。 */
  uint last_slice_ticks;               /* 本次被调度后累计的用户 tick。 */
  int tickets;                         /* Stride 权重；值越大获得 CPU 越多。 */
  uint stride;                         /* LAB4_STRIDE_BIG/tickets，至少为 1。 */
  uint64 pass;                         /* Stride 虚拟运行时间，选择最小者。 */
  uint64 image_size;                   /* 内嵌程序实际二进制长度。 */
  uint64 sz;                           /* 用户低地址有效区上边界。 */
  uint64 heap_base;                    /* sbrk 不得收缩越过的初始 break。 */
  char name[16];                       /* 诊断用短程序名。 */
  pagetable_t pagetable;               /* 每进程独立 SV39 根页表。 */
  struct trapframe *trapframe;         /* 每进程独立用户寄存器现场页。 */
  __attribute__((aligned(16))) uchar kstack[LAB4_KSTACK_SIZE]; /* 可信内核栈。 */
};                                      /* 完成单个 PCB 的内存布局。 */

struct cpu {                            /* 保存当前 hart 的调度器私有状态。 */
  struct proc *proc;                   /* 当前 CPU 上运行的进程；调度器中为空。 */
  struct context context;              /* 调度器自身被 swtch 保存的上下文。 */
  int noff;                            /* push_off 的嵌套层数。 */
  int intena;                          /* 最外层 push_off 前的中断状态。 */
};                                      /* 完成单 CPU 状态结构定义。 */

void procinit(void);                   /* 初始化 PCB 锁、wait_lock 和 CPU 状态。 */
struct proc *proc_userinit(const char *name); /* 建立首个可运行用户进程。 */
struct cpu *mycpu(void);               /* 返回当前 hart 的 CPU 状态。 */
struct proc *myproc(void);             /* 关闭中断后安全读取当前进程。 */
struct trapframe *active_trapframe(void); /* 返回当前进程 trapframe。 */
void scheduler(void) __attribute__((noreturn)); /* 永久执行 Stride 调度循环。 */
void sched(void);                      /* 持 p->lock 从进程切回调度器。 */
void yield(void);                      /* 把当前进程改回 RUNNABLE 并让出 CPU。 */
void sleep(void *chan, struct spinlock *lk); /* 原子释放条件锁并睡眠。 */
void wakeup(void *chan);               /* 唤醒指定条件上的所有进程。 */
int proc_exec(const char *name);       /* 原子替换当前用户地址空间。 */
int proc_fork(void);                   /* 深拷贝当前进程。 */
int proc_wait(uint64 status_addr);     /* 睡眠等待并回收一个子进程。 */
void proc_exit(int status) __attribute__((noreturn)); /* 退出并成为僵尸。 */
uint64 proc_sbrk(long amount, int policy); /* 调整当前进程堆边界。 */
int proc_kill(int pid);                /* 标记目标被杀并唤醒睡眠目标。 */
int proc_schedstat(int pid, int *cpu_ticks, int *slice_ticks); /* 读取统计快照。 */
int proc_setickets(int tickets);       /* 修改当前进程 Stride 权重。 */

#endif                                  /* 结束 LAB4_PROC_H 包含保护。 */
