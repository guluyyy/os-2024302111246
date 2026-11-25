/* Lab4 自旋锁与嵌套关中断实现。单核验收仍严格维护锁所有权和 noff。 */
#include "types.h"                     /* 引入 uint 等固定宽度整数类型。 */
#include "riscv.h"                     /* 引入 S 态中断开关与状态读取接口。 */
#include "proc.h"                      /* 引入 struct cpu 以及 mycpu()。 */
#include "spinlock.h"                  /* 引入 struct spinlock 的字段定义。 */
#include "defs.h"                      /* 引入 panic 和锁函数公共声明。 */

/* 函数作用：把一块静态锁对象初始化为未持有状态。 */
void                                    /* 初始化只修改锁对象，不返回结果。 */
initlock(struct spinlock *lk, const char *name) /* 初始化指定锁并绑定调试名称。 */
{                                       /* 开始建立一把确定为空闲状态的锁。 */
  lk->name = name;                     /* 保存诊断名称。 */
  lk->locked = 0;                      /* 发布未上锁的初始状态。 */
  lk->cpu = 0;                         /* 初始没有 CPU 拥有此锁。 */
}                                       /* 完成锁对象初始化。 */

/* 函数作用：查询当前 CPU 是否持有指定锁。调用时应已关闭中断。 */
int                                     /* 返回 1 表示本 CPU 持锁，否则返回 0。 */
holding(struct spinlock *lk)            /* 同时检查锁位和记录的所有者。 */
{                                       /* 开始执行锁所有权判定。 */
  return lk->locked && lk->cpu == mycpu(); /* 同时校验锁状态和所有者。 */
}                                       /* 结束锁所有权判定。 */

/* 函数作用：嵌套关闭本 CPU 中断，防止持锁代码被中断后再次申请同锁。 */
void                                    /* 关中断操作不返回额外结果。 */
push_off(void)                          /* 进入一层不可中断临界区。 */
{                                       /* 开始保存中断状态并增加嵌套深度。 */
  int old = intr_get();                /* 记住最外层调用前的中断状态。 */
  intr_off();                          /* 在更新嵌套计数前先禁止抢占。 */
  if (mycpu()->noff == 0)              /* 只有最外层需要保存原始状态。 */
    mycpu()->intena = old;             /* 记录离开全部临界区后是否应开中断。 */
  mycpu()->noff++;                     /* 增加本 CPU 的关中断嵌套深度。 */
}                                       /* 完成一层嵌套关中断。 */

/* 函数作用：退出一层关中断区，并在最外层恢复进入前的中断状态。 */
void                                    /* 恢复操作不返回额外结果。 */
pop_off(void)                           /* 退出一层不可中断临界区。 */
{                                       /* 开始检查并减少关中断嵌套深度。 */
  struct cpu *c = mycpu();             /* 取得当前 CPU 的嵌套状态。 */
  if (intr_get())                      /* 临界区内不允许有人提前打开中断。 */
    panic("pop_off: interruptible");   /* 临界区被错误开中断时立即停止内核。 */
  if (c->noff < 1)                     /* 防止 pop 次数多于 push 次数。 */
    panic("pop_off: underflow");       /* push/pop 不配对会破坏中断恢复语义。 */
  c->noff--;                           /* 退出当前一层嵌套。 */
  if (c->noff == 0 && c->intena)       /* 最外层且原来开中断时才恢复。 */
    intr_on();                          /* 恢复最外层进入前的开中断状态。 */
}                                       /* 完成一层嵌套关中断退出。 */

/* 函数作用：原子申请锁，并把当前 CPU 记录为锁的所有者。 */
void                                    /* 成功后由调用者持锁，不返回状态码。 */
acquire(struct spinlock *lk)            /* 自旋等待并取得指定锁。 */
{                                       /* 开始不可中断的原子取锁过程。 */
  push_off();                          /* 先关中断，杜绝同 CPU 中断重入死锁。 */
  if (holding(lk))                     /* 自己重复申请非递归锁属于内核错误。 */
    panic("acquire: recursive");       /* 非递归锁禁止当前 CPU 重复申请。 */
  while (__sync_lock_test_and_set(&lk->locked, 1) != 0) /* 原子交换直到抢锁成功。 */
    ;                                   /* 锁忙时不读写受保护数据，只持续重试。 */
  __sync_synchronize();                /* 禁止临界区访问越过加锁点。 */
  lk->cpu = mycpu();                   /* 在互斥已经建立后发布所有者。 */
}                                       /* 调用者从此可以安全访问锁保护的数据。 */

/* 函数作用：清除锁所有权并恢复与 acquire 配对的中断嵌套状态。 */
void                                    /* 释放完成后不返回状态码。 */
release(struct spinlock *lk)            /* 释放当前 CPU 已持有的指定锁。 */
{                                       /* 开始清除所有者并发布解锁状态。 */
  if (!holding(lk))                    /* 只有真正的持锁 CPU 才能释放。 */
    panic("release: not owner");       /* 防止非所有者破坏互斥关系。 */
  lk->cpu = 0;                         /* 先清除调试所有者信息。 */
  __sync_synchronize();                /* 确保临界区写入先于解锁对外可见。 */
  __sync_lock_release(&lk->locked);    /* 原子写 0 发布锁已空闲。 */
  pop_off();                           /* 离开本锁对应的关中断层级。 */
}                                       /* 完成解锁并退出对应中断临界层。 */
