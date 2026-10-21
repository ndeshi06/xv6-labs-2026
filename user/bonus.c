#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int result;

  (void)argc;
  (void)argv;

  printf("bonus: calling kill(1)\n");
  result = kill(1);
  printf("bonus: kill(1) returned %d\n", result);

  exit(0);
}
