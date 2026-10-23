#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

void
find(char *path, char *name, char **cmd)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, O_RDONLY)) < 0)
    return;

  if (fstat(fd, &st) < 0) {
    close(fd);
    return;
  }

  if (st.type != T_DIR) {
    close(fd);
    return;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
      continue;

    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = '\0';

    if (stat(buf, &st) < 0)
      continue;

    if (strcmp(de.name, name) == 0) {
      if (cmd == 0) {
        printf("%s\n", buf);
      } else {
        char *args[MAXARG];
        int i = 0;

        while (cmd[i] && i < MAXARG - 2) {
          args[i] = cmd[i];
          i++;
        }

        args[i++] = buf;
        args[i] = 0;

        int pid = fork();
        if (pid == 0) {
          exec(args[0], args);
          fprintf(2, "find: exec failed\n");
          exit(1);
        }

        if (pid > 0)
          wait(0);
      }
    }

    if (st.type == T_DIR)
      find(buf, name, cmd);
  }

  close(fd);
}

void
find_exec(char *path, char *name, char **cmd)
{
  find(path, name, cmd);
}

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(2, "Usage: find path filename [-exec cmd ...]\n");
    exit(1);
  }

  if (argc == 3) {
    find(argv[1], argv[2], 0);
  } else if (argc >= 5 && strcmp(argv[3], "-exec") == 0) {
    find_exec(argv[1], argv[2], &argv[4]);
  } else {
    fprintf(2, "Usage: find path filename [-exec cmd ...]\n");
    exit(1);
  }

  exit(0);
}
