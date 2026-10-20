/* Lab3 系统调用分发：所有用户指针均通过页表受控复制。 */
#include "types.h"
#include "riscv.h"
#include "syscall.h"
#include "vm.h"
#include "proc.h"
#include "defs.h"

/* 函数作用：按 ABI 从保存的 a0 至 a5 提取原始参数。 */
static uint64 /* 声明下方函数的返回类型与可见范围。 */
argraw(int index) /* 函数作用：从陷阱帧提取原始系统调用参数。 */
{ /* 开始当前结构或代码块。 */
  struct trapframe *tf = active_trapframe(); /* 取得保存系统调用参数的当前陷阱帧。 */
  switch (index) { /* 根据编号选择对应的系统调用参数或处理分支。 */
  case 0: return tf->a0; /* 匹配当前编号并执行对应处理。 */
  case 1: return tf->a1; /* 匹配当前编号并执行对应处理。 */
  case 2: return tf->a2; /* 匹配当前编号并执行对应处理。 */
  case 3: return tf->a3; /* 匹配当前编号并执行对应处理。 */
  case 4: return tf->a4; /* 匹配当前编号并执行对应处理。 */
  case 5: return tf->a5; /* 匹配当前编号并执行对应处理。 */
  default: return 0; /* 处理所有未定义编号并保持安全失败语义。 */
  } /* 结束当前条件、循环或函数代码块。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：把系统调用参数解释为 int。 */
static int /* 声明下方函数的返回类型与可见范围。 */
argint(int index) /* 函数作用：把系统调用参数解释为有符号整数。 */
{ /* 开始当前结构或代码块。 */
  return (int)argraw(index); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：把系统调用参数解释为用户虚拟地址。 */
static uint64 /* 声明下方函数的返回类型与可见范围。 */
argaddr(int index) /* 函数作用：把系统调用参数解释为用户虚拟地址。 */
{ /* 开始当前结构或代码块。 */
  return argraw(index); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：经 copyin 分块输出用户缓冲区，杜绝直接解引用用户指针。 */
static int /* 声明下方函数的返回类型与可见范围。 */
sys_write_impl(void) /* 函数作用：经copyin安全完成用户写系统调用。 */
{ /* 开始当前结构或代码块。 */
  int fd = argint(0); /* 取得用户传入的文件描述符。 */
  uint64 address = argaddr(1); /* 保存待检查的物理或用户地址。 */
  int count = argint(2); /* 取得用户请求处理的总字节数。 */
  int done = 0; /* 累计已经完成的写入字节数。 */
  char buffer[128]; /* 声明内核中转缓冲区，禁止直接解引用用户指针。 */

  if ((fd != 1 && fd != 2) || count < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  while (done < count) { /* 在剩余数据或条件有效时继续循环处理。 */
    int amount = count - done; /* 保存本轮不跨页的复制字节数。 */
    if (amount > (int)sizeof(buffer)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      amount = sizeof(buffer); /* 把单轮复制限制为128字节，避免内核栈缓冲区越界。 */
    if (copyin(myproc()->pagetable, buffer, address + done, amount) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    if (console_write(buffer, amount) != amount) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    done += amount; /* 累加本轮已输出字节，下一轮从后续用户地址继续。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return done; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：先读入内核缓冲区，再经 copyout 安全写入用户页。 */
static int /* 声明下方函数的返回类型与可见范围。 */
sys_read_impl(void) /* 函数作用：经copyout安全完成用户读系统调用。 */
{ /* 开始当前结构或代码块。 */
  int fd = argint(0); /* 取得用户传入的文件描述符。 */
  uint64 address = argaddr(1); /* 保存待检查的物理或用户地址。 */
  int count = argint(2); /* 取得用户请求处理的总字节数。 */
  int amount; /* 保存本轮不跨页的复制字节数。 */
  int got; /* 保存控制台实际读取的字节数。 */
  char buffer[128]; /* 声明内核中转缓冲区，禁止直接解引用用户指针。 */

  if (fd != 0 || count < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  if (count == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
  amount = count; /* 默认本轮尝试读取用户请求的全部字节。 */
  if (amount > (int)sizeof(buffer)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    amount = sizeof(buffer); /* 用户请求过大时只读取内核中转缓冲区可容纳的部分。 */
  got = console_read(buffer, amount); /* 从控制台输入缓冲区读取数据。 */
  if (got < 0 || copyout(myproc()->pagetable, address, buffer, got) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  return got; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：安全复制用户程序名并以新页表原子替换当前程序。 */
static int /* 声明下方函数的返回类型与可见范围。 */
sys_exec_impl(void) /* 函数作用：复制用户程序名并执行exec。 */
{ /* 开始当前结构或代码块。 */
  char name[64]; /* 声明内核侧有界程序名缓冲区。 */

  if (copyinstr(myproc()->pagetable, name, argaddr(0), sizeof(name)) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  return proc_exec(name);            /* Lab3 仍暂不解析 argv。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：按系统 tick 实现 Lab2 兼容的忙等 pause。 */
static int /* 声明下方函数的返回类型与可见范围。 */
sys_pause_impl(void) /* 函数作用：按系统tick完成忙等暂停。 */
{ /* 开始当前结构或代码块。 */
  int requested = argint(0); /* 取得用户请求等待的tick数量。 */
  uint64 start; /* 保存区间起点、计时起点或镜像起点。 */

  if (requested < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  start = ticks_read(); /* 读取稳定的系统tick计数。 */
  while (ticks_read() - start < (uint64)requested) /* 在剩余数据或条件有效时继续循环处理。 */
    ; /* 保持忙等，直到外部条件发生变化。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：执行 EAGER sbrk；LAZY 请求在 Lab3 同样按 EAGER 处理。 */
static uint64 /* 声明下方函数的返回类型与可见范围。 */
sys_sbrk_impl(void) /* 函数作用：解析sbrk参数并调整当前进程堆。 */
{ /* 开始当前结构或代码块。 */
  long amount = (long)(int)argraw(0); /* 用户接口参数为 int，先有符号扩展。 */
  int policy = argint(1); /* 取得用户选择的sbrk分配策略。 */
  return proc_sbrk(amount, policy); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：按 a7 分发系统调用，并把同一进程的结果写回保存的 a0。 */
void /* 声明下方函数的返回类型与可见范围。 */
syscall(void) /* 函数作用：按照a7中的编号分发系统调用。 */
{ /* 开始当前结构或代码块。 */
  struct proc *caller = myproc(); /* 保存进入系统调用时的进程，防止切换后污染现场。 */
  struct trapframe *tf = active_trapframe(); /* 取得保存系统调用参数的当前陷阱帧。 */
  int number = (int)tf->a7; /* 从a7取得系统调用编号。 */
  uint64 result = (uint64)-1; /* 预置系统调用失败返回值。 */

  switch (number) { /* 根据编号选择对应的系统调用参数或处理分支。 */
  case SYS_fork: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)(long)proc_fork(); /* 创建深拷贝子进程，并把子PID或-1保存为返回值。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_exit: /* 定义失败回滚或分支跳转目标。 */
    proc_exit(argint(0)); /* 函数作用：结束子进程并恢复等待中的父进程。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_wait: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)(long)proc_wait(argaddr(0)); /* 等待子进程，并把退出状态写到用户传入地址。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_read: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)(long)sys_read_impl(); /* 执行经过copyout保护的控制台读取。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_exec: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)(long)sys_exec_impl(); /* 安全读取程序名并原子替换当前地址空间。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_getpid: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)myproc()->pid; /* 直接返回当前PCB中由内核分配的进程号。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_sbrk: /* 定义失败回滚或分支跳转目标。 */
    result = sys_sbrk_impl(); /* 调整当前堆边界并返回旧break或错误值。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_pause: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)(long)sys_pause_impl(); /* 忙等到请求的tick数后返回。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_uptime: /* 定义失败回滚或分支跳转目标。 */
    result = ticks_read(); /* 读取稳定的系统tick计数。 */
    break; /* 结束当前循环或switch分支。 */
  case SYS_write: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)(long)sys_write_impl(); /* 执行经过copyin保护的控制台写入。 */
    break; /* 结束当前循环或switch分支。 */
  default: /* 定义失败回滚或分支跳转目标。 */
    result = (uint64)-1; /* 未知系统调用统一返回-1，满足badecall回归语义。 */
    break; /* 结束当前循环或switch分支。 */
  } /* 结束当前条件、循环或函数代码块。 */

  /* wait/exit 可能换了 current，此时不得污染新进程的返回寄存器。 */
  if (myproc() == caller) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    active_trapframe()->a0 = result; /* 函数作用：取得当前进程的独立陷阱帧。 */
} /* 结束当前条件、循环或函数代码块。 */
