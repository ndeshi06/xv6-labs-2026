#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define HEAP_PAGES 32
#define MSG        "Here it is: "

static int
is_alnum(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

static int
scan_region(char *base, uint64 len)
{
  uint64 msglen = sizeof(MSG) - 1;
  uint64 i;

  for (i = 0; i + msglen < len; i++) {
    uint64 start, end;

    if (memcmp(base + i, MSG, msglen) != 0)
      continue;

    start = i + msglen;
    end = start;

    // Doc cac ky tu chu hoac so sau chuoi danh dau.
    while (end < len && is_alnum(base[end]))
      end++;

    // Secret phai co it nhat mot ky tu va ket thuc bang '\0'.
    if (end > start && end < len && base[end] == '\0') {
      printf("%s\n", base + start);
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

  // Uu tien quet vung heap vua cap phat.
  if (scan_region(heap, HEAP_PAGES * PGSIZE))
    exit(0);

  // Quet trang stack co the truy cap.
  if (heap_start >= PGSIZE &&
      scan_region((char *)(heap_start - PGSIZE), PGSIZE))
    exit(0);

  // Quet vung chuong trinh da nap.
  if (heap_start >= 3 * PGSIZE &&
      scan_region((char *)PGSIZE, heap_start - 3 * PGSIZE))
    exit(0);

  exit(1);
}
