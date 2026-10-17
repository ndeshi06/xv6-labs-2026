// Shell.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10
#define HISTORY_SIZE 10
#define MAX_COMPLETIONS 200

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_TAB   0x82
#define KEY_KILL  0x83
#define C(x)      ((x) - '@')

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

static char history[HISTORY_SIZE][100];
static int history_count;
static char completions[MAX_COMPLETIONS][DIRSIZ + 1];

static void
history_add(const char *line)
{
  int slot = history_count % HISTORY_SIZE;
  int i;

  for (i = 0; i < sizeof(history[slot]) - 1 && line[i]; i++)
    history[slot][i] = line[i];
  history[slot][i] = 0;
  history_count++;
}

static const char *
history_get(int number)
{
  if (number < 1 || number > history_count ||
      history_count - number >= HISTORY_SIZE)
    return 0;
  return history[(number - 1) % HISTORY_SIZE];
}

static int
is_word_delimiter(char c)
{
  return c == ' ' || c == '\t' || c == '|' || c == ';' || c == '&' ||
         c == '(' || c == ')' || c == '<' || c == '>';
}

static int
starts_with(const char *name, const char *prefix, int length)
{
  int i;

  for (i = 0; i < length; i++)
    if (name[i] != prefix[i])
      return 0;
  return 1;
}

static int
is_executable(const char *name)
{
  char path[DIRSIZ + 2];
  char magic[4];
  int fd, ok;

  path[0] = '/';
  strcpy(path + 1, name);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return 0;
  ok = read(fd, magic, sizeof(magic)) == sizeof(magic) &&
       magic[0] == 0x7f && magic[1] == 'E' &&
       magic[2] == 'L' && magic[3] == 'F';
  close(fd);
  return ok;
}

static int
collect_completions(const char *prefix, int prefix_len, int command)
{
  struct dirent de;
  char name[DIRSIZ + 1];
  int fd, count = 0, i;

  fd = open(command ? "/" : ".", O_RDONLY);
  if (fd < 0)
    return 0;
  while (read(fd, &de, sizeof(de)) == sizeof(de) &&
         count < MAX_COMPLETIONS) {
    if (de.inum == 0)
      continue;
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;
    for (i = 0; i < DIRSIZ && name[i]; i++)
      ;
    if (i == 0 || !starts_with(name, prefix, prefix_len))
      continue;
    if (command && !is_executable(name))
      continue;
    if (!command && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0))
      continue;
    strcpy(completions[count++], name);
  }
  close(fd);
  return count;
}

static void
complete_word(char *line, int *length, int nbuf, int interactive)
{
  int start = *length;
  int command = 1;
  int i, prefix_len, count, suffix_len;
  int has_slash = 0;
  char *suffix;

  while (start > 0 && !is_word_delimiter(line[start - 1]))
    start--;
  for (i = 0; i < start; i++) {
    if (line[i] != ' ' && line[i] != '\t') {
      command = 0;
      break;
    }
  }
  prefix_len = *length - start;
  for (i = 0; i < prefix_len; i++)
    if (line[start + i] == '/')
      has_slash = 1;
  if (has_slash)
    return;
  count = collect_completions(line + start, prefix_len, command);
  if (count == 0)
    return;

  if (count == 1) {
    suffix = completions[0] + prefix_len;
    suffix_len = strlen(suffix);
    if (*length + suffix_len >= nbuf)
      suffix_len = nbuf - *length - 1;
    if (suffix_len > 0) {
      memmove(line + *length, suffix, suffix_len);
      *length += suffix_len;
      line[*length] = 0;
      write(1, suffix, suffix_len);
    }
    return;
  }

  if (!interactive)
    return;
  write(1, "\n", 1);
  for (i = 0; i < count; i++) {
    if (i)
      write(1, " ", 1);
    write(1, completions[i], strlen(completions[i]));
  }
  write(1, "\n$ ", 3);
  write(1, line, *length);
}

static void
clear_line(int length, int interactive)
{
  int i;

  if (interactive)
    for (i = 0; i < length; i++)
      write(1, "\b \b", 3);
}

static int
getcmd(char *buf, int nbuf, int interactive)
{
  int length = 0;
  int history_pos = history_count + 1;

  memset(buf, 0, nbuf);
  if (!interactive) {
    gets(buf, nbuf);
    return buf[0] == 0 ? -1 : 0;
  }

  if (consolemode(1) < 0)
    return -1;
  write(2, "$ ", 2);
  for (;;) {
    char input;
    int n = read(0, &input, 1);
    int c;

    if (n < 1) {
      consolemode(0);
      if (length == 0)
        return -1;
      buf[length++] = '\n';
      buf[length] = 0;
      return 0;
    }
    c = (uchar)input;
    if (c == KEY_TAB) {
      complete_word(buf, &length, nbuf, interactive);
    } else if (c == KEY_UP || c == KEY_DOWN) {
      int oldest = history_count - HISTORY_SIZE + 1;
      int old_length = length;
      const char *entry;

      if (oldest < 1)
        oldest = 1;
      if (c == KEY_UP && history_pos > oldest)
        history_pos--;
      else if (c == KEY_DOWN && history_pos <= history_count)
        history_pos++;
      entry = history_get(history_pos);
      if (entry) {
        length = strlen(entry);
        if (length >= nbuf)
          length = nbuf - 1;
        memmove(buf, entry, length);
        buf[length] = 0;
      } else {
        length = 0;
        buf[0] = 0;
      }
      clear_line(old_length, interactive);
      write(1, buf, length);
    } else if (c == KEY_KILL) {
      while (length > 0) {
        length--;
        write(1, "\b \b", 3);
      }
      buf[0] = 0;
      history_pos = history_count + 1;
    } else if (c == '\b') {
      if (length > 0) {
        length--;
        buf[length] = 0;
        write(1, "\b \b", 3);
      }
      history_pos = history_count + 1;
    } else if (c == '\n') {
      if (length < nbuf - 1)
        buf[length++] = '\n';
      buf[length] = 0;
      write(1, "\n", 1);
      consolemode(0);
      return 0;
    } else if (c == C('D')) {
      consolemode(0);
      if (length == 0)
        return -1;
      buf[length++] = '\n';
      buf[length] = 0;
      return 0;
    } else if (c >= 32 && c < 127) {
      if (length < nbuf - 1) {
        buf[length++] = c;
        buf[length] = 0;
        write(1, &input, 1);
      } else {
        write(1, "\a", 1);
      }
      history_pos = history_count + 1;
    }
  }
}

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

static int
stdin_is_console(void)
{
  struct stat input, console;
  int fd, result = 0;

  if (fstat(0, &input) < 0)
    return 0;
  fd = open("console", O_RDWR);
  if (fd < 0)
    return 0;
  if (fstat(fd, &console) == 0 && input.type == T_DEVICE &&
      input.dev == console.dev && input.ino == console.ino)
    result = 1;
  close(fd);
  return result;
}

static int
history_reference(const char *line, int *number)
{
  int i = 0, n = 0, digits = 0;

  while (line[i] == ' ' || line[i] == '\t')
    i++;
  if (line[i] != '!')
    return 0;
  i++;
  if (line[i] == '!') {
    *number = history_count;
    i++;
  } else {
    while (line[i] >= '0' && line[i] <= '9') {
      digits = 1;
      if (n <= history_count)
        n = n * 10 + line[i] - '0';
      i++;
    }
    if (!digits)
      return 0;
    *number = n;
  }
  while (line[i] == ' ' || line[i] == '\t')
    i++;
  return line[i] == 0 ? 1 : 0;
}

static int
is_history_command(const char *line)
{
  int i = 0;

  while (line[i] == ' ' || line[i] == '\t')
    i++;
  if (line[i] != 'h' || line[i + 1] != 'i' || line[i + 2] != 's' ||
      line[i + 3] != 't' || line[i + 4] != 'o' || line[i + 5] != 'r' ||
      line[i + 6] != 'y')
    return 0;
  i += 7;
  while (line[i] == ' ' || line[i] == '\t')
    i++;
  return line[i] == 0;
}

int
main(void)
{
  static char buf[100];
  static char expanded[100];
  int fd;
  int interactive;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }
  interactive = stdin_is_console();

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf), interactive) >= 0) {
    int number;
    char *cmd = buf;

    if (strlen(cmd) > 0 && cmd[strlen(cmd) - 1] == '\n')
      cmd[strlen(cmd) - 1] = 0;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == 0)
      continue;
    if (history_reference(cmd, &number)) {
      const char *entry = history_get(number);
      if (entry == 0) {
        fprintf(2, "history: event not found\n");
        continue;
      }
      strcpy(expanded, entry);
      printf("%s\n", expanded);
      cmd = expanded;
    } else if (*cmd == '!') {
      fprintf(2, "history: event not found\n");
      continue;
    }
    history_add(cmd);
    if (is_history_command(cmd)) {
      int first = history_count - HISTORY_SIZE + 1;
      if (first < 1)
        first = 1;
      for (number = first; number <= history_count; number++)
        printf("%d  %s\n", number, history[(number - 1) % HISTORY_SIZE]);
      continue;
    }
    if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
      // Chdir must be called by the parent, not the child.
      if (chdir(cmd + 3) < 0)
        fprintf(2, "cannot cd %s\n", cmd + 3);
    } else {
      if (fork1() == 0)
        runcmd(parsecmd(cmd));
      wait(0);
    }
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
