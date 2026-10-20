/* Lab3 最小进程层：每进程独立页表、深拷贝 fork 与原子 exec。 */
#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "vm.h"
#include "proc.h"
#include "defs.h"

static struct proc procs[LAB2_NPROC]; /* 固定进程表。 */
static struct proc *current;          /* 当前将从 trap 返回的进程。 */
static int next_pid = 1;              /* 单调递增 PID。 */

extern uchar _uprog_table[];          /* Makefile 生成的内嵌程序表。 */

/* 构建期程序元数据：物理镜像范围和用户段边界。 */
struct uprog_header { /* 定义Makefile生成的单个内嵌程序表项二进制布局。 */
  uint64 start;       /* 内嵌平铺二进制首地址。 */
  uint64 end;         /* 内嵌平铺二进制末地址后一字节。 */
  uint64 data_start;  /* 页对齐的数据段用户虚拟地址。 */
  uint64 image_end;   /* 包含 BSS 的用户镜像逻辑末端。 */
  char name[1];       /* 紧随固定头部的 NUL 结尾程序名。 */
}; /* 结束当前结构定义。 */

/* 函数作用：返回当前进程独立 trapframe。 */
struct trapframe * /* 声明下方函数的返回类型与可见范围。 */
active_trapframe(void) /* 函数作用：取得当前进程的独立陷阱帧。 */
{ /* 开始当前结构或代码块。 */
  return current == 0 ? 0 : current->trapframe; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：返回当前活动进程 PCB。 */
struct proc * /* 声明下方函数的返回类型与可见范围。 */
myproc(void) /* 函数作用：取得当前正在运行的进程。 */
{ /* 开始当前结构或代码块。 */
  return current; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：有界复制进程诊断名称。 */
static void /* 声明下方函数的返回类型与可见范围。 */
copy_name(char dst[16], const char *src) /* 函数作用：有界复制进程名称。 */
{ /* 开始当前结构或代码块。 */
  int i = 0; /* 声明循环下标。 */
  while (i < 15 && src[i] != '\0') { /* 在剩余数据或条件有效时继续循环处理。 */
    dst[i] = src[i]; /* 逐字节复制当前名称字符，且最多写入15个可见字符。 */
    i++; /* 推进源和目标共用的字符下标。 */
  } /* 结束当前条件、循环或函数代码块。 */
  dst[i] = '\0'; /* 在有效名称末端补NUL，保证后续诊断输出有界。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：去除 Shell 命令首尾空白和换行。 */
static void /* 声明下方函数的返回类型与可见范围。 */
normalize_name(char out[32], const char *in) /* 函数作用：清理Shell命令中的空白与换行。 */
{ /* 开始当前结构或代码块。 */
  int begin = 0; /* 保存去除前导空格后的首字符下标。 */
  int end; /* 保存页对齐后的区间终点。 */
  int i; /* 声明循环下标。 */

  while (in[begin] == ' ' || in[begin] == '\t') /* 在剩余数据或条件有效时继续循环处理。 */
    begin++; /* 跳过一个前导空格或制表符。 */
  end = begin; /* 从首个有效字符开始搜索命令末端。 */
  while (in[end] != '\0' && in[end] != '\n' && in[end] != '\r') /* 在剩余数据或条件有效时继续循环处理。 */
    end++; /* 向后扫描，直到NUL、回车或换行。 */
  while (end > begin && (in[end - 1] == ' ' || in[end - 1] == '\t')) /* 在剩余数据或条件有效时继续循环处理。 */
    end--; /* 去掉命令末端残留的空格或制表符。 */
  for (i = 0; i < 31 && begin < end; i++, begin++) /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    out[i] = in[begin]; /* 把规范化范围内的字符复制到输出缓冲区。 */
  out[i] = '\0'; /* 为规范化命令补NUL，供strcmp安全比较。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：遍历内嵌程序表并返回镜像和段边界。 */
static int /* 声明下方函数的返回类型与可见范围。 */
find_uprog(const char *requested, const uchar **start, uint64 *size, /* 函数作用：在内嵌程序表中查找程序及段边界。 */
           uint64 *data_start, uint64 *image_end) /* 保存用户数据段的页对齐起点。 */
{ /* 开始当前结构或代码块。 */
  char wanted[32]; /* 保存用于程序表比较的规范化名称。 */
  uchar *cursor = _uprog_table; /* 保存当前内嵌程序表条目地址。 */

  normalize_name(wanted, requested); /* 函数作用：清理Shell命令中的空白与换行。 */
  for (;;) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    struct uprog_header *entry = (struct uprog_header *)cursor; /* 解释当前内嵌程序表条目。 */
    if (entry->start == 0 && entry->end == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    if (entry->end < entry->start || entry->data_start > entry->image_end) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    if (strcmp(entry->name, wanted) == 0) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      *start = (const uchar *)entry->start; /* 向调用方返回匹配程序的内嵌二进制首地址。 */
      *size = entry->end - entry->start; /* 通过起止物理地址计算平铺镜像字节数。 */
      *data_start = entry->data_start; /* 返回链接期导出的代码/数据页边界。 */
      *image_end = entry->image_end; /* 返回包含BSS在内的逻辑用户镜像末端。 */
      return 0; /* 向调用方报告本次操作成功或返回空值。 */
    } /* 结束当前条件、循环或函数代码块。 */
    cursor = (uchar *)((entry->end + 7) & ~7ULL); /* 越过当前镜像并按8字节对齐定位下一表项。 */
  } /* 结束当前条件、循环或函数代码块。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：申请并清空一个空闲 PCB；不会预先创建用户地址空间。 */
static struct proc * /* 声明下方内部函数返回一个PCB指针。 */
allocproc(void) /* 函数作用：从固定进程表申请一个空闲PCB。 */
{ /* 开始当前结构或代码块。 */
  int i; /* 声明循环下标。 */

  for (i = 0; i < LAB2_NPROC; i++) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    if (procs[i].state == PROC_UNUSED) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      memset(&procs[i], 0, sizeof(procs[i])); /* 按指定值初始化目标内存区域。 */
      procs[i].pid = next_pid++; /* 分配唯一PID，并推进下次可用PID。 */
      procs[i].state = PROC_RUNNABLE; /* 把新槽位标为已分配且可以运行。 */
      return &procs[i]; /* 把当前计算结果或对象地址返回调用方。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：为进程创建根页表、trampoline 和独立 trapframe 映射。 */
static int /* 声明下方函数的返回类型与可见范围。 */
create_empty_vm(struct proc *p) /* 函数作用：为进程建立空用户页表和陷阱帧。 */
{ /* 开始当前结构或代码块。 */
  p->pagetable = uvmcreate(); /* 创建只带共享trampoline的独立用户根页表。 */
  if (p->pagetable == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  p->trapframe = (struct trapframe *)kalloc(); /* 向物理页分配器申请一个页面。 */
  if (p->trapframe == 0 || /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      mappages(p->pagetable, TRAPFRAME, PGSIZE, (uint64)p->trapframe, /* 函数作用：建立连续虚拟页到物理页的映射。 */
               PTE_R | PTE_W) < 0) { /* trapframe只允许S态读写，不设置PTE_U。 */
    uvmfree(p->pagetable, (uint64)p->trapframe); /* 函数作用：释放完整用户地址空间。 */
    p->pagetable = 0; /* 清除已经回收的页表指针，防止再次释放。 */
    p->trapframe = 0; /* 清除已经回收或未成功申请的trapframe指针。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：释放进程拥有的用户叶页、trapframe 与各级页表。 */
static void /* 声明下方函数的返回类型与可见范围。 */
release_vm(struct proc *p) /* 函数作用：释放进程拥有的地址空间资源。 */
{ /* 开始当前结构或代码块。 */
  if (p->pagetable != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    uvmfree(p->pagetable, (uint64)p->trapframe); /* 函数作用：释放完整用户地址空间。 */
  p->pagetable = 0; /* 资源释放后清除根页表所有权。 */
  p->trapframe = 0; /* 资源释放后清除trapframe所有权。 */
  p->sz = 0; /* 清除已不存在的用户地址空间上边界。 */
  p->heap_base = 0; /* 清除已不存在的初始堆边界。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：初始化进程表并建立尚未装载程序的 PID 1。 */
void /* 声明下方函数的返回类型与可见范围。 */
procinit(void) /* 函数作用：初始化进程表和首个进程。 */
{ /* 开始当前结构或代码块。 */
  memset(procs, 0, sizeof(procs)); /* 按指定值初始化目标内存区域。 */
  current = allocproc(); /* 为启动阶段的首个Shell申请PID 1槽位。 */
  if (current == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("procinit: no process slot"); /* 检测到不可恢复的内核状态并停止运行。 */
  current->state = PROC_RUNNING; /* 将首进程设为当前正在构造和运行的进程。 */
  copy_name(current->name, "sh"); /* 函数作用：有界复制进程名称。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：构建全新程序地址空间，成功后原子替换当前地址空间。 */
int /* 声明下方函数的返回类型与可见范围。 */
proc_exec(const char *name) /* 函数作用：构造新程序地址空间并原子替换旧空间。 */
{ /* 开始当前结构或代码块。 */
  const uchar *image; /* 保存内嵌用户程序镜像首地址。 */
  uint64 binary_size; /* 保存平铺用户程序镜像字节数。 */
  uint64 data_start; /* 保存用户数据段的页对齐起点。 */
  uint64 image_end; /* 保存包含BSS的用户镜像末端。 */
  uint64 guard_start; /* 保存未映射保护页的起始地址。 */
  uint64 stack_base; /* 保存用户栈页面的底部地址。 */
  uint64 stack_top; /* 保存用户栈初始栈顶和堆基址。 */
  pagetable_t newpt; /* 保存exec构造中的新用户根页表。 */
  struct trapframe *newtf; /* 保存exec构造中的新陷阱帧页面。 */
  pagetable_t oldpt; /* 暂存exec替换前的旧用户根页表。 */
  struct trapframe *oldtf; /* 暂存exec替换前的旧陷阱帧页面。 */
  char normalized[32]; /* 保存去除空白后的程序名。 */

  if (find_uprog(name, &image, &binary_size, &data_start, &image_end) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  if (binary_size == 0 || data_start == 0 || /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      (data_start & (PGSIZE - 1)) != 0 || binary_size > image_end) /* 同时要求数据边界页对齐且镜像不超过逻辑末端。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */

  newpt = uvmcreate(); /* 先在旧程序仍可运行时创建不可见的新根页表。 */
  if (newpt == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  newtf = (struct trapframe *)kalloc(); /* 向物理页分配器申请一个页面。 */
  if (newtf == 0 || mappages(newpt, TRAPFRAME, PGSIZE, (uint64)newtf, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
                             PTE_R | PTE_W) < 0) /* 把新trapframe映射到固定高地址且禁止用户访问。 */
    goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */

  if (uvmalloc(newpt, 0, data_start, PTE_R | PTE_X | PTE_U) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */
  if (uvmalloc(newpt, data_start, image_end, PTE_R | PTE_W | PTE_U) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */
  if (uvmwrite(newpt, 0, image, binary_size) < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */

  guard_start = PGROUNDUP(image_end);             /* 保护页保持完全未映射。 */
  stack_base = guard_start + LAB3_GUARD_PAGES * PGSIZE; /* 跳过一页未映射guard后得到用户栈底。 */
  stack_top = stack_base + USER_STACK_PAGES * PGSIZE; /* 计算向下增长栈的初始栈顶，同时作为堆基址。 */
  if (stack_top >= TRAPFRAME || /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      uvmalloc(newpt, stack_base, stack_top, PTE_R | PTE_W | PTE_U) < 0) /* 函数作用：为用户虚拟区间分配并映射物理页。 */
    goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */

  memset(newtf, 0, sizeof(*newtf)); /* 按指定值初始化目标内存区域。 */
  newtf->epc = 0;                                /* 用户程序链接入口固定为 VA 0。 */
  newtf->sp = stack_top;                          /* 栈从独立栈页顶端向低地址增长。 */
  newtf->a0 = 0; /* 本阶段不传argc，因此把首个用户参数寄存器清零。 */
  newtf->a1 = 0; /* 本阶段不解析argv，因此把第二个用户参数寄存器清零。 */

  oldpt = current->pagetable; /* 暂存旧根页表，等原子交换完成后再释放。 */
  oldtf = current->trapframe; /* 暂存旧trapframe物理页，和旧页表一起释放。 */
  current->pagetable = newpt; /* 新空间全部构造成功后才发布新根页表。 */
  current->trapframe = newtf; /* 同步发布与新页表配套的新trapframe。 */
  current->sz = stack_top; /* 初始有效低地址空间到用户栈顶为止。 */
  current->heap_base = stack_top; /* 记录sbrk不得向下越过的初始break。 */
  current->image_size = binary_size; /* 保存实际平铺镜像长度供诊断。 */
  normalize_name(normalized, name); /* 函数作用：清理Shell命令中的空白与换行。 */
  copy_name(current->name, normalized); /* 函数作用：有界复制进程名称。 */
  if (oldpt != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    uvmfree(oldpt, (uint64)oldtf);                 /* 新空间完整后才释放旧空间。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */

fail: /* 定义失败回滚或分支跳转目标。 */
  uvmfree(newpt, (uint64)newtf);                  /* 半成品空间全部回滚。 */
  return -1;                                      /* 旧地址空间仍可继续运行。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：逐页深拷贝当前地址空间并创建可运行子进程。 */
int /* 声明下方函数的返回类型与可见范围。 */
proc_fork(void) /* 函数作用：深拷贝当前地址空间并创建子进程。 */
{ /* 开始当前结构或代码块。 */
  struct proc *child = allocproc(); /* 保存下一级页表或新建子进程。 */

  if (child == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  if (create_empty_vm(child) < 0 || /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      uvmcopy(current->pagetable, child->pagetable, current->sz) < 0) { /* 函数作用：逐页深拷贝父进程用户地址空间。 */
    release_vm(child); /* 函数作用：释放进程拥有的地址空间资源。 */
    memset(child, 0, sizeof(*child)); /* 按指定值初始化目标内存区域。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  } /* 结束当前条件、循环或函数代码块。 */
  *child->trapframe = *current->trapframe; /* 复制父进程全部用户寄存器现场作为子进程起点。 */
  child->trapframe->a0 = 0;                       /* 子进程 fork 返回值。 */
  child->parent = current; /* 建立父子关系，供wait和exit相互定位。 */
  child->image_size = current->image_size; /* 子进程继承当前程序镜像长度元数据。 */
  child->sz = current->sz; /* 子进程深拷贝后拥有相同虚拟地址上边界。 */
  child->heap_base = current->heap_base; /* 子进程继承相同的堆收缩下限。 */
  copy_name(child->name, current->name); /* 函数作用：有界复制进程名称。 */
  child->state = PROC_RUNNABLE; /* 所有内存和现场复制成功后才让调度路径看见子进程。 */
  return child->pid;                              /* 父进程 fork 返回子 PID。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：向非活动父进程的独立页表安全写入退出状态。 */
static void /* 声明下方函数的返回类型与可见范围。 */
store_wait_status(struct proc *parent, uint64 address, int status) /* 函数作用：经父进程页表安全写回退出状态。 */
{ /* 开始当前结构或代码块。 */
  if (address != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    copyout(parent->pagetable, address, &status, sizeof(status)); /* 函数作用：把内核缓冲区逐页复制到用户空间。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：回收僵尸子进程，或保存等待状态并切换到可运行子进程。 */
int /* 声明下方函数的返回类型与可见范围。 */
proc_wait(uint64 status_addr) /* 函数作用：等待、调度或回收当前进程的子进程。 */
{ /* 开始当前结构或代码块。 */
  int i; /* 声明循环下标。 */
  int have_child = 0; /* 记录当前进程是否拥有子进程。 */
  struct proc *runnable = 0; /* 记录找到的第一个可运行子进程。 */

  for (i = 0; i < LAB2_NPROC; i++) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    struct proc *p = &procs[i]; /* 取得当前扫描槽位的PCB地址。 */
    if (p->parent != current || p->state == PROC_UNUSED) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      continue; /* 跳过当前空洞或无效条目并检查下一项。 */
    have_child = 1; /* 找到父指针匹配的槽位，记住当前进程确实有子进程。 */
    if (p->state == PROC_ZOMBIE) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      int pid = p->pid; /* 保存待返回或回收的进程号。 */
      if (status_addr != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        copyout(current->pagetable, status_addr, &p->xstate, sizeof(p->xstate)); /* 函数作用：把内核缓冲区逐页复制到用户空间。 */
      release_vm(p); /* 函数作用：释放进程拥有的地址空间资源。 */
      memset(p, 0, sizeof(*p)); /* 按指定值初始化目标内存区域。 */
      return pid; /* 把当前计算结果或对象地址返回调用方。 */
    } /* 结束当前条件、循环或函数代码块。 */
    if (p->state == PROC_RUNNABLE && runnable == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      runnable = p; /* 保存第一个可切换执行的子进程。 */
  } /* 结束当前条件、循环或函数代码块。 */
  if (!have_child || runnable == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  current->wait_status_addr = status_addr; /* 保存父进程用户状态地址，供子进程退出时跨页表写回。 */
  current->state = PROC_WAITING; /* 父进程暂停，等待正在运行的子进程退出。 */
  current = runnable;                               /* 独立页表无需复制整段用户内存。 */
  current->state = PROC_RUNNING; /* 将刚选择的子进程标为当前运行进程。 */
  return 0;                                         /* syscall 防护不会把该值写进子现场。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：结束当前子进程、释放其地址空间并恢复等待父进程。 */
void /* 声明下方函数的返回类型与可见范围。 */
proc_exit(int status) /* 函数作用：结束子进程并恢复等待中的父进程。 */
{ /* 开始当前结构或代码块。 */
  struct proc *exiting = current; /* 保存即将退出的当前子进程。 */
  struct proc *parent = exiting->parent; /* 保存退出进程的父进程。 */
  int pid = exiting->pid; /* 保存待返回或回收的进程号。 */

  if (parent == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("init process exited"); /* 检测到不可恢复的内核状态并停止运行。 */
  if (parent->state != PROC_WAITING) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("child exited without waiting parent"); /* 检测到不可恢复的内核状态并停止运行。 */
  exiting->xstate = status; /* 保存exit参数，供父进程wait取得。 */
  exiting->state = PROC_ZOMBIE; /* 在资源和父进程状态处理完成前暂时标记为僵尸。 */
  store_wait_status(parent, parent->wait_status_addr, status); /* 函数作用：经父进程页表安全写回退出状态。 */
  parent->trapframe->a0 = (uint64)pid;               /* 父进程 wait 返回子 PID。 */
  release_vm(exiting);                               /* 当前运行在独立内核栈，可释放用户 VM。 */
  exiting->state = PROC_UNUSED;                      /* 不清 PCB，避免抹掉当前内核调用栈。 */
  exiting->parent = 0; /* 解除父子关系，使该槽位以后可以安全复用。 */
  current = parent; /* 把活动进程切回已经等待的父进程。 */
  current->state = PROC_RUNNING; /* 让父进程从wait系统调用之后继续用户态执行。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：按 EAGER 语义调整堆，失败保持旧边界并返回 -1。 */
uint64 /* 声明下方函数的返回类型与可见范围。 */
proc_sbrk(long amount, int policy) /* 函数作用：按EAGER策略扩大或缩小用户堆。 */
{ /* 开始当前结构或代码块。 */
  uint64 oldsz = current->sz; /* 保存调整前的用户堆边界。 */
  uint64 newsz; /* 保存检查后的目标用户堆边界。 */

  if (policy != SBRK_EAGER && policy != SBRK_LAZY) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return (uint64)-1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  if (amount == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return oldsz; /* 把当前计算结果或对象地址返回调用方。 */
  if (amount > 0) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    newsz = oldsz + (uint64)amount; /* 计算扩展后的目标break，并在下一步检查溢出。 */
    if (newsz < oldsz || newsz >= TRAPFRAME) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return (uint64)-1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    if (uvmalloc(current->pagetable, oldsz, newsz, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
                 PTE_R | PTE_W | PTE_U) < 0) /* 堆页只允许用户读写，OOM时由uvmalloc完整回滚。 */
      return (uint64)-1;                            /* uvmalloc 已回滚部分分配。 */
  } else { /* 前一条件不成立时进入备用处理分支。 */
    uint64 shrink = (uint64)(-amount); /* 保存本次请求收缩的字节数。 */
    if (shrink > oldsz || oldsz - shrink < current->heap_base) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return (uint64)-1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    newsz = oldsz - shrink; /* 通过下溢检查后计算收缩后的目标break。 */
    uvmdealloc(current->pagetable, oldsz, newsz); /* 函数作用：收缩用户区间并释放多余页面。 */
  } /* 结束当前条件、循环或函数代码块。 */
  current->sz = newsz; /* 只有分配或回收全部成功后才提交新的进程break。 */
  return oldsz;                                      /* 标准 sbrk 返回新区域旧起点。 */
} /* 结束当前条件、循环或函数代码块。 */
