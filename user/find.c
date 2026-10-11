#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

void find(char *path, char **argv)
{
    char *name = argv[2];
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

        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = '\0';

        if (strcmp(p, "./.") == 0 || strcmp(p, "./..") == 0 ||
            strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
            continue;

        if (stat(buf, &st) < 0)
            continue;

        char filename[DIRSIZ + 1];
        memmove(filename, de.name, DIRSIZ);
        filename[DIRSIZ] = '\0';

        if (strcmp(filename, name) == 0) {
            if (argv[3] && strcmp(argv[3], "-exec") == 0) {
                char *args[MAXARG];
                int i = 4, j = 0;

                while (argv[i] && j < MAXARG - 2)
                    args[j++] = argv[i++];

                args[j++] = buf;
                args[j] = 0;

                int pid = fork();
                if (pid == 0) {
                    exec(args[0], args);
                    fprintf(2, "find: exec failed\n");
                    exit(1);
                } else if (pid > 0) {
                    wait(0);
                }
            } else {
                printf("%s\n", buf);
            }
        }

        if (st.type == T_DIR)
            find(buf, argv);
    }

    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(2, "Usage: find path filename [-exec cmd ...]\n");
        exit(1);
    }

    if (argc > 3 && strcmp(argv[3], "-exec") != 0) {
        fprintf(2, "Usage: find path filename [-exec cmd ...]\n");
        exit(1);
    }

    if (argc > 3 && strcmp(argv[3], "-exec") == 0 && argc < 5) {
        fprintf(2, "find: missing command after -exec\n");
        exit(1);
    }

    find(argv[1], argv);
    exit(0);
}

