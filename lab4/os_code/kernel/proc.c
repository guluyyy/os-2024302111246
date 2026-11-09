/*
 * Lab4 进程与调度核心。
 *
 * 与 Lab3 最大的区别是：不再修改一个 current 指针来假装切换，而是通过课程
 * 赠送的 swtch.S 在“进程内核栈”和“CPU 调度器栈”之间保存/恢复真实上下文。
 * 验收固定 -smp 1，但锁、关中断嵌套和锁交接仍按标准协议实现。
 */
#include "types.h"                     /* 引入进程代码使用的基本整数类型。 */
#include "riscv.h"                     /* 引入页表、PTE 与中断状态接口。 */
#include "memlayout.h"                 /* 引入 TRAPFRAME 和用户栈布局常量。 */
#include "course_sid.h"                /* 引入个人策略编号与完整学号。 */
#include "vm.h"                        /* 引入 sbrk 策略编号。 */
#include "proc.h"                      /* 引入 PCB、CPU、context 和进程接口。 */
#include "defs.h"                      /* 引入内存、锁、切换和诊断函数声明。 */

#if LAB4_POLICY_ID != 6                 /* 编译期确认源码与本人策略分配一致。 */
#error "本实现按本人分配的 LAB4_POLICY_ID=6 实现 Stride 调度"
#endif                                  /* 结束个人策略编号一致性检查。 */

static struct proc procs[LAB4_NPROC];  /* 固定大小 PCB 表。 */
static struct cpu cpus[1];             /* 基础验收固定一个 hart。 */
static struct spinlock wait_lock;      /* 保护 parent 指针和 wait/exit 协议。 */
static struct spinlock pid_lock;       /* 保护 next_pid 的单调分配。 */
static int next_pid = 1;               /* 下一个尚未使用的 PID。 */
static struct proc *initproc;          /* 孤儿进程的最终父进程。 */

extern uchar _uprog_table[];           /* Makefile 生成的内嵌程序表。 */

/* 内嵌用户程序元数据，布局必须与 Makefile 生成的 .quad/.asciz 一致。 */
struct uprog_header {                   /* 解释 Makefile 生成的程序表固定头部。 */
  uint64 start;                        /* 平铺二进制首物理地址。 */
  uint64 end;                          /* 平铺二进制末地址后一字节。 */
  uint64 data_start;                   /* 用户数据段页对齐虚拟起点。 */
  uint64 image_end;                    /* 包含 BSS 的逻辑镜像末端。 */
  uint64 entry;                        /* ELF ENTRY(main) 对应的用户虚拟地址。 */
  char name[1];                        /* 紧随固定字段的 NUL 结尾名称。 */
};                                      /* 固定字段后紧跟变长程序名和镜像。 */

/* 函数作用：返回当前 hart 的 CPU 私有调度状态。 */
struct cpu *
mycpu(void)
{
  return &cpus[0];                     /* 基础要求只有 hart 0。 */
}

/* 函数作用：在短暂关闭中断的条件下读取当前进程指针。 */
struct proc *
myproc(void)
{
  struct proc *p;                      /* 保存稳定的当前进程快照。 */
  push_off();                          /* 防止读取期间发生调度切换。 */
  p = mycpu()->proc;                   /* CPU 结构是 current 的唯一权威来源。 */
  pop_off();                           /* 恢复进入函数前的中断状态。 */
  return p;                            /* 允许调度器上下文得到空指针。 */
}

/* 函数作用：取得当前进程供 trampoline 使用的用户寄存器现场。 */
struct trapframe *
active_trapframe(void)
{
  struct proc *p = myproc();           /* 先稳定取得当前进程。 */
  return p == 0 ? 0 : p->trapframe;    /* 调度器上下文没有 trapframe。 */
}

/* 函数作用：有界复制进程诊断名称，并始终补上 NUL。 */
static void
copy_name(char dst[16], const char *src)
{
  int i = 0;                           /* 当前复制下标。 */
  while (i < 15 && src[i] != '\0') {   /* 留出最后一个 NUL 字节。 */
    dst[i] = src[i];                   /* 复制一个可见字符。 */
    i++;                               /* 移动到下一位置。 */
  }
  dst[i] = '\0';                       /* 保证后续日志不会越界读取。 */
}

/* 函数作用：去掉 Shell 命令两端空白和行结束符。 */
static void
normalize_name(char out[32], const char *in)
{
  int begin = 0;                       /* 首个非空白字符位置。 */
  int end;                             /* 行内容末端。 */
  int i;                               /* 输出数组下标。 */
  while (in[begin] == ' ' || in[begin] == '\t') /* 只跳过空格和制表符两类前导空白。 */
    begin++;                           /* 跳过前导空白。 */
  end = begin;                         /* 从有效起点查找行尾。 */
  while (in[end] && in[end] != '\n' && in[end] != '\r') /* 查找字符串或行结束位置。 */
    end++;                             /* 不把回车换行纳入程序名。 */
  while (end > begin && (in[end - 1] == ' ' || in[end - 1] == '\t')) /* 反向去掉尾部空白。 */
    end--;                             /* 去除尾部空白。 */
  for (i = 0; i < 31 && begin < end; i++, begin++) /* 最多复制31字节并保留NUL空间。 */
    out[i] = in[begin];                /* 有界复制规范化名称。 */
  out[i] = '\0';                       /* 形成标准 C 字符串。 */
}

/* 函数作用：在内嵌程序表中查询名称并返回镜像及段边界。 */
static int
find_uprog(const char *requested, const uchar **start, uint64 *size,
           uint64 *data_start, uint64 *image_end, uint64 *entry)
{
  char wanted[32];                     /* 规范化后的查询键。 */
  uchar *cursor = _uprog_table;        /* 当前程序表条目。 */
  normalize_name(wanted, requested);   /* 允许 Shell 输入带换行。 */
  for (;;) {                            /* 按表项顺序搜索，直到匹配或遇到双零结束项。 */
    struct uprog_header *e = (struct uprog_header *)cursor; /* 解释固定头。 */
    if (e->start == 0 && e->end == 0)  /* 双零是程序表结束标记。 */
      return -1;                        /* 已到表尾，表示请求的程序不存在。 */
    if (e->end < e->start || e->data_start > e->image_end) /* 拒绝损坏元数据。 */
      return -1;                        /* 元数据边界倒置，拒绝继续解析损坏表。 */
    if (strcmp(e->name, wanted) == 0) { /* 当前名称与规范化请求完全一致。 */
      *start = (const uchar *)e->start; /* 返回镜像首地址。 */
      *size = e->end - e->start;       /* 返回镜像实际字节数。 */
      *data_start = e->data_start;     /* 返回代码/数据权限分界。 */
      *image_end = e->image_end;       /* 返回 BSS 后逻辑末端。 */
      *entry = e->entry;               /* 返回链接器指定的真实程序入口。 */
      return 0;                         /* 五项元数据已成功返回调用者。 */
    }
    cursor = (uchar *)((e->end + 7) & ~7ULL); /* 跳过镜像并按 8 字节对齐。 */
  }
}

/* 函数作用：从学号计算本人的默认票数，并计算至少为 1 的 stride。 */
static void
set_default_stride(struct proc *p)
{
  p->tickets = 7 + (int)(COURSE_SID % 13ULL); /* 本学号得到 19 票。 */
  p->stride = LAB4_STRIDE_BIG / (uint)p->tickets; /* 按课程公式求步长。 */
  if (p->stride == 0)                 /* 极大票数也必须持续推进 pass。 */
    p->stride = 1;                     /* 防止整数除法得到零而令pass不再前进。 */
}

/* 函数作用：为进程创建只含 trampoline 和独立 trapframe 的空页表。 */
static int
create_empty_vm(struct proc *p)
{
  p->pagetable = uvmcreate();          /* 新建 SV39 根页表并映射 trampoline。 */
  if (p->pagetable == 0)               /* 根页表物理页申请失败时无法建进程。 */
    return -1;                         /* 保持PCB没有地址空间并报告失败。 */
  p->trapframe = (struct trapframe *)kalloc(); /* 分配一页可信寄存器现场。 */
  if (p->trapframe == 0 ||             /* trapframe申请失败，或下一行映射失败。 */
      mappages(p->pagetable, TRAPFRAME, PGSIZE, (uint64)p->trapframe,
               PTE_R | PTE_W) < 0) {   /* 不设置 PTE_U，用户不能直接访问。 */
    uvmfree(p->pagetable, (uint64)p->trapframe); /* 统一回滚半成品。 */
    p->pagetable = 0;                  /* 清除已回收所有权。 */
    p->trapframe = 0;                  /* 清除已回收所有权。 */
    return -1;                         /* 半成品已清理，调用者可安全回收PCB。 */
  }
  return 0;                            /* 空用户页表与trapframe均创建成功。 */
}

/* 函数作用：释放一个进程拥有的用户叶页、trapframe 和各级页表。 */
static void
release_vm(struct proc *p)
{
  if (p->pagetable != 0)               /* 只有实际拥有根页表时才递归释放。 */
    uvmfree(p->pagetable, (uint64)p->trapframe); /* vm.c 负责完整递归回收。 */
  p->pagetable = 0;                   /* 防止二次释放。 */
  p->trapframe = 0;                   /* 防止访问失效现场。 */
  p->sz = 0;                          /* 清除地址空间边界。 */
  p->heap_base = 0;                   /* 清除堆收缩下限。 */
}

/* forkret 是新进程第一次从 swtch 返回时的内核入口。 */
static void
forkret(void)
{
  struct proc *p = myproc();           /* 调度器已把 CPU 指向当前进程。 */
  release(&p->lock);                   /* 完成跨 swtch 的 p->lock 锁交接。 */
  usertrapret();                       /* 填写 trapframe 头部并用 sret 进入 U 态。 */
}

/* 函数作用：申请一个 PCB 并保持其 p->lock；失败返回空指针。 */
static struct proc *
allocproc(void)
{
  int i;                               /* PCB 表扫描下标。 */
  for (i = 0; i < LAB4_NPROC; i++) {   /* 从低下标开始寻找第一个空闲PCB。 */
    struct proc *p = &procs[i];        /* 当前候选槽。 */
    acquire(&p->lock);                 /* 状态检查和占用必须原子完成。 */
    if (p->state == PROC_UNUSED) {     /* 持锁确认槽位仍为空闲，随后独占初始化。 */
      acquire(&pid_lock);              /* 单独保护 PID 分配。 */
      p->pid = next_pid++;             /* 分配永不重复的活动 PID。 */
      release(&pid_lock);              /* PID已经取出，可以允许下一次分配。 */
      p->state = PROC_USED;            /* 暂不让调度器运行半成品。 */
      p->chan = 0;                     /* 新进程没有睡眠条件。 */
      p->killed = 0;                   /* 清除槽位上次留下的 kill 状态。 */
      p->xstate = 0;                   /* 清除退出码。 */
      p->parent = 0;                   /* 父子关系稍后在 wait_lock 下发布。 */
      memset(&p->context, 0, sizeof(p->context)); /* 清空内核上下文。 */
      p->context.ra = (uint64)forkret; /* 第一次切入时从 forkret 开始。 */
      p->context.sp = (uint64)(p->kstack + LAB4_KSTACK_SIZE); /* 栈顶 16B 对齐。 */
      p->cpu_ticks = 0;                /* 统计从零开始。 */
      p->last_slice_ticks = 0;         /* 尚未获得时间片。 */
      set_default_stride(p);           /* 安装个人默认票数和步长。 */
      p->pass = 0;                     /* 首进程为零，fork 会继承父 pass。 */
      p->image_size = 0;               /* 尚未装载用户镜像。 */
      p->pagetable = 0;                /* 尚未建立用户页表。 */
      p->trapframe = 0;                /* 尚未分配用户现场。 */
      p->sz = p->heap_base = 0;        /* 尚无用户虚拟区。 */
      p->name[0] = '\0';               /* 清除旧诊断名。 */
      return p;                        /* 按约定返回时仍持有 p->lock。 */
    }
    release(&p->lock);                 /* 该槽不可用，继续扫描。 */
  }
  return 0;                            /* 进程表已满。 */
}

/* 函数作用：回收 ZOMBIE PCB 的所有资源；调用者必须持有 p->lock。 */
static void
freeproc(struct proc *p)
{
  release_vm(p);                       /* 先释放大块地址空间资源。 */
  p->pid = 0;                          /* PID 不再可查询。 */
  p->parent = 0;                       /* 解除父子关系。 */
  p->chan = 0;                         /* 清除睡眠条件。 */
  p->killed = 0;                       /* 清除终止请求。 */
  p->xstate = 0;                       /* 清除旧退出码。 */
  p->cpu_ticks = p->last_slice_ticks = 0; /* 清除调度统计。 */
  p->tickets = 0;                     /* 清除策略字段。 */
  p->stride = 0;                      /* 清除策略字段。 */
  p->pass = 0;                        /* 清除虚拟运行时间。 */
  p->image_size = 0;                  /* 清除镜像元数据。 */
  p->name[0] = '\0';                  /* 清除诊断名称。 */
  memset(&p->context, 0, sizeof(p->context)); /* 防止复用旧返回地址。 */
  p->state = PROC_UNUSED;             /* 最后发布槽位重新可分配。 */
}

/* 函数作用：初始化全部进程锁、父子关系锁和 CPU 调度状态。 */
void
procinit(void)
{
  int i;                               /* 初始化 PCB 表下标。 */
  initlock(&wait_lock, "wait_lock");  /* wait/exit 共享条件锁。 */
  initlock(&pid_lock, "pid_lock");    /* PID 分配锁。 */
  memset(cpus, 0, sizeof(cpus));       /* 调度器开始时没有当前进程。 */
  for (i = 0; i < LAB4_NPROC; i++) {   /* 逐个初始化进程表中的独立锁和状态。 */
    initlock(&procs[i].lock, "proc"); /* 每个 PCB 拥有独立锁。 */
    procs[i].state = PROC_UNUSED;      /* 所有槽位初始可用。 */
  }
}

/* 函数作用：装载首个程序并把它作为 RUNNABLE 进程交给 scheduler。 */
struct proc *
proc_userinit(const char *name)
{
  struct proc *p = allocproc();        /* 返回时持有首进程锁。 */
  if (p == 0)                          /* 启动阶段连PID1都无法分配属于致命错误。 */
    panic("proc_userinit: no slot");  /* 打印原因并永久停止，避免空指针启动。 */
  mycpu()->proc = p;                   /* 让复用的 proc_exec 能定位构造目标。 */
  if (proc_exec(name) < 0)             /* 首个Shell镜像必须存在且能完成映射。 */
    panic("proc_userinit: exec failed"); /* 无Shell时系统无法接受任何验收命令。 */
  copy_name(p->name, name);            /* 保存首进程诊断名。 */
  initproc = p;                        /* 记录孤儿收养者。 */
  p->state = PROC_RUNNABLE;            /* 完整构造后才发布给调度器。 */
  mycpu()->proc = 0;                   /* main 返回调度器上下文。 */
  release(&p->lock);                   /* 允许 scheduler 申请该 PCB。 */
  return p;                            /* main 用它打印一次页表。 */
}

/* 函数作用：构建新程序地址空间，全部成功后原子替换当前地址空间。 */
int
proc_exec(const char *name)
{
  const uchar *image;                  /* 内嵌镜像首地址。 */
  uint64 binary_size, data_start, image_end, entry; /* 程序表元数据。 */
  uint64 guard_start, stack_base, stack_top;  /* 低地址用户布局。 */
  pagetable_t newpt, oldpt;            /* 新旧根页表。 */
  struct trapframe *newtf, *oldtf;     /* 新旧用户现场页。 */
  struct proc *p = myproc();           /* exec 只替换调用进程。 */
  char normalized[32];                 /* 规范化诊断名称。 */

  if (p == 0 || find_uprog(name, &image, &binary_size, /* 检查调用上下文并查询程序元数据。 */
                           &data_start, &image_end, &entry) < 0)
    return -1;                         /* 名称不存在或没有当前进程。 */
  if (binary_size == 0 || data_start == 0 || /* 镜像必须非空且有合法数据段分界。 */
      (data_start & (PGSIZE - 1)) != 0 || binary_size > image_end)
    return -1;                         /* 拒绝不符合链接约定的镜像。 */
  newpt = uvmcreate();                 /* 旧空间仍有效时构造新空间。 */
  if (newpt == 0)                      /* OOM导致无法创建新地址空间时保留旧程序。 */
    return -1;                         /* exec原子失败，不改变当前页表。 */
  newtf = (struct trapframe *)kalloc(); /* 新空间配套独立现场页。 */
  if (newtf == 0 ||                    /* 现场页或固定高地址映射任一失败都回滚。 */
      mappages(newpt, TRAPFRAME, PGSIZE, (uint64)newtf, PTE_R | PTE_W) < 0)
    goto fail;                         /* 统一释放newpt和可能存在的newtf。 */
  if (uvmalloc(newpt, 0, data_start, PTE_R | PTE_X | PTE_U) < 0) /* 分配代码页。 */
    goto fail;                         /* 代码页用户可读、可执行。 */
  if (uvmalloc(newpt, data_start, image_end, PTE_R | PTE_W | PTE_U) < 0) /* 分配数据页。 */
    goto fail;                         /* 数据/BSS 页用户可读写。 */
  if (uvmwrite(newpt, 0, image, binary_size) < 0) /* 把内嵌字节写入新用户页。 */
    goto fail;                         /* 受控写入程序镜像。 */
  guard_start = PGROUNDUP(image_end);  /* 镜像之后从页边界开始布局。 */
  stack_base = guard_start + LAB3_GUARD_PAGES * PGSIZE; /* guard 保持未映射。 */
  stack_top = stack_base + USER_STACK_PAGES * PGSIZE;   /* 单页用户栈顶。 */
  if (stack_top >= TRAPFRAME ||        /* 栈不得覆盖trapframe保留页。 */
      uvmalloc(newpt, stack_base, stack_top, PTE_R | PTE_W | PTE_U) < 0)
    goto fail;                         /* 栈映射失败时同样保持旧程序可运行。 */
  memset(newtf, 0, sizeof(*newtf));    /* 用户寄存器从确定状态启动。 */
  newtf->epc = entry;                 /* 从ELF ENTRY(main)启动，不假设main位于VA 0。 */
  newtf->sp = stack_top;              /* 栈从映射页顶端向低地址增长。 */
  oldpt = p->pagetable;               /* 保存旧资源供提交后回收。 */
  oldtf = p->trapframe;               /* 保存旧现场页。 */
  p->pagetable = newpt;               /* 原子提交新页表。 */
  p->trapframe = newtf;               /* 原子提交新现场。 */
  p->sz = stack_top;                  /* 堆从初始栈顶之后开始。 */
  p->heap_base = stack_top;           /* 记录堆收缩下限。 */
  p->image_size = binary_size;        /* 保存观测元数据。 */
  normalize_name(normalized, name);   /* 去掉 Shell 输入行尾。 */
  copy_name(p->name, normalized);     /* 更新进程诊断名称。 */
  if (oldpt != 0)                      /* 首次exec没有旧空间，普通exec则需要回收。 */
    uvmfree(oldpt, (uint64)oldtf);     /* 仅在提交成功后回收旧空间。 */
  return 0;                            /* 新程序已完整提交，exec成功。 */

fail:
  uvmfree(newpt, (uint64)newtf);       /* 回滚半成品，旧空间完全不变。 */
  return -1;                           /* 向用户报告失败且保留原地址空间。 */
}

/* 函数作用：深拷贝当前地址空间，建立一个 RUNNABLE 子进程。 */
int
proc_fork(void)
{
  struct proc *parent = myproc();      /* fork 的调用者。 */
  struct proc *child = allocproc();    /* 返回时持有 child->lock。 */
  int pid;                             /* 发布前保存子 PID。 */
  if (child == 0)                      /* 进程表没有空闲槽时无法fork。 */
    return -1;                         /* 父进程收到标准失败返回值。 */
  if (create_empty_vm(child) < 0 ||    /* 建空页表或深拷贝任一步失败都回滚。 */
      uvmcopy(parent->pagetable, child->pagetable, parent->sz) < 0) {
    freeproc(child);                   /* 回收所有已经分配的子资源。 */
    release(&child->lock);             /* 回收后交还PCB锁，使槽位可再次使用。 */
    return -1;                         /* 父进程地址空间保持不变。 */
  }
  *child->trapframe = *parent->trapframe; /* 复制全部用户寄存器。 */
  child->trapframe->a0 = 0;            /* 子进程看到 fork 返回 0。 */
  child->image_size = parent->image_size; /* 继承地址空间元数据。 */
  child->sz = parent->sz;              /* 继承低地址有效上界。 */
  child->heap_base = parent->heap_base; /* 继承堆收缩下限。 */
  child->tickets = parent->tickets;    /* fork 继承父进程权重。 */
  child->stride = parent->stride;      /* 保持票数/步长一致。 */
  child->pass = parent->pass;          /* 从当前公平位置起步，避免新进程垄断。 */
  copy_name(child->name, parent->name); /* 继承程序名称。 */
  pid = child->pid;                    /* 保存父进程的 fork 返回值。 */
  release(&child->lock);               /* 先结束反向锁序。 */
  acquire(&wait_lock);                 /* parent 只能在 wait_lock 下发布。 */
  child->parent = parent;              /* 建立父子关系。 */
  release(&wait_lock);                 /* 父指针已经完整发布。 */
  acquire(&child->lock);               /* 再发布可运行状态。 */
  child->state = PROC_RUNNABLE;        /* 调度器从此可以选择子进程。 */
  release(&child->lock);               /* 子进程从此可以被调度器选中。 */
  return pid;                          /* 父进程看到子 PID。 */
}

/* 函数作用：验证 sched 所需四条不变量后切回 CPU 调度器上下文。 */
void
sched(void)
{
  struct proc *p = myproc();           /* 当前必须是正在让出 CPU 的进程。 */
  struct cpu *c = mycpu();             /* 保存调度器上下文的位置。 */
  int intena;                          /* 跨切换保存当前嵌套中断意图。 */
  if (!holding(&p->lock))              /* 切换前必须由当前CPU持有本进程锁。 */
    panic("sched: p->lock not held"); /* 不变量 1：必须持进程锁。 */
  if (c->noff != 1)                    /* noff大于1表示还持有其他锁，可能死锁。 */
    panic("sched: lock depth");       /* 不变量 2：只能持这一把进程锁。 */
  if (p->state == PROC_RUNNING)        /* 调度器不能接收仍声明占用CPU的进程。 */
    panic("sched: still running");    /* 不变量 3：状态必须先改变。 */
  if (intr_get())                      /* 上下文和锁交接窗口禁止中断重入。 */
    panic("sched: interruptible");    /* 不变量 4：切换窗口必须关中断。 */
  intena = c->intena;                  /* 保存本进程进入锁前的中断意图。 */
  swtch(&p->context, &c->context);     /* 保存进程内核现场，恢复调度器现场。 */
  c->intena = intena;                  /* 下次被选中后恢复进程的嵌套语义。 */
}

/* 函数作用：把当前进程改为 RUNNABLE 并主动切回 scheduler。 */
void
yield(void)
{
  struct proc *p = myproc();           /* 取得要让出 CPU 的进程。 */
  acquire(&p->lock);                   /* 状态和上下文切换由同一锁覆盖。 */
  p->state = PROC_RUNNABLE;            /* 允许后续再次选择。 */
  sched();                             /* 持锁切出；再次运行时从这里返回。 */
  release(&p->lock);                   /* 完成 scheduler 交回的锁交接。 */
}

/* 函数作用：返回当前全部 RUNNABLE 进程中最小的 pass。仅用于单核唤醒归一化。 */
static uint64
minimum_runnable_pass(void)
{
  uint64 minimum = ~0ULL;              /* 尚未找到候选时使用最大值。 */
  int i;                               /* PCB 扫描下标。 */
  for (i = 0; i < LAB4_NPROC; i++)     /* 单核关中断条件下遍历全部PCB快照。 */
    if (procs[i].state == PROC_RUNNABLE && procs[i].pass < minimum) /* 只比较就绪者。 */
      minimum = procs[i].pass;         /* 单核关中断环境下快照稳定。 */
  if (minimum == ~0ULL && mycpu()->proc != 0) /* 没有其他就绪者时使用当前公平进度。 */
    minimum = mycpu()->proc->pass;     /* 没有其他就绪者时跟随当前进度。 */
  return minimum == ~0ULL ? 0 : minimum; /* 系统完全空闲时从零起步。 */
}

/* 函数作用：永久选择 pass 最小的 RUNNABLE 进程并执行上下文切换。 */
void
scheduler(void)
{
  struct cpu *c = mycpu();             /* 当前 CPU 的调度器上下文。 */
  int cursor = 0;                      /* 同 pass 时轮换扫描起点。 */
  c->proc = 0;                         /* 明确当前位于调度器。 */
  for (;;) {                           /* 调度器永不返回，持续完成选择与切换。 */
    struct proc *best = 0;             /* 本轮最小 pass 候选。 */
    uint64 best_pass = ~0ULL;          /* 候选虚拟运行时间。 */
    int best_index = -1;               /* 用于更新轮换起点。 */
    int n;                             /* 环形扫描计数。 */
    intr_on();                         /* 空闲和扫描阶段允许设备中断。 */
    for (n = 0; n < LAB4_NPROC; n++) { /* 从轮转游标开始检查每一个PCB一次。 */
      int i = (cursor + n) % LAB4_NPROC; /* 从上次候选之后环形扫描。 */
      struct proc *p = &procs[i];      /* 当前候选 PCB。 */
      acquire(&p->lock);               /* 稳定读取状态和 pass。 */
      if (p->state == PROC_RUNNABLE && p->pass < best_pass) { /* 发现更小pass候选。 */
        best = p;                      /* 记录当前最公平候选。 */
        best_pass = p->pass;           /* 保存比较基准。 */
        best_index = i;                /* 保存轮换位置。 */
      }
      release(&p->lock);               /* 扫描时不同时持有多把进程锁。 */
    }
    if (best == 0) {                   /* 一轮扫描没有发现任何可运行进程。 */
      asm volatile("wfi");             /* 无进程时等待下一次中断，避免忙等。 */
      continue;                        /* 被中断唤醒后重新扫描进程表。 */
    }
    acquire(&best->lock);              /* 正式选择前重新稳定候选。 */
    if (best->state != PROC_RUNNABLE || best->pass != best_pass) { /* 重锁后复核快照。 */
      release(&best->lock);            /* 状态改变则重新开始选择。 */
      continue;                        /* 候选发生变化时放弃本轮并重新选择。 */
    }
    best->state = PROC_RUNNING;         /* 发布它正在 CPU 上执行。 */
    best->last_slice_ticks = 0;         /* 新调度片从零开始观测。 */
    best->pass += best->stride;         /* Stride：先推进虚拟运行时间。 */
    c->proc = best;                     /* trap 与 myproc 从此定位该进程。 */
    cursor = (best_index + 1) % LAB4_NPROC; /* 同 pass 时下次从后续槽开始。 */
    swtch(&c->context, &best->context); /* 切到进程内核栈。 */
    c->proc = 0;                        /* 只有调度器可以清除当前进程。 */
    release(&best->lock);               /* 完成进程交回的锁交接。 */
  }
}

/* 函数作用：原子释放条件锁、睡眠，醒来后重新取得原条件锁。 */
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();           /* 当前调用进程。 */
  if (p == 0 || lk == 0)               /* 调度器上下文或空条件锁都不能睡眠。 */
    panic("sleep: bad arguments");     /* 非法调用会破坏锁交接，立即停止。 */
  acquire(&p->lock);                   /* 先锁 PCB，封住 wakeup 丢失窗口。 */
  release(lk);                         /* 再释放调用者的条件锁。 */
  p->chan = chan;                      /* 公布正在等待的条件。 */
  p->state = PROC_SLEEPING;            /* 调度器不再选择该进程。 */
  sched();                             /* 持 p->lock 切出。 */
  p->chan = 0;                         /* 被唤醒后清除条件记录。 */
  release(&p->lock);                   /* 结束 scheduler 交回的 PCB 锁。 */
  acquire(lk);                         /* 按 sleep API 约定重获条件锁。 */
}

/* 函数作用：把 chan 上全部睡眠进程变为 RUNNABLE。 */
void
wakeup(void *chan)
{
  struct proc *current = myproc();     /* 跳过调用者，避免重入自己的锁。 */
  uint64 floor = minimum_runnable_pass(); /* 取得当前公平起点。 */
  int i;                               /* PCB 扫描下标。 */
  for (i = 0; i < LAB4_NPROC; i++) {   /* 检查所有可能睡在同一条件上的进程。 */
    struct proc *p = &procs[i];        /* 当前被检查进程。 */
    if (p == current)                  /* 调用wakeup的运行进程不应被重复加锁。 */
      continue;                        /* 当前进程不可能等待自己的条件。 */
    acquire(&p->lock);                 /* 稳定检查 state 和 chan。 */
    if (p->state == PROC_SLEEPING && p->chan == chan) { /* 状态与条件都匹配才唤醒。 */
      p->pass = floor;                 /* 醒来从当前最小 pass 起步，不积累睡眠红利。 */
      p->state = PROC_RUNNABLE;         /* 发布为可调度。 */
    }
    release(&p->lock);                 /* 当前PCB检查结束，允许其继续变化。 */
  }
}

/* 函数作用：睡眠等待任意子进程退出，写回退出码并完整回收资源。 */
int
proc_wait(uint64 status_addr)
{
  struct proc *parent = myproc();      /* wait 调用者。 */
  acquire(&wait_lock);                 /* 防止与 exit 的唤醒/父指针修改交错。 */
  for (;;) {                           /* 未发现僵尸时睡眠，醒来后必须重新扫描。 */
    int have_child = 0;                /* 本轮是否看见任意子进程。 */
    int i;                             /* PCB 扫描下标。 */
    for (i = 0; i < LAB4_NPROC; i++) { /* 在wait_lock保护下寻找自己的子进程。 */
      struct proc *p = &procs[i];      /* 当前可能的子进程。 */
      if (p->parent != parent)         /* 非本进程子女不参与本次wait。 */
        continue;                      /* parent 由 wait_lock 保护，可先过滤。 */
      acquire(&p->lock);               /* 稳定子进程状态和退出码。 */
      have_child = 1;                  /* 记住父进程确实还有孩子。 */
      if (p->state == PROC_ZOMBIE) {   /* 僵尸已经停止使用自身资源，可以回收。 */
        int pid = p->pid;              /* 回收前保存返回 PID。 */
        int status = p->xstate;         /* 回收前保存退出状态。 */
        if (status_addr != 0 &&        /* 非空用户指针需要安全写入退出码。 */
            copyout(parent->pagetable, status_addr,
                    &status, sizeof(status)) < 0) {
          release(&p->lock);           /* 用户地址非法时不错误回收子进程。 */
          release(&wait_lock);         /* 写回失败时交还父子关系锁。 */
          return -1;                   /* 保留僵尸供父进程以后再次wait。 */
        }
        freeproc(p);                    /* 父进程负责最终释放僵尸资源。 */
        release(&p->lock);             /* PCB已变为UNUSED，释放其独立锁。 */
        release(&wait_lock);           /* 本次父子关系处理结束。 */
        return pid;                     /* wait 返回被回收子 PID。 */
      }
      release(&p->lock);               /* 非僵尸子进程暂时不处理。 */
    }
    if (!have_child || parent->killed) { /* 无子女或被终止时不能继续阻塞。 */
      release(&wait_lock);             /* 无孩子或被 kill 时不能继续等。 */
      return -1;                       /* 按wait接口报告没有可等待目标。 */
    }
    sleep(parent, &wait_lock);         /* 原子释放 wait_lock 并等待孩子唤醒。 */
  }
}

/* 函数作用：结束当前进程，唤醒父进程并以 ZOMBIE 状态永久切出。 */
void
proc_exit(int status)
{
  struct proc *p = myproc();           /* 即将退出的进程。 */
  int i;                               /* 用于收养子进程。 */
  if (p == initproc)                   /* PID1退出后将无人收养孤儿或提供Shell。 */
    panic("init process exited");      /* 首 Shell 是系统生存根，不能退出。 */
  acquire(&wait_lock);                 /* 固定全部 parent 关系和 wait 条件。 */
  for (i = 0; i < LAB4_NPROC; i++)     /* 扫描退出者可能留下的全部子进程。 */
    if (procs[i].parent == p)          /* 只重设直接子进程的父指针。 */
      procs[i].parent = initproc;       /* 防止退出者留下无人回收的孤儿。 */
  wakeup(p->parent);                    /* 父进程若在 wait，立即变为 RUNNABLE。 */
  acquire(&p->lock);                   /* 按 wait_lock -> p->lock 固定顺序加锁。 */
  p->xstate = status;                  /* 保存 wait(&status) 要取得的值。 */
  p->state = PROC_ZOMBIE;              /* 资源保留到父进程回收。 */
  release(&wait_lock);                 /* parent 关系处理已经完成。 */
  sched();                             /* 持 p->lock 永久切回调度器。 */
  panic("zombie returned");           /* ZOMBIE 绝不能再次被调度。 */
}

/* 函数作用：标记指定 PID 被 kill，并唤醒睡眠目标以便尽快处理。 */
int
proc_kill(int pid)
{
  int i;                               /* PCB 扫描下标。 */
  for (i = 0; i < LAB4_NPROC; i++) {   /* 按PID搜索仍然活动的目标进程。 */
    struct proc *p = &procs[i];        /* 当前候选。 */
    acquire(&p->lock);                 /* 稳定 PID 和状态。 */
    if (p->pid == pid && p->state != PROC_UNUSED && p->state != PROC_ZOMBIE) { /* 命中活动目标。 */
      p->killed = 1;                   /* 在用户 trap 安全点转为 exit(-1)。 */
      if (p->state == PROC_SLEEPING) { /* 睡眠目标必须先醒来才能检查killed。 */
        p->pass = minimum_runnable_pass(); /* 醒来融入当前公平位置。 */
        p->state = PROC_RUNNABLE;       /* 使它能运行到终止检查。 */
      }
      release(&p->lock);               /* killed和可能的唤醒状态已经发布。 */
      return 0;                        /* 告诉调用者kill请求已成功记录。 */
    }
    release(&p->lock);                 /* 当前槽位不匹配，继续搜索。 */
  }
  return -1;                           /* 不存在或已经退出。 */
}

/* 函数作用：读取目标活动进程的累计 CPU tick 和当前片 tick 快照。 */
int
proc_schedstat(int pid, int *cpu_ticks, int *slice_ticks)
{
  struct proc *caller = myproc();      /* pid=-1 的目标。 */
  int wanted = pid == -1 ? caller->pid : pid; /* 解析自查询约定。 */
  int i;                               /* PCB 扫描下标。 */
  for (i = 0; i < LAB4_NPROC; i++) {   /* 扫描所有PCB寻找指定活动PID。 */
    struct proc *p = &procs[i];        /* 当前候选。 */
    acquire(&p->lock);                 /* 取得一致的两个统计值。 */
    if (p->pid == wanted && p->state != PROC_UNUSED && p->state != PROC_ZOMBIE) { /* 命中目标。 */
      *cpu_ticks = (int)p->cpu_ticks;  /* 仅复制到内核局部变量。 */
      *slice_ticks = (int)p->last_slice_ticks; /* 不在持锁时 copyout。 */
      release(&p->lock);               /* 统计快照已复制到内核局部变量。 */
      return 0;                        /* 调用者可以在无锁状态下copyout。 */
    }
    release(&p->lock);                 /* 未命中时释放当前PCB锁。 */
  }
  return -1;                           /* 不存在或已经退出。 */
}

/* 函数作用：修改当前进程票数并据此重算至少为 1 的 stride。 */
int
proc_setickets(int tickets)
{
  struct proc *p = myproc();           /* setickets 只允许修改调用者。 */
  if (tickets < 1)                    /* 零或负票数无法形成有效Stride权重。 */
    return -1;                         /* 非正票数会破坏调度公式。 */
  acquire(&p->lock);                   /* 与 scheduler 的策略读取互斥。 */
  p->tickets = tickets;                /* 覆盖学号默认票数。 */
  p->stride = LAB4_STRIDE_BIG / (uint)tickets; /* 重算步长。 */
  if (p->stride == 0)                 /* 票数大于1000时整数除法可能得到零。 */
    p->stride = 1;                     /* 保证 pass 始终能前进。 */
  release(&p->lock);                  /* 新tickets和stride已原子发布。 */
  return 0;                           /* 向用户确认改票成功。 */
}

/* 函数作用：按 EAGER 语义扩大或收缩当前进程堆。 */
uint64
proc_sbrk(long amount, int policy)
{
  struct proc *p = myproc();           /* 当前地址空间所有者。 */
  uint64 oldsz = p->sz;                /* 标准 sbrk 成功返回旧 break。 */
  uint64 newsz;                        /* 经过边界检查的新 break。 */
  if (policy != SBRK_EAGER && policy != SBRK_LAZY) /* 仅接受课程定义的两种参数。 */
    return (uint64)-1;                 /* 未知策略参数。 */
  if (amount == 0)                    /* 零增量只查询当前break。 */
    return oldsz;                      /* 查询 break 不改变映射。 */
  if (amount > 0) {                   /* 正增量走扩堆和物理页分配路径。 */
    newsz = oldsz + (uint64)amount;    /* 计算扩展目标。 */
    if (newsz < oldsz || newsz >= TRAPFRAME) /* 检查加法溢出和高地址冲突。 */
      return (uint64)-1;               /* 拒绝溢出或覆盖高地址保留页。 */
    if (uvmalloc(p->pagetable, oldsz, newsz, PTE_R | PTE_W | PTE_U) < 0) /* 立即分配。 */
      return (uint64)-1;               /* OOM 时 uvmalloc 自行回滚。 */
  } else {
    uint64 shrink = (uint64)(-amount); /* 转换为正的收缩量。 */
    if (shrink > oldsz || oldsz - shrink < p->heap_base) /* 检查下溢和越界。 */
      return (uint64)-1;               /* 不允许越过初始堆基址。 */
    newsz = oldsz - shrink;            /* 得到合法目标。 */
    uvmdealloc(p->pagetable, oldsz, newsz); /* 拆映射并归还整页。 */
  }
  p->sz = newsz;                       /* 全部成功后提交新 break。 */
  return oldsz;                       /* sbrk ABI规定成功返回调整前break。 */
}
