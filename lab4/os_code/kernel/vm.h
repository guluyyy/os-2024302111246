#ifndef LAB3_VM_H /* 若尚未包含本文件，则进入sbrk策略声明区域。 */
#define LAB3_VM_H /* 建立包含保护，防止策略宏被重复处理。 */

/* sbrk 的策略参数；Lab3 对 LAZY 请求也按 EAGER 行为实现。 */
#define SBRK_EAGER 1 /* 策略1要求系统调用期间立即分配并映射全部物理页。 */
#define SBRK_LAZY  2 /* 策略2在本阶段按课程要求兼容为同样的EAGER行为。 */

#endif /* 结束LAB3_VM_H包含保护范围。 */
