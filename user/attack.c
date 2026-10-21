#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define HEAP_PAGES 32
#define SECRET_LEN 8
#define MSG "Here it is: "

static int
is_alnum(char c)
{
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

static int
scan_region(char *base, uint64 len)
{
  uint64 msglen = sizeof(MSG) - 1;
  uint64 i;
  int j;

  for (i = 0; i + msglen + SECRET_LEN + 1 <= len; i++) {
    if (memcmp(base + i, MSG, msglen) != 0)
      continue;

    char *candidate = base + i + msglen;
    int valid = 1;

    for (j = 0; j < SECRET_LEN; j++) {
      if (!is_alnum(candidate[j])) {
        valid = 0;
        break;
      }
    }

    if (valid && candidate[SECRET_LEN] == '\0') {
      char result[SECRET_LEN + 1];

      for (j = 0; j < SECRET_LEN; j++)
        result[j] = candidate[j];

      result[SECRET_LEN] = '\0';
      printf("%s\n", result);
      return 1;
    }
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  char *heap;
  uint64 heap_start;

  (void)argc;
  (void)argv;

  heap = sbrk(HEAP_PAGES * PGSIZE);
  if (heap == (char *)-1)
    exit(1);

  heap_start = (uint64)heap;

  // Quet cac trang chuong trinh da nap, tru trang guard.
  if (heap_start >= 3 * PGSIZE &&
      scan_region((char *)PGSIZE, heap_start - 3 * PGSIZE))
    exit(0);

  // Quet trang stack co the truy cap.
  if (scan_region((char *)(heap_start - PGSIZE), PGSIZE))
    exit(0);

  // Quet vung bo nho moi cap phat.
  if (scan_region(heap, HEAP_PAGES * PGSIZE))
    exit(0);

  exit(1);
}
