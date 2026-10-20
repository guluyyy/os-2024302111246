/* Lab3 派生脚本：原 user.ld 保持不动，仅增加页对齐段边界元数据。 */
OUTPUT_ARCH(riscv)                 /* 指定输出目标为RISC-V ELF。 */
ENTRY(main)                        /* 指定用户程序从main符号开始执行。 */

SECTIONS                           /* 开始描述用户ELF各段的虚拟地址布局。 */
{
  . = 0x0;                        /* 用户程序链接到虚拟地址0，由独立用户页表提供映射。 */
  .text : { *(.text .text.*) }    /* 收集全部代码段，最终映射为R-XU。 */
  .rodata : { *(.rodata .rodata.*) } /* 把只读常量紧随代码放入同一只读执行区域。 */

  . = ALIGN(0x1000);              /* 把可写数据推进到新页，避免与代码共享PTE权限。 */
  __user_data_start = .;          /* 导出数据页起点供Makefile写入内嵌程序元数据。 */
  .data : { *(.data .data.*) }    /* 收集已初始化全局数据，运行时映射为RW-U。 */
  .bss : {                        /* 开始收集需要在装载时清零的未初始化数据。 */
    *(.bss .bss.*)                /* 收集普通BSS输入节。 */
    *(.sbss*)                     /* 收集小数据模型使用的SBSS输入节。 */
  }                               /* 结束BSS输出节。 */
  __user_image_end = .;           /* 导出含BSS的逻辑镜像末端供exec确定布局。 */
  end = .;                        /* 保留传统end符号供用户程序或工具查询。 */
}                                 /* 结束用户ELF段布局。 */
