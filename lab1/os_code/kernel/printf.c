/*
 * Lab1 最小 printf。
 *
 * 这是 freestanding 内核，链接时使用 -nostdlib，不能调用 libc 的
 * printf、strlen 或整数转换函数。这里仅依赖编译器提供的 stdarg.h，
 * 并把每个最终字符交给 console_putc()。
 */

#include <stdarg.h>

#include "types.h"

/* console.c 提供的协议化字符输出入口。 */
void console_putc(char c);

/* 小写数字表保证 %x 使用 a-f，而不是 A-F。 */
static const char digits[] = "0123456789abcdef";

/*
 * 按 base 逆序收集数字，再反向输出。
 * do-while 保证 value=0 时仍然输出一个字符 '0'。
 */
static void
print_unsigned(uint64 value, uint base)
{
  char buffer[32];
  uint index = 0;

  do {
    buffer[index++] = digits[value % base];
    value /= base;
  } while (value != 0);

  while (index > 0)
    console_putc(buffer[--index]);
}

/*
 * 输出 32 位有符号十进制整数。
 * 使用无符号减法求负数的模，连 INT_MIN 也不会触发有符号溢出。
 */
static void
print_decimal(int value)
{
  uint magnitude;

  if (value < 0) {
    console_putc('-');
    magnitude = 0U - (uint)value;
  } else {
    magnitude = (uint)value;
  }

  print_unsigned((uint64)magnitude, 10);
}

/* 输出以 '\0' 结尾的字符串；空指针使用可诊断文本代替崩溃。 */
static void
print_string(const char *text)
{
  if (text == 0)
    text = "(null)";

  while (*text != '\0')
    console_putc(*text++);
}

/*
 * 支持 Lab1 所需的最小格式集合：
 *   %d 有符号十进制；%x 无符号小写十六进制；
 *   %s 字符串；%c 单字符；%% 百分号本身。
 * 未知格式不会静默丢失，而是原样输出 '%' 和格式字符。
 */
void
printf(const char *format, ...)
{
  va_list arguments;

  va_start(arguments, format);

  while (*format != '\0') {
    if (*format != '%') {
      console_putc(*format++);
      continue;
    }

    format++;
    if (*format == '\0') {
      console_putc('%');
      break;
    }

    switch (*format) {
    case 'd':
      print_decimal(va_arg(arguments, int));
      break;
    case 'x':
      print_unsigned((uint64)va_arg(arguments, uint), 16);
      break;
    case 's':
      print_string(va_arg(arguments, const char *));
      break;
    case 'c':
      console_putc((char)va_arg(arguments, int));
      break;
    case '%':
      console_putc('%');
      break;
    default:
      console_putc('%');
      console_putc(*format);
      break;
    }

    format++;
  }

  va_end(arguments);
}

