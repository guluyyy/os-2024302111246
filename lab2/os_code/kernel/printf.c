/*
 * Lab2 最小 printf。
 *
 * 这是 freestanding 内核，链接时使用 -nostdlib，不能调用 libc 的
 * printf、strlen 或整数转换函数。这里仅依赖编译器提供的 stdarg.h，
 * 并把每个最终字符交给 console_putc()。
 */

#include <stdarg.h> /* 引入可变参数列表 va_list 与 va_arg 宏。 */

#include "types.h" /* 引入 uint 和 uint64 数字类型。 */
#include "riscv.h" /* 引入 panic 使用的 S 态中断控制函数。 */

/* console.c 提供最终字符输出入口。 */
void console_putc(char c); /* 声明最终单字符 UART 输出函数。 */

/* 小写数字表保证 %x 使用 a-f，而不是 A-F。 */
static const char digits[] = "0123456789abcdef"; /* 建立数值 0..15 到小写字符的查表。 */

/*
 * 按 base 逆序收集数字，再反向输出。
 * do-while 保证 value=0 时仍然输出一个字符 '0'。
 */
/* 函数作用：按指定进制把无符号整数逐位输出到控制台。 */
static void                                  /* 数字通过字符输出，不返回值。 */
print_unsigned(uint64 value, uint base)      /* 按指定进制输出无符号 64 位整数。 */
{                                            /* 开始执行除基取余转换。 */
  char buffer[32];                           /* 暂存逆序数字；二进制 64 位也需另扩，本实验只用 10/16。 */
  uint index = 0;                            /* 指向下一个逆序数字存放位置。 */

  do {                                       /* 至少执行一次，保证数值 0 可见。 */
    buffer[index++] = digits[value % base];  /* 用余数查表并逆序保存最低位。 */
    value /= base;                           /* 丢弃已经保存的最低位。 */
  } while (value != 0);                      /* 商非零时继续提取更高位。 */

  while (index > 0)                          /* 从逆序缓冲区末端向前输出。 */
    console_putc(buffer[--index]);            /* 先回退下标，再输出当前最高剩余位。 */
}                                            /* 结束无符号整数输出。 */

/*
 * 输出 32 位有符号十进制整数。
 * 使用无符号减法求负数的模，连 INT_MIN 也不会触发有符号溢出。
 */
/* 函数作用：正确处理正负号并输出一个有符号十进制整数。 */
static void                                  /* 十进制文本通过控制台输出。 */
print_decimal(int value)                     /* 输出一个有符号 32 位十进制整数。 */
{                                            /* 开始处理符号和绝对值。 */
  uint magnitude;                            /* 用无符号数保存不溢出的数值大小。 */

  if (value < 0) {                            /* 负数需要先输出符号。 */
    console_putc('-');                        /* 在数字前写出减号。 */
    magnitude = 0U - (uint)value;            /* 用无符号模运算安全取得绝对值。 */
  } else {                                    /* 零和正数无需符号转换。 */
    magnitude = (uint)value;                  /* 直接转换为无符号大小。 */
  }                                           /* 结束正负号分支。 */

  print_unsigned((uint64)magnitude, 10);      /* 复用无符号转换输出十进制数字。 */
}                                             /* 结束有符号十进制输出。 */

/* 输出以 '\0' 结尾的字符串；空指针使用可诊断文本代替崩溃。 */
/* 函数作用：输出 NUL 结尾字符串，并为 NULL 指针提供安全占位文本。 */
static void                                   /* 字符串逐字输出，不返回结果。 */
print_string(const char *text)                /* 输出一个 NUL 结尾的只读字符串。 */
{                                             /* 开始检查并遍历字符串。 */
  if (text == 0)                              /* 防止解引用调用者传入的空指针。 */
    text = "(null)";                          /* 用明确诊断文本替代无效地址。 */

  while (*text != '\0')                       /* 逐字符处理直到字符串终止符。 */
    console_putc(*text++);                    /* 输出当前字符并推进源指针。 */
}                                             /* 结束字符串输出。 */

/*
 * 支持实验所需的最小格式集合：
 *   %d 有符号十进制；%x 无符号小写十六进制；
 *   %p 64 位地址；%s 字符串；%c 单字符；%% 百分号本身。
 * 未知格式不会静默丢失，而是原样输出 '%' 和格式字符。
 */
/* 函数作用：解析最小格式串并把格式化结果写入内核控制台。 */
void                                          /* printf 直接写控制台，不返回字符数。 */
printf(const char *format, ...)               /* 解析最小格式串和其后的可变参数。 */
{                                             /* 开始格式化输出。 */
  va_list arguments;                          /* 保存当前可变参数遍历状态。 */

  va_start(arguments, format);                /* 从最后一个固定参数后初始化参数读取位置。 */

  while (*format != '\0') {                   /* 逐字符扫描完整格式字符串。 */
    if (*format != '%') {                     /* 普通字符不需要格式解释。 */
      console_putc(*format++);                /* 原样输出并前进到下一个格式字符。 */
      continue;                               /* 跳过本轮剩余格式分派逻辑。 */
    }                                         /* 当前字符是格式引导符。 */

    format++;                                  /* 移到百分号后的格式类型字符。 */
    if (*format == '\0') {                    /* 百分号恰好位于末尾时没有类型字符。 */
      console_putc('%');                      /* 保留孤立百分号，避免静默丢失。 */
      break;                                  /* 格式串已经结束，退出扫描。 */
    }                                         /* 结束孤立百分号处理。 */

    switch (*format) {                          /* 根据类型字符选择参数读取和输出方式。 */
    case 'd':                                   /* %d 表示有符号十进制 int。 */
      print_decimal(va_arg(arguments, int));    /* 取出一个 int 并按十进制输出。 */
      break;                                    /* 结束 %d 分支。 */
    case 'x':                                   /* %x 表示无符号小写十六进制 uint。 */
      print_unsigned((uint64)va_arg(arguments, uint), 16); /* 取出 uint 并按 16 进制输出。 */
      break;                                    /* 结束 %x 分支。 */
    case 'p':                                   /* %p 表示带 0x 前缀的 64 位地址。 */
      console_putc('0');                        /* 输出地址前缀的字符 0。 */
      console_putc('x');                        /* 输出地址前缀的字符 x。 */
      print_unsigned(va_arg(arguments, uint64), 16); /* 取出 64 位地址并输出十六进制数值。 */
      break;                                    /* 结束 %p 分支。 */
    case 's':                                   /* %s 表示 NUL 结尾字符串指针。 */
      print_string(va_arg(arguments, const char *)); /* 取出字符指针并安全输出字符串。 */
      break;                                    /* 结束 %s 分支。 */
    case 'c':                                   /* %c 表示经默认提升传入的单字符。 */
      console_putc((char)va_arg(arguments, int)); /* 以 int 取参后截成一个字符输出。 */
      break;                                    /* 结束 %c 分支。 */
    case '%':                                   /* %% 表示不消费参数的字面百分号。 */
      console_putc('%');                        /* 输出一个百分号字符。 */
      break;                                    /* 结束 %% 分支。 */
    default:                                    /* 未知格式符不属于支持集合。 */
      console_putc('%');                        /* 先保留原格式引导百分号。 */
      console_putc(*format);                    /* 再原样输出未知类型字符。 */
      break;                                    /* 结束未知格式分支。 */
    }                                           /* 结束格式类型分派。 */

    format++;                                   /* 前进到当前格式项后的下一个字符。 */
  }                                             /* 完成整个格式字符串扫描。 */

  va_end(arguments);                            /* 结束可变参数遍历，满足 C 接口要求。 */
}                                               /* 结束格式化输出。 */

/* 内核不可恢复错误统一打印并停机，避免继续破坏现场。 */
/* 函数作用：打印不可恢复的内核错误信息并永久停止当前 CPU。 */
void                                            /* noreturn 语义由头文件声明，函数不会返回。 */
panic(const char *message)                     /* 输出内核不可恢复错误并安全停机。 */
{                                               /* 开始 panic 终止路径。 */
  intr_off();                                   /* 禁止后续中断改变或掩盖故障现场。 */
  printf("panic: %s\n", message);              /* 输出固定前缀和调用者提供的原因。 */
  for (;;)                                      /* 永久循环，禁止继续执行已不可信代码。 */
    asm volatile("wfi");                       /* 等待事件以降低停机循环资源消耗。 */
}                                               /* panic 按设计永远无法到达函数末尾。 */

