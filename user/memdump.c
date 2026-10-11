#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include <stdint.h>

void memdump(char *fmt, char *data, int len);

int main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void memdump(char *fmt, char *data, int len)
{
  int cntByte = len;
  for (int i = 0; fmt[i] != '\0'; ++i)
  {
    switch (fmt[i])
    {
      case 'i':
      {
        if (cntByte < 4) printf("memdump: not enough data for 'i'\n"), exit(1);
        int32_t val;
        memcpy(&val, data, 4);
        printf("%d\n", val);
        data += 4;
        cntByte -= 4;
        break;
      }
      case 'p':
      {
        if (cntByte < 8) printf("memdump: not enough data for 'p'\n"), exit(1);
        int64_t val;
        memcpy(&val, data, 8);
        printf("%lx\n", val);
        data += 8;
        cntByte -= 8;
        break;
      }
      case 'h':
      {
        if (cntByte < 2) printf("memdump: not enough data for 'h'\n"), exit(1);
        int16_t val;
        memcpy(&val, data, 2);
        printf("%d\n", val);
        data += 2;
        cntByte -= 2;
        break;
      }
      case 'c':
      {
        if (cntByte < 1) printf("memdump: not enough data for 'c'\n"), exit(1);
        char val;
        memcpy(&val, data, 1);
        printf("%c\n", val);
        data += 1;
        cntByte -= 1;
        break;
      }
      case 's':
      {
        if (cntByte < 8)
          printf("memdump: not enough data for 's'\n"), exit(1);
        char *str;
        memcpy(&str, data, 8);
        printf("%s\n", str);
        data += 8;
        cntByte -= 8;
        break;
      }
      case 'S':
      {
        if (cntByte == 0)
          printf("memdump: not enough data for 'S'\n"), exit(1);
        printf("%s\n", data);
        data += cntByte;
        cntByte = 0;
        break;
      }
    }
  }
}

