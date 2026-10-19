/*
 * Lab2 最小进程层。
 *
 * 这里没有每进程独立页表和真正的内核调度器。所有用户程序都在 USER_BASE 执行，
 * 进程切换时保存/恢复整段用户内存及 trapframe。该方案足以支持课程
 * Shell 的 fork -> child exec -> exit -> parent wait 流程，同时明确把
 * 地址空间隔离和高效上下文切换留给后续实验。
 */
#include "types.h"      /* 引入 uint64、uchar 等基础数据类型。 */
#include "memlayout.h"  /* 引入固定用户执行区和活动 trapframe 地址。 */
#include "proc.h"       /* 引入进程控制块、状态和公开接口。 */
#include "defs.h"       /* 引入内存、字符串与 panic 帮助函数。 */

static struct proc procs[LAB2_NPROC]; /* 静态分配整个最小进程表。 */
static struct proc *current;          /* 指向其镜像当前装在 USER_BASE 的进程。 */
static int next_pid = 1;              /* 保存下一个可分配 PID，从 1 开始。 */

/* Makefile 生成的内嵌程序表起点。 */
extern uchar _uprog_table[];          /* 引用 userimg.S 导出的内嵌程序表首地址。 */

struct uprog_header {                 /* 描述程序表中每个变长记录的固定头部。 */
  uint64 start;                       /* 保存内嵌程序二进制起始内核地址。 */
  uint64 end;                         /* 保存内嵌程序二进制末尾后一字节地址。 */
  char name[1];                       /* 表示紧跟头部的 NUL 结尾变长程序名。 */
};                                    /* 头部之后按 8 字节对齐存放程序数据。 */

/* 函数作用：取得 trampoline 当前读写的固定活动 trapframe。 */
struct trapframe *                    /* 返回 trampoline 当前读写的现场指针。 */
active_trapframe(void)                /* 获取固定活动 trapframe 物理区。 */
{                                     /* 开始计算活动现场地址。 */
  return (struct trapframe *)ACTIVE_TRAPFRAME_PA; /* 把固定物理地址解释为 trapframe 指针。 */
}                                     /* 结束活动现场查询。 */

/* 函数作用：返回当前占用固定用户执行区的进程控制块。 */
struct proc *                         /* 返回当前进程控制块指针。 */
myproc(void)                          /* 查询当前加载到执行区的进程。 */
{                                     /* 开始当前进程查询。 */
  return current;                     /* 直接返回单核全局 current 指针。 */
}                                     /* 结束当前进程查询。 */

/* 函数作用：把程序名安全复制到进程控制块的定长名称字段。 */
static void                           /* 名称复制只写目标数组，不返回结果。 */
copy_name(char dst[16], const char *src) /* 把程序名安全截断到 15 个字符。 */
{                                     /* 开始有界复制进程名。 */
  int i = 0;                          /* 从目标数组第 0 个字符开始。 */
  while (i < 15 && src[i] != '\0') {  /* 保留终止符空间且源串未结束时继续。 */
    dst[i] = src[i];                  /* 复制当前位置的程序名字符。 */
    i++;                              /* 前进到下一个字符位置。 */
  }                                   /* 完成源串复制或达到长度上限。 */
  dst[i] = '\0';                      /* 无条件补 NUL，保证目标始终是合法字符串。 */
}                                     /* 结束进程名复制。 */

/* 把 Shell 输入规范化为程序表名：去掉首尾空白与回车换行。 */
/* 函数作用：去掉 Shell 输入中的首尾空白和回车换行，得到程序表名称。 */
static void                            /* 规范化结果通过 out 数组返回。 */
normalize_name(char out[32], const char *in) /* 去掉输入首尾空白和行结束符。 */
{                                      /* 开始寻找有效程序名区间。 */
  int begin = 0;                       /* 保存第一个非前导空白字符下标。 */
  int end;                             /* 保存有效名称末尾后一字符下标。 */
  int i;                               /* 保存复制到输出数组的下标。 */

  while (in[begin] == ' ' || in[begin] == '\t') /* 跳过 Shell 输入开头的空格和制表符。 */
    begin++;                          /* 推进到第一个候选名称字符。 */
  end = begin;                        /* 从有效区间起点开始搜索末尾。 */
  while (in[end] != '\0' && in[end] != '\n' && in[end] != '\r') /* 扫描到字符串或换行结尾。 */
    end++;                            /* 扩展候选名称区间。 */
  while (end > begin && (in[end - 1] == ' ' || in[end - 1] == '\t')) /* 删除末尾水平空白。 */
    end--;                            /* 向前收缩有效区间末端。 */

  for (i = 0; i < 31 && begin < end; i++, begin++) /* 最多复制 31 个有效字符。 */
    out[i] = in[begin];               /* 把当前有效输入字符写入输出。 */
  out[i] = '\0';                      /* 追加 NUL，形成规范化 C 字符串。 */
}                                      /* 结束程序名规范化。 */

/*
 * 表项后紧跟本程序二进制，下一表项位于 end 向上 8 字节对齐处。
 * 这与 Makefile.upgrade 生成 userimg.S 的顺序完全一致。
 */
/* 函数作用：遍历内嵌程序表，查找指定程序的二进制起点和大小。 */
static int                                      /* 找到返回 0，未找到或表损坏返回 -1。 */
find_uprog(const char *requested, const uchar **start, uint64 *size) /* 在变长内嵌表中查程序。 */
{                                               /* 开始顺序遍历内嵌程序记录。 */
  char wanted[32];                              /* 保存去掉空白后的目标程序名。 */
  uchar *cursor = _uprog_table;                 /* 从表首记录开始解析。 */

  normalize_name(wanted, requested);            /* 先把 Shell 输入转换为精确表项名称。 */
  for (;;) {                                    /* 持续检查记录，直到命中或遇到终止项。 */
    struct uprog_header *entry = (struct uprog_header *)cursor; /* 把当前位置解释为记录头。 */
    if (entry->start == 0 && entry->end == 0)   /* 两个零地址是 Makefile 生成的表终止标记。 */
      return -1;                                /* 到表尾仍未命中，报告程序不存在。 */
    if (entry->end < entry->start)              /* 末地址小于首地址说明表数据已经损坏。 */
      return -1;                                /* 拒绝用下溢长度继续复制。 */
    if (strcmp(entry->name, wanted) == 0) {     /* 比较当前记录名与请求名。 */
      *start = (const uchar *)entry->start;     /* 向调用者返回程序二进制首地址。 */
      *size = entry->end - entry->start;        /* 用首尾地址差计算程序字节数。 */
      return 0;                                 /* 报告程序查找成功。 */
    }                                           /* 结束当前记录名称匹配分支。 */
    cursor = (uchar *)((entry->end + 7) & ~7ULL); /* 从程序末端向上 8 字节对齐到下一记录。 */
  }                                             /* 结束内嵌程序表循环。 */
}                                               /* 结束内嵌程序查找。 */

/* 函数作用：在线性进程表中申请并初始化一个空闲进程槽。 */
static struct proc *                           /* 成功返回新槽指针，失败返回空指针。 */
allocproc(void)                                /* 在线性进程表中申请一个空闲槽位。 */
{                                              /* 开始扫描固定进程表。 */
  int i;                                       /* 保存当前检查的进程表下标。 */
  for (i = 0; i < LAB2_NPROC; i++) {           /* 依次检查所有可用槽位。 */
    if (procs[i].state == PROC_UNUSED) {       /* 只选择标记为空闲的槽位。 */
      memset(&procs[i], 0, sizeof(procs[i]));  /* 清除上一个进程留下的全部数据。 */
      procs[i].pid = next_pid++;               /* 分配唯一 PID 并推进全局序号。 */
      procs[i].state = PROC_RUNNABLE;          /* 把新槽标记为已创建且可运行。 */
      return &procs[i];                        /* 返回初始化后的进程控制块。 */
    }                                          /* 结束当前槽位是否空闲的判断。 */
  }                                            /* 完成整个进程表扫描。 */
  return 0;                                    /* 没有空闲槽时用空指针表示失败。 */
}                                              /* 结束进程槽分配。 */

/* 函数作用：把当前活动 trapframe 和用户内存保存到当前进程快照。 */
static void                                    /* 保存操作更新 current，不返回结果。 */
save_current(void)                             /* 把活动现场与用户区快照保存回当前 PCB。 */
{                                              /* 开始保存当前进程。 */
  if (current == 0)                            /* 启动早期可能尚未建立当前进程。 */
    return;                                    /* 无当前进程时无需保存。 */
  current->tf = *active_trapframe();           /* 复制全部活动用户寄存器现场。 */
  memmove(current->image, (void *)USER_BASE, USER_MEM_SIZE); /* 复制完整固定用户执行区。 */
}                                              /* 结束当前进程保存。 */

/* 函数作用：把指定进程的 trapframe 和用户内存快照恢复到活动区域。 */
static void                                    /* 装载操作切换全局当前进程，不返回结果。 */
load_current(struct proc *next)                /* 把指定进程快照恢复到固定活动区域。 */
{                                              /* 开始装载下一个进程。 */
  current = next;                              /* 先更新当前 PCB，使后续查询指向 next。 */
  memmove((void *)USER_BASE, next->image, USER_MEM_SIZE); /* 恢复 next 的完整用户内存。 */
  *active_trapframe() = next->tf;              /* 恢复 next 的全部用户寄存器现场。 */
  next->state = PROC_RUNNING;                  /* 标记该进程现在占用活动执行区。 */
}                                              /* 结束进程装载。 */

/* 函数作用：初始化进程表、活动用户区和最初的 Shell 进程槽。 */
void                                           /* 进程初始化建立全局状态，不返回结果。 */
procinit(void)                                 /* 清空进程层并创建最初的 Shell 进程槽。 */
{                                              /* 开始初始化固定进程资源。 */
  memset(procs, 0, sizeof(procs));             /* 把所有进程槽初始化为 PROC_UNUSED。 */
  memset((void *)USER_BASE, 0, USER_MEM_SIZE); /* 清空共享的物理用户执行区。 */
  memset((void *)ACTIVE_TRAPFRAME_PA, 0, sizeof(struct trapframe)); /* 清空活动 trapframe。 */

  current = allocproc();                       /* 从刚清空的表中申请第一个进程。 */
  if (current == 0)                            /* 固定进程表此时理论上必有空槽。 */
    panic("procinit: no process slot");         /* 若申请仍失败则说明内核状态损坏。 */
  current->state = PROC_RUNNING;               /* 第一个进程直接成为当前活动进程。 */
  copy_name(current->name, "sh");              /* 先为该槽记录即将装载的 Shell 名称。 */
}                                              /* 结束进程子系统初始化。 */

/* 函数作用：从内嵌程序表装载目标程序，并构造全新的用户第一现场。 */
int                                            /* 成功装载返回 0，失败返回 -1。 */
proc_exec(const char *name)                    /* 用内嵌程序替换当前进程用户镜像。 */
{                                              /* 开始执行程序查找与现场重建。 */
  const uchar *image;                          /* 接收程序二进制在内核镜像中的首地址。 */
  uint64 size;                                 /* 接收程序二进制的字节长度。 */
  struct trapframe *tf = active_trapframe();   /* 取得即将重建的当前活动现场。 */
  char normalized[32];                         /* 保存用于进程名字段的规范化名称。 */

  if (find_uprog(name, &image, &size) < 0)     /* 在编译期内嵌表中查找目标程序。 */
    return -1;                                 /* 表中无此名称时保持调用者并报告失败。 */
  if (size == 0 || size > USER_MEM_SIZE - 4096) /* 要求镜像非空且至少为用户栈留出 4KB。 */
    return -1;                                 /* 拒绝空镜像或超过固定用户区的镜像。 */

  memset((void *)USER_BASE, 0, USER_MEM_SIZE); /* 清除旧程序代码、数据和用户栈。 */
  memmove((void *)USER_BASE, image, size);     /* 把目标平铺二进制复制到固定执行区。 */
  memset(tf, 0, sizeof(*tf));                  /* 清除旧程序寄存器，构造全新第一现场。 */

  /* 用户栈向低地址增长，初值按 RISC-V ABI 做 16 字节对齐。 */
  tf->epc = 0;                                 /* 用户程序链接入口为虚拟地址 0。 */
  tf->sp = USER_MEM_SIZE & ~15ULL;             /* 把用户栈顶设到空间末端并按 16 字节对齐。 */
  tf->a0 = 0;                                  /* Lab2 忽略 argc，因此首次 a0 设为 0。 */
  tf->a1 = 0;                                  /* Lab2 忽略 argv，因此首次 a1 设为空。 */
  current->image_size = size;                  /* 记录当前程序实际镜像长度用于诊断。 */
  normalize_name(normalized, name);            /* 去掉调用者程序名中的首尾空白。 */
  copy_name(current->name, normalized);        /* 把规范化名称写入固定长度进程名。 */
  return 0;                                    /* 报告当前进程镜像替换成功。 */
}                                              /* 结束 exec 实现。 */

/* 函数作用：复制当前用户镜像和寄存器现场，创建一个可运行子进程。 */
int                                            /* 父进程得到子 PID，失败得到 -1。 */
proc_fork(void)                                /* 复制当前进程，创建可运行子进程。 */
{                                              /* 开始构造 fork 子进程。 */
  struct proc *child = allocproc();            /* 先从固定表中申请一个干净子进程槽。 */
  if (child == 0)                              /* 空指针表示没有剩余进程槽。 */
    return -1;                                 /* 无资源时不修改父进程并报告失败。 */

  /* 当前用户内存仍在 USER_BASE，直接复制得到 fork 时刻的完整快照。 */
  memmove(child->image, (void *)USER_BASE, USER_MEM_SIZE); /* 复制 fork 时刻全部用户内存。 */
  child->tf = *active_trapframe();              /* 复制 fork 时刻全部用户寄存器现场。 */
  child->tf.a0 = 0;                             /* 按 fork 语义让子进程看到返回值 0。 */
  child->parent = current;                      /* 建立子进程指向当前父进程的关系。 */
  child->image_size = current->image_size;      /* 复制当前程序镜像长度元数据。 */
  copy_name(child->name, current->name);        /* 子进程初始名称与父进程相同。 */
  child->state = PROC_RUNNABLE;                 /* 标记子进程可由父进程 wait 路径运行。 */
  return child->pid;                            /* 让父进程从 fork 得到新子进程 PID。 */
}                                               /* 结束 fork 实现。 */

/* 函数作用：把子进程退出状态写入处于非活动状态的父进程镜像。 */
static void                                     /* 状态写入是内部辅助操作，不返回结果。 */
store_wait_status(struct proc *parent, uint64 address, int status) /* 写入非活动父进程镜像。 */
{                                               /* 开始验证并存放 wait 退出状态。 */
  uint64 offset;                                /* 保存用户虚拟地址对应的镜像数组偏移。 */
  if (address == 0)                             /* wait(0) 明确表示调用者不需要退出状态。 */
    return;                                     /* 空状态指针时直接结束。 */
  if (address + sizeof(int) < address ||        /* 检查地址加长度是否发生无符号溢出。 */
      address + sizeof(int) > USER_MEM_SIZE)    /* 检查整个 int 是否落在用户空间内。 */
    return;                                     /* 非法状态地址只忽略写入，不破坏内核。 */
  offset = address;                             /* 用户程序按 VA 0 链接，因此 VA 就是镜像偏移。 */
  memmove(parent->image + offset, &status, sizeof(status)); /* 把退出码复制到父进程快照。 */
}                                               /* 结束非活动父进程状态写入。 */

/* 函数作用：回收已退出子进程，或保存父进程并切换到可运行子进程。 */
int                                             /* 回收时返回子 PID；失败返回 -1；切换时暂存 0。 */
proc_wait(uint64 status_addr)                   /* 等待子进程并在简化模型中直接运行它。 */
{                                               /* 开始扫描当前进程的所有子进程。 */
  int i;                                        /* 保存进程表扫描下标。 */
  int have_child = 0;                           /* 记录当前进程是否至少拥有一个子进程。 */
  struct proc *runnable = 0;                    /* 记录第一个可直接切换运行的子进程。 */

  for (i = 0; i < LAB2_NPROC; i++) {            /* 顺序检查固定进程表全部槽位。 */
    struct proc *p = &procs[i];                 /* 取得当前候选进程控制块。 */
    if (p->parent != current || p->state == PROC_UNUSED) /* 排除非子进程和空闲槽。 */
      continue;                                 /* 继续检查下一个进程表项。 */
    have_child = 1;                             /* 记住至少找到一个属于当前父进程的子进程。 */
    if (p->state == PROC_ZOMBIE) {              /* 已退出子进程可以立即回收。 */
      int pid = p->pid;                         /* 在清空 PCB 前保存待返回 PID。 */
      if (status_addr != 0)                     /* 调用者提供状态地址时才复制退出码。 */
        copy_to_user(status_addr, &p->xstate, sizeof(p->xstate)); /* 写入当前活动父用户区。 */
      memset(p, 0, sizeof(*p));                 /* 清空僵尸 PCB，使槽位重新变为 UNUSED。 */
      return pid;                               /* 让父进程的 wait 返回已回收子 PID。 */
    }                                           /* 结束僵尸子进程回收分支。 */
    if (p->state == PROC_RUNNABLE && runnable == 0) /* 只选择第一个尚未运行的子进程。 */
      runnable = p;                             /* 记录稍后要切换到的子进程。 */
  }                                             /* 完成所有进程表项检查。 */

  if (!have_child)                              /* 没有任何子进程时 wait 无对象可等。 */
    return -1;                                  /* 按简化接口返回失败。 */
  if (runnable == 0)                            /* 有子进程但既非僵尸也不可运行属于状态错误。 */
    return -1;                                  /* 避免在没有切换目标时继续。 */

  /* 保存父进程系统调用后的用户现场，随后直接运行一个子进程。 */
  current->wait_status_addr = status_addr;      /* 保存将来子进程退出时写状态的位置。 */
  current->state = PROC_WAITING;                /* 标记父进程暂时不占用活动执行区。 */
  save_current();                               /* 保存系统调用后的父用户现场和内存。 */
  load_current(runnable);                       /* 把选中子进程恢复为新的活动进程。 */
  return 0;                                     /* 返回值写入已切换现场会被 syscall 防护忽略。 */
}                                               /* 结束 wait 实现。 */

/* 函数作用：记录当前子进程退出状态，并恢复正在等待的父进程。 */
void                                            /* 正常情况下切换回父进程后不再回到退出用户程序。 */
proc_exit(int status)                           /* 记录退出码、唤回父进程并释放子槽。 */
{                                               /* 开始结束当前活动子进程。 */
  struct proc *exiting = current;               /* 保存退出进程指针，切换 current 后仍需访问。 */
  struct proc *parent = exiting->parent;        /* 取得 fork 时记录的父进程控制块。 */
  int pid = exiting->pid;                       /* 保存 PID 作为父进程 wait 返回值。 */

  if (parent == 0)                              /* 最初 Shell 没有父进程，当前模型不能回收它。 */
    panic("init process exited");               /* 初始进程退出属于不可恢复设计错误。 */

  exiting->xstate = status;                     /* 保存用户传入的退出状态。 */
  exiting->state = PROC_ZOMBIE;                 /* 暂时标记为僵尸，表示执行已经结束。 */

  if (parent->state != PROC_WAITING)            /* 简化模型要求父进程先进入 wait。 */
    panic("child exited without waiting parent"); /* 缺少通用调度器时无法处理其他情况。 */

  store_wait_status(parent, parent->wait_status_addr, status); /* 向非活动父镜像写退出码。 */
  parent->tf.a0 = (uint64)pid;                  /* 让父进程恢复后从 wait 得到子 PID。 */

  /*
   * 此刻仍运行在 exiting->kstack 上，绝不能清空整个结构，否则会把
   * 当前 C 调用链的返回地址一并抹掉。只标记槽位空闲；下次 allocproc
   * 已在父进程栈上运行，届时再安全地 memset。
   */
  exiting->state = PROC_UNUSED;                 /* 先把退出槽标为空闲，但不清空其当前内核栈。 */
  exiting->parent = 0;                          /* 断开父子关系，避免后续扫描误认。 */
  load_current(parent);                         /* 恢复等待父进程的用户镜像和 trapframe。 */
}                                               /* 结束 exit 实现，后续返回路径属于父进程。 */

/* 函数作用：检查一个用户虚拟地址区间是否完整落在固定用户空间内。 */
int                                             /* 合法返回 1，越界或溢出返回 0。 */
user_range_ok(uint64 address, uint64 length)    /* 验证用户虚拟区间 [address,address+length)。 */
{                                               /* 开始执行无符号边界检查。 */
  if (address + length < address)               /* 加法结果回绕说明 address+length 溢出。 */
    return 0;                                   /* 溢出区间一律视为非法。 */
  return address + length <= USER_MEM_SIZE;     /* 只有末端不超过固定用户空间才合法。 */
}                                               /* 结束用户区间检查。 */

/* 函数作用：验证用户地址后，把指定字节从用户空间复制到内核。 */
int                                             /* 复制成功返回 0，地址非法返回 -1。 */
copy_from_user(void *dst, uint64 src, uint64 length) /* 从用户 VA 复制到内核缓冲区。 */
{                                               /* 开始验证并复制用户数据。 */
  if (!user_range_ok(src, length))              /* 验证整个源区间都在用户空间。 */
    return -1;                                  /* 不读取越界用户地址。 */
  memmove(dst, (const void *)(USER_BASE + src), length); /* 把 VA 转为固定执行区 PA 后复制。 */
  return 0;                                     /* 报告用户数据复制成功。 */
}                                               /* 结束从用户空间复制。 */

/* 函数作用：验证用户地址后，把指定字节从内核复制到用户空间。 */
int                                             /* 复制成功返回 0，地址非法返回 -1。 */
copy_to_user(uint64 dst, const void *src, uint64 length) /* 从内核复制到用户 VA。 */
{                                               /* 开始验证并写入用户数据。 */
  if (!user_range_ok(dst, length))              /* 验证整个目标区间都在用户空间。 */
    return -1;                                  /* 不写入越界用户地址。 */
  memmove((void *)(USER_BASE + dst), src, length); /* 把用户 VA 转为固定执行区 PA 后复制。 */
  return 0;                                     /* 报告用户数据写入成功。 */
}                                               /* 结束向用户空间复制。 */

/* 函数作用：从用户空间有界复制一个以 NUL 结尾的字符串到内核。 */
int                                             /* 成功复制完整字符串返回 0，否则返回 -1。 */
copy_user_string(char *dst, uint64 src, int max) /* 有界复制 NUL 结尾的用户字符串。 */
{                                               /* 开始逐字符验证与复制。 */
  int i;                                        /* 保存当前复制的字符串下标。 */
  if (max <= 0 || !user_range_ok(src, 1))      /* 目标无容量或源首字节越界时无法开始。 */
    return -1;                                  /* 对非法基本参数返回错误。 */
  for (i = 0; i < max - 1; i++) {               /* 为目标 NUL 终止符保留最后一个字节。 */
    if (!user_range_ok(src + i, 1))             /* 每次读取前验证当前用户字节。 */
      return -1;                                /* 遇到用户空间边界时停止复制。 */
    dst[i] = *(const char *)(USER_BASE + src + i); /* 把当前用户字符复制到内核缓冲区。 */
    if (dst[i] == '\0')                         /* NUL 表示完整用户字符串已经结束。 */
      return 0;                                 /* 保留终止符并报告成功。 */
  }                                             /* 达到目标容量上限但尚未遇到 NUL。 */
  dst[max - 1] = '\0';                          /* 强制终止目标，避免后续越界读取。 */
  return -1;                                    /* 报告源字符串过长，调用者不得继续使用。 */
}                                               /* 结束用户字符串复制。 */
