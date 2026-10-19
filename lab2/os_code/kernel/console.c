/*
 * Lab2 UART 控制台。
 *
 * 输出仍采用可靠的同步 THR 轮询；输入改为 UART 接收中断驱动。中断
 * 处理程序把字符写进环形缓冲区，read 系统调用从缓冲区消费。启动时
 * 可短暂启用 lab1 个人 Banner 协议，随后普通 Shell 输出保持明文。
 */

#include "types.h"       /* 引入 uint8、uint、uint64 等基本类型。 */
#include "memlayout.h"   /* 引入 QEMU virt 的 UART0 MMIO 基地址。 */
#include "course_sid.h"  /* 引入个人协议、缓冲区和节流参数。 */
#include "riscv.h"       /* 引入 S 态中断开关与状态查询函数。 */

/* 16550 UART 寄存器相对 UART0 的字节偏移。 */
#define UART_RHR 0 /* 读：Receive Holding Register，接收保持寄存器 */
#define UART_THR 0 /* 写：Transmit Holding Register，发送保持寄存器 */
#define UART_IER 1 /* Interrupt Enable Register，中断使能寄存器 */
#define UART_FCR 2 /* FIFO Control Register，FIFO 控制寄存器 */
#define UART_LCR 3 /* Line Control Register，线路控制寄存器 */
#define UART_LSR 5 /* Line Status Register，线路状态寄存器 */

/* LCR/FCR/LSR 中本实验需要的位。 */
#define LCR_BAUD_LATCH  (1 << 7) /* 置 LCR.DLAB，使偏移 0/1 访问波特率除数。 */
#define LCR_EIGHT_BITS  (3 << 0) /* 设置每个串口字符包含 8 个数据位。 */
#define FCR_FIFO_ENABLE (1 << 0) /* 打开 16550 收发 FIFO。 */
#define FCR_FIFO_CLEAR  (3 << 1) /* 同时清空接收 FIFO 和发送 FIFO。 */
#define IER_RX_ENABLE    (1 << 0) /* 允许“接收数据可用”事件产生 UART 中断。 */
#define LSR_RX_READY     (1 << 0) /* LSR 位 0 表示接收保持寄存器中已有字符。 */
#define LSR_TX_IDLE      (1 << 5) /* LSR 位 5 表示发送保持寄存器可以接收新字符。 */

/*
 * 每发送 16 + 学号低四位 个物理字节执行一次节流。
 * 当前学号得到 30，但必须保留 COURSE_SID 公式，禁止硬编码个人结果。
 */
#define THROTTLE_PERIOD (16U + (uint)(COURSE_SID % 16U))

/* nop 次数同样由学号派生，使循环具有清楚的个人参数来源。 */
#define THROTTLE_NOPS (64U + (uint)(COURSE_SID % 64U))

/* 距离上一次节流已经实际写入 UART 的字节数；.bss 清零后初值为 0。 */
static uint bytes_since_throttle; /* 统计距离上次节流已发送的物理字节。 */
static int banner_mode;           /* 非零时启用 Lab1 个性化 Banner 转义。 */

/*
 * r：已被 read 取走的位置；w：已写入位置；commit：允许读者看见的位置。
 * 三者均单调递增，数组索引时再取模，避免回绕时的满/空歧义。
 */
static struct {                    /* 定义单核 UART 生产者/消费者共享状态。 */
  char data[LAB2_BUF_SIZE];        /* 按个人容量保存尚未被 read 消费的字符。 */
  uint64 r;                        /* 指向下一个要被 read 取走的逻辑位置。 */
  uint64 w;                        /* 指向下一个 UART 字符要写入的逻辑位置。 */
  uint64 commit;                   /* 标记当前允许 read 看见的逻辑末端。 */
  uint64 dropped;                  /* 统计缓冲区满时被丢弃的输入字符。 */
} input;                           /* 只创建一个全局控制台输入缓冲区。 */

/* 通过 volatile 指针访问 MMIO，防止编译器缓存或删除硬件读写。 */
/* 函数作用：根据 UART 寄存器偏移计算对应的 MMIO 字节地址。 */
static inline volatile uint8 *                 /* 返回必须每次真实访问硬件的字节指针。 */
uart_register(uint offset)                     /* 计算 UART0 内指定寄存器的 MMIO 地址。 */
{                                              /* 开始执行基址加偏移计算。 */
  return (volatile uint8 *)(UART0 + offset);   /* 将整数硬件地址转换为 volatile 指针。 */
}                                              /* 结束 UART 寄存器地址计算。 */

/* 函数作用：从指定 UART MMIO 寄存器读取一个 8 位值。 */
static inline uint8                            /* 返回目标 UART 寄存器的 8 位当前值。 */
uart_read(uint offset)                         /* 从指定 UART MMIO 偏移读取一个字节。 */
{                                              /* 开始执行硬件寄存器读取。 */
  return *uart_register(offset);               /* 解引用 volatile 指针，强制产生一次总线读。 */
}                                              /* 结束 UART 寄存器读取。 */

/* 函数作用：向指定 UART MMIO 寄存器写入一个 8 位值。 */
static inline void                             /* 写寄存器不需要返回值。 */
uart_write(uint offset, uint8 value)           /* 向指定 UART MMIO 偏移写入一个字节。 */
{                                              /* 开始执行硬件寄存器写入。 */
  *uart_register(offset) = value;              /* 解引用 volatile 指针，强制产生一次总线写。 */
}                                              /* 结束 UART 寄存器写入。 */

/*
 * 初始化 16550 为 38400 波特率、8N1，并打开接收中断。发送中断不启用，
 * 因为输出路径采用同步轮询，不需要额外发送队列。
 */
/* 函数作用：初始化 16550 UART、输入环形缓冲区并开启接收中断。 */
void                                           /* 控制台初始化只配置状态，不返回结果。 */
consoleinit(void)                              /* 初始化 16550 线路、FIFO、计数器和接收中断。 */
{                                              /* 开始配置 UART 与内存输入状态。 */
  uart_write(UART_IER, 0x00);                  /* 先关闭全部 UART 中断，避免配置过程被打断。 */

  uart_write(UART_LCR, LCR_BAUD_LATCH);        /* 打开 DLAB，使偏移 0/1 指向波特率除数寄存器。 */
  uart_write(UART_THR, 0x03); /* 除数低字节。 */
  uart_write(UART_IER, 0x00); /* DLAB=1 时偏移 1 是除数高字节 DLM。 */

  uart_write(UART_LCR, LCR_EIGHT_BITS);        /* 关闭 DLAB 并选择 8 数据位、无校验、1 停止位。 */
  uart_write(UART_FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR); /* 开启并清空收发 FIFO。 */

  bytes_since_throttle = 0;                    /* 从零开始统计物理发送字节。 */
  banner_mode = 0;                             /* 默认普通明文输出，main 会按需短暂开启。 */
  input.r = input.w = input.commit = input.dropped = 0; /* 清空环形缓冲下标与丢字计数。 */

  /* DLAB 已清零，此时偏移 1 恢复为 IER。 */
  uart_write(UART_IER, IER_RX_ENABLE);         /* 配置完成后只打开接收数据中断。 */
}                                              /* 结束控制台初始化。 */

/* 函数作用：开启或关闭仅用于启动 Banner 的个性化字符输出协议。 */
void                                           /* 模式切换只更新全局标志。 */
console_set_banner_mode(int enabled)           /* 控制 console_putc 是否应用个人协议。 */
{                                              /* 开始规范化并保存模式参数。 */
  banner_mode = enabled != 0;                  /* 把任意非零参数统一转换为真。 */
}                                              /* 结束 Banner 模式切换。 */

/*
 * 同步发送一个原始字节。只有当 LSR bit5=1（THR 空闲）时才写 THR。
 * 启动阶段没有可用的时钟中断和调度器，因此忙等是可靠且可解释的选择。
 */
/* 函数作用：轮询等待 UART 发送寄存器空闲并同步发送一个原始字节。 */
void                                           /* 同步发送完成后不返回附加结果。 */
uartputc_sync(char c)                          /* 等待 THR 空闲后发送一个原始物理字节。 */
{                                              /* 开始轮询发送路径。 */
  while ((uart_read(UART_LSR) & LSR_TX_IDLE) == 0) /* 检查发送保持寄存器是否可写。 */
    ;                                          /* 硬件仍忙时保持空循环等待。 */

  uart_write(UART_THR, (uint8)c);              /* 把字符低 8 位写入发送保持寄存器。 */
}                                              /* 结束同步单字节发送。 */

/* 对已经写入 UART 的物理字节计数，并在达到个人周期时插入 nop。 */
/* 函数作用：发送一个物理字节，并按个人参数执行周期性节流。 */
static void                                    /* 物理发送函数通过 UART 产生效果，不返回值。 */
emit_physical_byte(char c)                     /* 发送一字节并执行个人化周期节流。 */
{                                              /* 开始一次物理字节发送。 */
  uint i;                                      /* 保存节流 nop 循环下标。 */

  uartputc_sync(c);                            /* 等待 UART 空闲并真正写出当前字节。 */

  bytes_since_throttle++;                      /* 物理写入成功后累加周期计数。 */
  if (bytes_since_throttle == THROTTLE_PERIOD) { /* 精确达到个人周期时插入一次节流。 */
    bytes_since_throttle = 0;                  /* 重置计数，为下一节流周期重新统计。 */
    for (i = 0; i < THROTTLE_NOPS; i++)        /* 执行个人参数规定次数的空操作。 */
      asm volatile("nop");                    /* 要求编译器保留一条真实 RISC-V nop。 */
  }                                            /* 结束是否到达节流周期的判断。 */
}                                              /* 结束物理字节发送。 */

/*
 * 输出一个逻辑字符并应用个人行协议。
 *
 * 协议 0：原样发送；
 * 协议 1：每个逻辑字节后追加 '.'，换行字节也不例外；
 * 协议 2：字符本身原样发送，最终校验和由上层在整段结束后输出。
 *
 * 本人的 LAB1_BANNER_PROTOCOL=1，因此编译后走第二个分支。
 */
/* 函数作用：输出一个逻辑字符，并在 Banner 模式下应用个人转义协议。 */
void                                           /* 字符通过 UART 输出，不返回结果。 */
console_putc(char c)                           /* 输出逻辑字符并按当前模式执行 Lab1 转义。 */
{                                              /* 开始输出一个逻辑字符。 */
  emit_physical_byte(c);                       /* 始终先发送字符本身。 */

  if (banner_mode) {                           /* 只有 Lab1 Banner 阶段才执行个人协议。 */
#if LAB1_BANNER_PROTOCOL == 1
    emit_physical_byte('.');                   /* 协议 1 要求每个逻辑字节后追加点号。 */
#elif LAB1_BANNER_PROTOCOL == 0
    /* 协议 0 无附加字节。 */
#elif LAB1_BANNER_PROTOCOL == 2
    /* 协议 2 的整段校验由上层负责；当前个人参数不走此分支。 */
#else
#error "未知的 LAB1_BANNER_PROTOCOL"
#endif
  }                                            /* 结束 Banner 模式判断。 */
}                                              /* 结束逻辑字符输出。 */

/* 写系统调用的底层实现。单核环境用关中断保证一段输出不被回显插入。 */
/* 函数作用：把一段内核可访问的数据连续写到 UART 控制台。 */
int                                            /* 返回成功写出的请求字节数。 */
console_write(const char *src, int n)          /* 连续发送内核可访问缓冲区的 n 个字节。 */
{                                              /* 开始不可被输入回显插入的输出临界区。 */
  int was_enabled = intr_get();                /* 记录调用前 S 态中断是否开启。 */
  int i;                                       /* 保存当前输出的缓冲区下标。 */

  intr_off();                                  /* 单核下关中断，避免 UART 回显插入整段输出。 */
  for (i = 0; i < n; i++)                     /* 按原顺序遍历请求的所有字节。 */
    console_putc(src[i]);                      /* 通过统一逻辑字符入口输出当前字节。 */
  if (was_enabled)                             /* 仅当调用前开中断时才恢复开启状态。 */
    intr_on();                                 /* 恢复调用者原来的中断环境。 */
  return n;                                    /* 同步路径全部成功，返回完整长度。 */
}                                              /* 结束连续控制台写。 */

/*
 * UART 接收中断的生产者。行缓冲模式只在回车/换行时推进 commit；字符
 * 流模式每写入一个字符都推进。缓冲区满时丢弃普通字符，但行尾仍会
 * 提交已有内容，防止超长行令读者永久等待。
 */
/* 函数作用：处理 UART 接收中断，回显字符并写入输入环形缓冲区。 */
void                                           /* 中断处理通过共享缓冲区传递结果。 */
uartintr(void)                                 /* 排空 UART 接收 FIFO 并提交可读输入。 */
{                                              /* 开始处理一次或多次接收字符。 */
  while ((uart_read(UART_LSR) & LSR_RX_READY) != 0) { /* 只要 FIFO 仍有字符就继续排空。 */
    char c = (char)uart_read(UART_RHR);         /* 从接收保持寄存器取出一个字符。 */

    if (c == '\b' || c == 0x7f) {              /* 同时识别 BS 和 DEL 两种退格编码。 */
      if (LAB2_BUF_SEMANTICS == 0 && input.w > input.commit) { /* 仅删除尚未提交行内字符。 */
        input.w--;                             /* 回退写游标，相当于撤销最后一个字符。 */
        console_putc('\b');                    /* 先让终端光标退回被删除字符的位置。 */
        console_putc(' ');                     /* 用空格覆盖屏幕上原来的字符。 */
        console_putc('\b');                    /* 再次退格，使光标停在正确输入位置。 */
      }                                        /* 没有可删除字符时保持屏幕和缓冲区不变。 */
      continue;                                /* 退格控制字符本身不写入输入缓冲区。 */
    }                                          /* 结束退格处理。 */

    /* 普通字符在确认不是退格后再回显，避免把 DEL 原样发送给终端。 */
    console_putc(c);                           /* 同步回显当前普通键盘字符。 */

    if (input.w - input.r < LAB2_BUF_SIZE) {   /* 未消费字符数小于容量表示仍有空槽。 */
      input.data[input.w % LAB2_BUF_SIZE] = c; /* 对单调写序号取模，写入实际数组槽。 */
      input.w++;                               /* 推进逻辑写位置，公布字符已经存入。 */
    } else {                                   /* 写入会覆盖未读字符时缓冲区已满。 */
      input.dropped++;                         /* 丢弃新字符并留下可诊断计数。 */
    }                                          /* 结束缓冲区满/非满分支。 */

#if LAB2_BUF_SEMANTICS == 0
    if (c == '\n' || c == '\r')               /* 行缓冲只有遇到回车或换行才提交。 */
      input.commit = input.w;                  /* 让 read 看见本行至当前写位置的内容。 */
#elif LAB2_BUF_SEMANTICS == 1
    input.commit = input.w;                    /* 字符流模式每收到一字符立即可读。 */
#else
#error "LAB2_BUF_SEMANTICS 只能为 0 或 1"
#endif
  }                                            /* UART FIFO 已空，结束接收循环。 */
}                                              /* 结束 UART 接收中断处理。 */

/*
 * read 的消费者。lab2 尚无 sleep/wakeup，空缓冲区时保持中断开启并忙等；
 * 这是手册允许的过渡实现，lab4 再用睡眠队列消除 CPU 浪费。
 */
/* 函数作用：从已提交的输入环形缓冲区读取字符到调用者缓冲区。 */
int                                            /* 返回实际复制的字符数。 */
console_read(char *dst, int n)                 /* 从已提交环形缓冲区读取一行或 n 字节。 */
{                                              /* 开始消费者读取循环。 */
  int copied = 0;                              /* 记录已经写入目标缓冲区的字符数。 */

  if (n <= 0)                                  /* 非正长度没有任何可复制空间。 */
    return 0;                                  /* 与常见 read 语义一致地返回零字节。 */

  for (;;) {                                   /* 直到读满、读到行尾或未来有输入。 */
    int was_enabled = intr_get();              /* 记录进入本次缓冲区检查前的中断状态。 */
    intr_off();                                /* 防止 uartintr 同时修改 r/w/commit。 */

    if (input.r < input.commit) {              /* r 落在 commit 前说明至少有一个已提交字符。 */
      char c = input.data[input.r % LAB2_BUF_SIZE]; /* 从逻辑读位置对应槽取出字符。 */
      input.r++;                               /* 消费字符并释放一个环形缓冲槽。 */
      if (was_enabled)                         /* 恢复调用者原来的开中断状态。 */
        intr_on();                             /* 在复制到用户目标前结束临界区。 */

      dst[copied++] = c;                       /* 写入目标并增加本次 read 的字符数。 */
      if (copied == n || c == '\n' || c == '\r') /* 满足长度或行结束条件即完成。 */
        return copied;                         /* 返回本次实际交付给用户的字节数。 */
      continue;                                /* 尚未结束时继续尝试消费同一提交区。 */
    }                                          /* 结束存在已提交字符的分支。 */

    if (was_enabled)                           /* 空缓冲时必须恢复中断，让 UART 能生产字符。 */
      intr_on();                               /* 重新允许接收中断后再进行下一次忙等检查。 */
  }                                            /* Lab2 无 sleep，因此循环等待 commit 推进。 */
}                                              /* 结束控制台读取。 */

