// Shell.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

// [MODIFIED - Task 3b & 3c]: Mã điều khiển phím tương tác từ console.c
#define KEY_UP    0x10
#define KEY_DOWN  0x11
#define CTRL_U    (('U') - '@')

// [MODIFIED - Task 3c]: Cấu hình lưu trữ lịch sử lệnh (lưu tối đa 16 lệnh)
#define MAX_HIST    16
#define MAX_CMD_LEN 100

static char history[MAX_HIST][MAX_CMD_LEN];
static int hist_count = 0;

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

// [MODIFIED - Task 3b]: Kiểm tra chuỗi str có bắt đầu bằng tiền tố prefix không
static int
prefix_match(const char *prefix, const char *str)
{
  while (*prefix) {
    if (*prefix != *str)
      return 0;
    prefix++;
    str++;
  }
  return 1;
}

// [MODIFIED - Task 3c]: Thêm một lệnh không rỗng vào mảng lịch sử (circular buffer)
static void
add_history(const char *cmd)
{
  if (cmd[0] == '\0')
    return;
  strcpy(history[hist_count % MAX_HIST], cmd);
  hist_count++;
}

// [MODIFIED - Task 3b]: Xử lý tính năng Tab completion
static void
handle_tab(char *buf, int *pos, int nbuf)
{
  buf[*pos] = '\0';

  // Xác định vị trí khoảng trắng cuối cùng để tách từ đang gõ dở
  int last_space = -1;
  for (int i = 0; i < *pos; i++) {
    if (buf[i] == ' ')
      last_space = i;
  }

  // Từ đầu tiên tìm trong "/", các từ tiếp theo tìm trong "."
  char *prefix = (last_space == -1) ? buf : &buf[last_space + 1];
  char *dir_path = (last_space == -1) ? "/" : ".";

  // [MODIFIED - Phương án B]: Chỉ chặn khi ở từ đầu tiên (đầu dòng lệnh) mà chưa gõ ký tự nào
  // Nếu đã qua khoảng trắng (ví dụ "cat "), cho phép Tab để liệt kê toàn bộ file trong thư mục hiện tại "."
  if (last_space == -1 && prefix[0] == '\0')
    return;

  int fd = open(dir_path, O_RDONLY);
  if (fd < 0)
    return;

  struct dirent de;
  char matches[64][DIRSIZ + 1];
  int match_count = 0;

  // Quét danh sách file trong thư mục
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    // Đảm bảo tên file luôn có byte null kết thúc (do DIRSIZ = 14 trong xv6)
    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';

    // Bỏ qua hai thư mục đặc biệt "." và ".."
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    if (prefix_match(prefix, name) && match_count < 64) {
      strcpy(matches[match_count++], name);
    }
  }
  close(fd);

  if (match_count == 1) {
    // Khớp 1 file duy nhất: tự động bù phần đuôi còn thiếu lên màn hình và vào buf
    int p_len = strlen(prefix);
    char *tail = &matches[0][p_len];
    while (*tail && *pos + 1 < nbuf) {
      write(2, tail, 1);
      buf[(*pos)++] = *tail++;
    }
    buf[*pos] = '\0';
  } else if (match_count > 1) {
    // Khớp nhiều file: xuống dòng in toàn bộ tên file, in lại prompt "$ " và nội dung buf hiện tại
    write(2, "\n", 1);
    for (int i = 0; i < match_count; i++) {
      write(2, matches[i], strlen(matches[i]));
      if (i + 1 < match_count)
        write(2, " ", 1);
    }
    write(2, "\n$ ", 3);
    write(2, buf, *pos);
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

int
getcmd(char *buf, int nbuf)
{
  struct stat st;
  int is_console = (fstat(0, &st) == 0 && st.type == T_DEVICE);

  // [MODIFIED - Task 3a]: Chỉ in dấu nhắc lệnh "$ " khi stdin là thiết bị console tương tác
  if (is_console) {
    write(2, "$ ", 2);
  }

  memset(buf, 0, nbuf);

  // [MODIFIED - Task 3a]: Khi đọc lệnh từ file script (sh < script.sh) hoặc pipe,
  // sử dụng gets() thuần túy để giữ nguyên hành vi tiêu chuẩn của xv6
  if (!is_console) {
    gets(buf, nbuf);
    if (buf[0] == 0) // EOF
      return -1;
    return 0;
  }

  // [MODIFIED - Task 3b & 3c]: Chế độ tương tác console: hỗ trợ Tab completion và duyệt lịch sử bằng Mũi tên
  int pos = 0;
  int hist_cur = hist_count;
  char draft[MAX_CMD_LEN];
  memset(draft, 0, sizeof(draft));

  while (pos + 1 < nbuf) {
    char c;
    int cc = read(0, &c, 1);
    if (cc < 1)
      break;

    // Phím Tab: hoàn thiện tên lệnh / tên file
    if (c == '\t') {
      handle_tab(buf, &pos, nbuf);
      continue;
    }

    // Phím Backspace khi đệm kernel đã rỗng: xóa lùi trên buf và màn hình
    if (c == '\b') {
      if (pos > 0) {
        pos--;
        buf[pos] = '\0';
        write(2, "\b \b", 3);
      }
      continue;
    }

    // Phím Ctrl+U: xóa toàn bộ dòng lệnh đang hiển thị
    if (c == CTRL_U) {
      while (pos > 0) {
        pos--;
        buf[pos] = '\0';
        write(2, "\b \b", 3);
      }
      continue;
    }

    // Phím Mũi tên Lên: duyệt về lệnh cũ hơn trong lịch sử
    if (c == KEY_UP) {
      int min_hist = (hist_count > MAX_HIST) ? (hist_count - MAX_HIST) : 0;
      if (hist_cur > min_hist) {
        if (hist_cur == hist_count) {
          // Lưu lại nội dung đang gõ dở vào bản nháp draft
          buf[pos] = '\0';
          strcpy(draft, buf);
        }
        hist_cur--;
        // Xóa dòng hiện tại trên màn hình
        while (pos > 0) {
          pos--;
          write(2, "\b \b", 3);
        }
        // Nạp lệnh từ lịch sử và in ra màn hình
        strcpy(buf, history[hist_cur % MAX_HIST]);
        pos = strlen(buf);
        write(2, buf, pos);
      }
      continue;
    }

    // Phím Mũi tên Xuống: duyệt về lệnh mới hơn trong lịch sử (hoặc khôi phục draft)
    if (c == KEY_DOWN) {
      if (hist_cur < hist_count) {
        hist_cur++;
        // Xóa dòng hiện tại trên màn hình
        while (pos > 0) {
          pos--;
          write(2, "\b \b", 3);
        }
        if (hist_cur == hist_count) {
          // Khôi phục bản nháp đang gõ dở ban đầu
          strcpy(buf, draft);
        } else {
          strcpy(buf, history[hist_cur % MAX_HIST]);
        }
        pos = strlen(buf);
        write(2, buf, pos);
      }
      continue;
    }

    // Ký tự thông thường (đã được kernel consoleintr echo ra màn hình)
    buf[pos++] = c;
    buf[pos] = '\0';
    if (c == '\n' || c == '\r')
      break;
  }

  buf[pos] = '\0';
  if (buf[0] == 0) // EOF (Ctrl+D)
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    // [MODIFIED - Task 3c]: Cắt bỏ ký tự xuống dòng '\n' và '\r' ở cuối
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      buf[--len] = '\0';
    }

    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\0') // Lệnh rỗng (chỉ bấm Enter) -> bỏ qua, không ghi vào history
      continue;

    // [MODIFIED - Task 3c]: Xử lý mở rộng !! (lệnh gần nhất) và !n (lệnh số n)
    if (cmd[0] == '!' && cmd[1] == '!' && cmd[2] == '\0') {
      if (hist_count == 0) {
        fprintf(2, "sh: no history available\n");
        continue;
      }
      strcpy(buf, history[(hist_count - 1) % MAX_HIST]);
      cmd = buf;
      printf("%s\n", cmd); // In lệnh ra trước khi chạy theo đúng yêu cầu đề bài
    } else if (cmd[0] == '!' && cmd[1] >= '0' && cmd[1] <= '9') {
      int n = atoi(cmd + 1);
      int min_idx = (hist_count > MAX_HIST) ? (hist_count - MAX_HIST + 1) : 1;
      int max_idx = hist_count;
      if (hist_count == 0 || n < min_idx || n > max_idx) {
        fprintf(2, "sh: event not found: %d\n", n);
        continue;
      }
      strcpy(buf, history[(n - 1) % MAX_HIST]);
      cmd = buf;
      printf("%s\n", cmd); // In lệnh ra trước khi chạy theo đúng yêu cầu đề bài
    }

    // [MODIFIED - Task 3c]: Lưu câu lệnh hợp lệ vào mảng history
    add_history(cmd);

    // [MODIFIED - Task 3c]: Built-in command: history (in danh sách lệnh đánh số từ 1)
    if (strcmp(cmd, "history") == 0) {
      int start = (hist_count > MAX_HIST) ? (hist_count - MAX_HIST) : 0;
      for (int i = start; i < hist_count; i++) {
        printf("%d %s\n", i + 1, history[i % MAX_HIST]);
      }
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
