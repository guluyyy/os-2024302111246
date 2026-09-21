/*
 * Lab1 轮询式 UART 控制台。
 *
 * 本文件刻意分成三层：
 *   uartputc_sync() 只处理 16550 寄存器和一个物理字节；
 *   emit_physical_byte() 统计真实发送字节并执行个性化节流；
 *   console_putc() 实现个人 Banner 协议。
 *
 * 这种分层可以保证硬件协议、节流规则和字符修饰互不混杂。Lab1 不做
 * UART 接收和中断驱动输出，所有字符都通过查询 LSR bit5 同步发送。
 */

#include "types.h"
#include "memlayout.h"
#include "course_sid.h"

/* 16550 UART 寄存器相对 UART0 的字节偏移。 */
#define UART_THR 0 /* 写：Transmit Holding Register，发送保持寄存器 */
#define UART_IER 1 /* Interrupt Enable Register，中断使能寄存器 */
#define UART_FCR 2 /* FIFO Control Register，FIFO 控制寄存器 */
#define UART_LCR 3 /* Line Control Register，线路控制寄存器 */
#define UART_LSR 5 /* Line Status Register，线路状态寄存器 */

/* LCR/FCR/LSR 中本实验需要的位。 */
#define LCR_BAUD_LATCH  (1 << 7)
#define LCR_EIGHT_BITS  (3 << 0)
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR  (3 << 1)
#define LSR_TX_IDLE      (1 << 5)

/*
 * 每发送 16 + 学号低四位 个物理字节执行一次节流。
 * 当前学号得到 30，但必须保留 COURSE_SID 公式，禁止硬编码个人结果。
 */
#define THROTTLE_PERIOD (16U + (uint)(COURSE_SID % 16U))

/* nop 次数同样由学号派生，使循环具有清楚的个人参数来源。 */
#define THROTTLE_NOPS (64U + (uint)(COURSE_SID % 64U))

/* 距离上一次节流已经实际写入 UART 的字节数；.bss 清零后初值为 0。 */
static uint bytes_since_throttle;

/* 通过 volatile 指针访问 MMIO，防止编译器缓存或删除硬件读写。 */
static inline volatile uint8 *
uart_register(uint offset)
{
  return (volatile uint8 *)(UART0 + offset);
}

static inline uint8
uart_read(uint offset)
{
  return *uart_register(offset);
}

static inline void
uart_write(uint offset, uint8 value)
{
  *uart_register(offset) = value;
}

/*
 * 初始化 16550 为 38400 波特率、8 数据位、无校验，并关闭 UART 中断。
 * QEMU 对实际波特率不敏感，但显式配置能让启动状态确定且易于答辩。
 */
void
consoleinit(void)
{
  uart_write(UART_IER, 0x00);

  uart_write(UART_LCR, LCR_BAUD_LATCH);
  uart_write(UART_THR, 0x03); /* 除数低字节。 */
  uart_write(UART_IER, 0x00); /* DLAB=1 时偏移 1 是除数高字节 DLM。 */

  uart_write(UART_LCR, LCR_EIGHT_BITS);
  uart_write(UART_FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  bytes_since_throttle = 0;
}

/*
 * 同步发送一个原始字节。只有当 LSR bit5=1（THR 空闲）时才写 THR。
 * 启动阶段没有可用的时钟中断和调度器，因此忙等是可靠且可解释的选择。
 */
void
uartputc_sync(char c)
{
  while ((uart_read(UART_LSR) & LSR_TX_IDLE) == 0)
    ;

  uart_write(UART_THR, (uint8)c);
}

/* 对已经写入 UART 的物理字节计数，并在达到个人周期时插入 nop。 */
static void
emit_physical_byte(char c)
{
  uint i;

  uartputc_sync(c);

  bytes_since_throttle++;
  if (bytes_since_throttle == THROTTLE_PERIOD) {
    bytes_since_throttle = 0;
    for (i = 0; i < THROTTLE_NOPS; i++)
      asm volatile("nop");
  }
}

/*
 * 输出一个逻辑字符并应用个人行协议。
 *
 * 协议 0：原样发送；
 * 协议 1：每个逻辑字节后追加 '.'，换行字节也不例外；
 * 协议 2：字符本身原样发送，最终校验和由上层在整段结束后输出。
 *
 * 本人的 LAB1_BANNER_PROTOCOL=1，因此编译后走第二个分支。
 */
void
console_putc(char c)
{
  emit_physical_byte(c);

#if LAB1_BANNER_PROTOCOL == 1
  emit_physical_byte('.');
#elif LAB1_BANNER_PROTOCOL == 0
  /* 协议 0 无附加字节。 */
#elif LAB1_BANNER_PROTOCOL == 2
  /* 协议 2 的校验和属于整段语义，不应在单字符硬件层计算。 */
#else
#error "未知的 LAB1_BANNER_PROTOCOL"
#endif
}

