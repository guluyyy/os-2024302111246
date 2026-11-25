#ifndef LAB5_VM_H /* 若尚未包含本文件，则进入 Lab5 页表声明区域。 */
#define LAB5_VM_H /* 建立包含保护，防止页表宏被重复处理。 */

/* RISC-V 为软件保留 PTE[9:8]；Lab5 使用 bit 8 标记合法的 COW 映射。 */
#define PTE_COW (1L << 8) /* 只能与已清除 PTE_W 的用户叶页表项同时存在。 */

/* sbrk 的策略参数；Lab3 对 LAZY 请求也按 EAGER 行为实现。 */
#define SBRK_EAGER 1 /* 策略1要求系统调用期间立即分配并映射全部物理页。 */
#define SBRK_LAZY  2 /* 策略2在本阶段按课程要求兼容为同样的EAGER行为。 */

#endif /* 结束 LAB5_VM_H 包含保护范围。 */
