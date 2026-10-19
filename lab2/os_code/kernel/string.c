/* freestanding 内核使用的最小字符串与内存函数，不依赖 libc。 */
#include "types.h" /* 引入 uchar 和 uint64 等 freestanding 基本类型。 */

/* 函数作用：用指定字节值填充一段连续内存。 */
void *                                      /* 返回原始目标地址，保持标准 memset 接口语义。 */
memset(void *dst, int value, uint64 count)  /* 用 value 的低 8 位填充 dst 开始的 count 个字节。 */
{                                           /* 开始执行逐字节填充。 */
  uchar *p = (uchar *)dst;                  /* 把无类型目标地址转换为可逐字节递增的指针。 */
  while (count-- != 0)                      /* 只要仍有字节未填充就继续循环。 */
    *p++ = (uchar)value;                    /* 写入一个字节，并把指针移动到下一个位置。 */
  return dst;                               /* 返回调用者传入的目标首地址。 */
}                                           /* 结束 memset。 */

/* 函数作用：在允许源区和目标区重叠的情况下安全复制内存。 */
void *                                               /* 返回原始目标地址，保持标准 memmove 接口语义。 */
memmove(void *dst, const void *src, uint64 count)    /* 安全复制 count 字节，允许源区和目标区重叠。 */
{                                                    /* 开始判断复制方向并搬运数据。 */
  uchar *d = (uchar *)dst;                           /* 建立可写的逐字节目标指针。 */
  const uchar *s = (const uchar *)src;               /* 建立只读的逐字节源指针。 */

  if (s > d) {                                       /* 源地址在目标地址之后时，从前向后复制不会覆盖未读数据。 */
    while (count-- != 0)                             /* 逐个处理剩余字节。 */
      *d++ = *s++;                                   /* 复制当前字节并同时推进两个指针。 */
  } else {                                           /* 源地址不高于目标地址，可能需要反向复制。 */
    d += count;                                      /* 把目标指针移到目标区末尾后一字节。 */
    s += count;                                      /* 把源指针移到源区末尾后一字节。 */
    while (count-- != 0)                             /* 从尾到头逐字节处理。 */
      *--d = *--s;                                   /* 先回退指针再复制，避免重叠覆盖。 */
  }                                                  /* 结束复制方向分支。 */
  return dst;                                        /* 返回调用者传入的目标首地址。 */
}                                                    /* 结束 memmove。 */

/* 函数作用：按字节比较两个 NUL 结尾字符串的字典序。 */
int                                                   /* 返回负数、零或正数表示字典序关系。 */
strcmp(const char *left, const char *right)           /* 比较两个以 NUL 结尾的字符串。 */
{                                                     /* 开始逐字符比较。 */
  while (*left != '\0' && *left == *right) {          /* 当前字符相同且尚未到左串末尾时继续。 */
    left++;                                           /* 左串指针前进到下一个字符。 */
    right++;                                          /* 右串指针同步前进到下一个字符。 */
  }                                                   /* 到达差异字符或字符串末尾。 */
  return (uchar)*left - (uchar)*right;                /* 用首个不同字符的无符号差值作为结果。 */
}                                                     /* 结束 strcmp。 */

/* 函数作用：计算一个 NUL 结尾字符串的有效字节数。 */
uint64                              /* 返回不包含末尾 NUL 的字符串长度。 */
strlen(const char *text)            /* 计算以 NUL 结尾字符串的字节数。 */
{                                   /* 开始从下标 0 扫描字符串。 */
  uint64 length = 0;                /* 长度计数器从零开始。 */
  while (text[length] != '\0')      /* 只要当前位置不是字符串终止符就继续。 */
    length++;                       /* 把已发现的普通字符数量加一。 */
  return length;                    /* 返回最终统计出的长度。 */
}                                   /* 结束 strlen。 */

/* 函数作用：有上限地复制字符串，并用 NUL 补足剩余目标空间。 */
char *                                            /* 返回目标缓冲区的原始首地址。 */
strncpy(char *dst, const char *src, int count)    /* 最多复制 count 字节，并用 NUL 补足剩余空间。 */
{                                                 /* 开始执行有限长度字符串复制。 */
  char *start = dst;                              /* 保存目标首地址供函数返回。 */
  while (count > 0 && *src != '\0') {             /* 仍有容量且源串未结束时复制普通字符。 */
    *dst++ = *src++;                              /* 复制一个字符并推进源、目标指针。 */
    count--;                                      /* 消耗一个可用目标字节。 */
  }                                               /* 结束普通字符复制阶段。 */
  while (count-- > 0)                             /* 如果目标长度还有剩余，就逐字节补零。 */
    *dst++ = '\0';                                /* 写入字符串终止字节。 */
  return start;                                   /* 返回目标缓冲区起始地址。 */
}                                                 /* 结束 strncpy。 */
