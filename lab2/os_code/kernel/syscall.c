/* Lab2 系统调用 ABI 与分发表。未知或未实现调用统一返回 -1。 */
#include "types.h"    /* 引入 uint64 等固定宽度整数类型。 */
#include "riscv.h"    /* 引入 USER_BASE 等间接依赖的 RISC-V 定义。 */
#include "syscall.h"  /* 引入课程固定的 SYS_* 系统调用编号。 */
#include "proc.h"     /* 引入进程、trapframe 和用户地址检查接口。 */
#include "defs.h"     /* 引入控制台、tick 与字符串复制函数原型。 */

/* 函数作用：按参数序号从活动 trapframe 的 a0 至 a5 读取原始参数。 */
static uint64                         /* 返回未经类型解释的 64 位系统调用参数。 */
argraw(int index)                    /* 根据从零开始的参数序号读取 a0 至 a5。 */
{                                    /* 开始从活动 trapframe 解包参数。 */
  struct trapframe *tf = active_trapframe(); /* 取得刚由 uservec 保存的当前用户现场。 */
  switch (index) {                   /* 根据 ABI 参数位置选择对应的 a 寄存器。 */
  case 0: return tf->a0;             /* 第 0 个参数来自 a0。 */
  case 1: return tf->a1;             /* 第 1 个参数来自 a1。 */
  case 2: return tf->a2;             /* 第 2 个参数来自 a2。 */
  case 3: return tf->a3;             /* 第 3 个参数来自 a3。 */
  case 4: return tf->a4;             /* 第 4 个参数来自 a4。 */
  case 5: return tf->a5;             /* 第 5 个参数来自 a5。 */
  default: return 0;                 /* 超出本实验 ABI 范围时返回确定值 0。 */
  }                                  /* 结束参数序号分派。 */
}                                    /* 结束原始参数提取。 */

/* 函数作用：读取指定系统调用参数并按 C int 类型解释。 */
static int                            /* 返回按 C int 解释后的系统调用参数。 */
argint(int index)                    /* 提取指定位置的整数参数。 */
{                                    /* 开始执行整数参数转换。 */
  return (int)argraw(index);         /* 读取 64 位寄存器并保留其低位作为 int。 */
}                                    /* 结束整数参数提取。 */

/* 函数作用：读取指定系统调用参数并按用户虚拟地址解释。 */
static uint64                         /* 用户地址保持 64 位无符号表示。 */
argaddr(int index)                   /* 提取指定位置的用户虚拟地址参数。 */
{                                    /* 开始执行地址参数提取。 */
  return argraw(index);              /* 原样返回寄存器中的 64 位地址值。 */
}                                    /* 结束地址参数提取。 */

/* 函数作用：验证 write 参数并把用户缓冲区内容输出到控制台。 */
static int                            /* 返回成功写出的字节数或错误码 -1。 */
sys_write_impl(void)                 /* 实现 write(fd, user_buffer, count)。 */
{                                    /* 开始验证参数并执行控制台输出。 */
  int fd = argint(0);                /* 从 a0 取得文件描述符。 */
  uint64 address = argaddr(1);       /* 从 a1 取得用户缓冲区虚拟地址。 */
  int count = argint(2);             /* 从 a2 取得请求输出的字节数。 */

  if ((fd != 1 && fd != 2) || count < 0) /* 本实验只接受 stdout/stderr，长度不能为负。 */
    return -1;                       /* 参数不符合接口约定时返回错误。 */
  if (!user_range_ok(address, (uint64)count)) /* 检查整个用户缓冲区是否落在允许范围。 */
    return -1;                       /* 拒绝越界或发生加法溢出的用户地址。 */
  return console_write((const char *)(USER_BASE + address), count); /* 把用户 VA 转为执行区 PA 后输出。 */
}                                    /* 结束 write 系统调用实现。 */

/* 函数作用：验证 read 参数并把控制台输入复制到用户缓冲区。 */
static int                            /* 返回实际读入字节数或错误码 -1。 */
sys_read_impl(void)                  /* 实现 read(fd, user_buffer, count)。 */
{                                    /* 开始验证参数并消费输入缓冲区。 */
  int fd = argint(0);                /* 从 a0 取得文件描述符。 */
  uint64 address = argaddr(1);       /* 从 a1 取得用户目标缓冲区虚拟地址。 */
  int count = argint(2);             /* 从 a2 取得最多读取的字节数。 */

  if (fd != 0 || count < 0)          /* 本实验只允许从标准输入读取且长度不能为负。 */
    return -1;                       /* 文件描述符或长度非法时返回错误。 */
  if (!user_range_ok(address, (uint64)count)) /* 检查写入用户缓冲区是否会越界。 */
    return -1;                       /* 拒绝不安全的用户地址范围。 */
  return console_read((char *)(USER_BASE + address), count); /* 把用户 VA 转为执行区 PA 后填入字符。 */
}                                    /* 结束 read 系统调用实现。 */

/* 函数作用：复制用户程序名并通过内嵌程序表替换当前进程镜像。 */
static int                            /* 返回 exec 成功状态或错误码 -1。 */
sys_exec_impl(void)                  /* 实现 exec(name, argv) 的 Lab2 简化版本。 */
{                                    /* 开始复制并查找用户给出的程序名。 */
  char name[64];                     /* 在内核栈上准备有界程序名缓冲区。 */
  if (copy_user_string(name, argaddr(0), sizeof(name)) < 0) /* 从用户地址复制 NUL 结尾程序名。 */
    return -1;                       /* 字符串越界或过长时拒绝执行。 */
  /* argv 在 lab2 允许忽略；接口仍保留完整 exec(name, argv) 形态。 */
  return proc_exec(name);            /* 在内嵌程序表中查找并替换当前用户镜像。 */
}                                    /* 结束 exec 系统调用实现。 */

/* 函数作用：根据系统 tick 实现 Lab2 阶段的简化忙等 pause。 */
static int                            /* 成功等待后返回 0，非法参数返回 -1。 */
sys_pause_impl(void)                 /* 实现按 tick 数忙等的 pause。 */
{                                    /* 开始计算等待区间。 */
  int requested = argint(0);         /* 从 a0 取得请求等待的 tick 数。 */
  uint64 start;                      /* 保存开始等待时的系统 tick。 */

  if (requested < 0)                 /* 负等待时间没有定义。 */
    return -1;                       /* 对非法等待时间返回错误。 */
  start = ticks_read();              /* 记录等待开始时刻。 */
  while (ticks_read() - start < (uint64)requested) /* 在累计差值达到请求值前持续检查。 */
    ;                                /* Lab2 尚无 sleep/wakeup，因此循环体为空并忙等。 */
  return 0;                          /* 达到等待时长后报告成功。 */
}                                    /* 结束 pause 系统调用实现。 */

/* 函数作用：依据用户 a7 中的调用号分发系统调用并把结果写回 a0。 */
void                                  /* 分发器通过 trapframe 写回返回值，不直接返回 C 值。 */
syscall(void)                         /* 根据用户 a7 中的编号执行相应内核服务。 */
{                                     /* 开始一次系统调用分发。 */
  struct proc *caller = myproc();     /* 记住发起调用的进程，防止切换后写错现场。 */
  struct trapframe *tf = active_trapframe(); /* 取得当前用户寄存器保存区。 */
  int number = (int)tf->a7;           /* 按 RISC-V ABI 从 a7 取得系统调用号。 */
  int result = -1;                    /* 默认结果为失败，覆盖未知或未实现调用。 */

  switch (number) {                   /* 按课程 syscall.h 的固定编号选择实现。 */
  case SYS_fork:                      /* 用户请求复制当前进程。 */
    result = proc_fork();             /* 创建子进程并取得子进程 PID 或错误码。 */
    break;                            /* 结束 fork 分支。 */
  case SYS_exit:                      /* 用户请求终止当前进程。 */
    proc_exit(argint(0));             /* 记录 a0 中的退出状态并恢复等待的父进程。 */
    break;                            /* 仅为控制流完整性保留；正常 proc_exit 会切换现场。 */
  case SYS_wait:                      /* 父进程请求等待一个子进程。 */
    result = proc_wait(argaddr(0));   /* 传入状态保存地址并取得子进程 PID。 */
    break;                            /* 结束 wait 分支。 */
  case SYS_read:                      /* 用户请求从标准输入读取。 */
    result = sys_read_impl();         /* 验证参数并从控制台环形缓冲区取数据。 */
    break;                            /* 结束 read 分支。 */
  case SYS_exec:                      /* 用户请求执行一个内嵌程序。 */
    result = sys_exec_impl();         /* 复制程序名并替换当前进程用户镜像。 */
    break;                            /* 结束 exec 分支。 */
  case SYS_getpid:                    /* 用户请求当前进程编号。 */
    result = myproc()->pid;           /* 直接读取当前进程结构中的 PID。 */
    break;                            /* 结束 getpid 分支。 */
  case SYS_pause:                     /* 用户请求等待若干 tick。 */
    result = sys_pause_impl();        /* 使用 tick 计数完成简化忙等。 */
    break;                            /* 结束 pause 分支。 */
  case SYS_uptime:                    /* 用户请求系统启动后的 tick 数。 */
    result = (int)ticks_read();       /* 原子读取累计 tick 并转换为接口返回类型。 */
    break;                            /* 结束 uptime 分支。 */
  case SYS_write:                     /* 用户请求向标准输出或错误输出写数据。 */
    result = sys_write_impl();        /* 验证用户缓冲区后交给控制台输出。 */
    break;                            /* 结束 write 分支。 */
  default:                            /* 捕获所有未知和本阶段未实现的调用号。 */
    /* 其余定义过但本阶段未实现的调用，与未知调用一样确定返回 -1。 */
    result = -1;                      /* 按实验合同给出确定的失败返回值。 */
    break;                            /* 结束默认分支。 */
  }                                   /* 结束系统调用号分派。 */

  /* wait/exit 可能已经切换到另一个进程，不得污染新进程的 a0。 */
  if (myproc() == caller)             /* 只有当前进程仍是调用者时才写它的返回寄存器。 */
    active_trapframe()->a0 = (uint64)(long)result; /* 把有符号结果按 ABI 写回用户 a0。 */
}                                     /* 结束一次系统调用分发。 */
