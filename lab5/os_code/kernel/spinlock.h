#ifndef LAB4_SPINLOCK_H                 /* 仅在尚未包含本头文件时声明锁结构。 */
#define LAB4_SPINLOCK_H                 /* 建立包含保护，避免结构体重复定义。 */

struct cpu;                            /* 前向声明：锁只记录持锁 CPU 的地址。 */

/* Lab4 自旋锁：locked 负责互斥，cpu 用于 holding() 的所有权检查。 */
struct spinlock {                       /* 描述一把带调试所有者的不可重入自旋锁。 */
  uint locked;                         /* 0 表示空闲，1 表示已经被某个 CPU 持有。 */
  const char *name;                    /* 调试名称，panic 时便于定位是哪一把锁。 */
  struct cpu *cpu;                     /* 当前持锁 CPU；释放后恢复为空指针。 */
};                                      /* 完成自旋锁内存布局定义。 */

#endif                                  /* 结束 LAB4_SPINLOCK_H 包含保护。 */
