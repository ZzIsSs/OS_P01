#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

// Lấy tên file/thư mục từ đường dẫn (ví dụ: "./a/b" -> "b")
char*
fmtname(char *path)
{
  char *p;
  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}

// Xử lý khi tìm thấy file/thư mục khớp tên
void
match(char *path, int has_exec, char **exec_argv, int exec_argc)
{
  if (!has_exec) {
    printf("%s\n", path);
    return;
  }

  // Nếu có -exec: chạy cmd [args...] path
  if (exec_argc + 2 > MAXARG) {
    fprintf(2, "find: too many arguments for -exec\n");
    exit(1);
  }

  char *cmd_argv[MAXARG];
  int i;
  for (i = 0; i < exec_argc; i++) {
    cmd_argv[i] = exec_argv[i];
  }
  cmd_argv[i++] = path;
  cmd_argv[i] = 0;

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "find: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    exec(cmd_argv[0], cmd_argv);
    // Nếu exec thành công thì sẽ không bao giờ chạy tới đây
    fprintf(2, "find: exec %s failed\n", cmd_argv[0]);
    exit(1);
  }
  // Tiến trình cha chờ tiến trình con thực thi xong
  wait(0);
}

// Duyệt thư mục đệ quy
void
find(char *path, char *target, int has_exec, char **exec_argv, int exec_argc)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // So sánh tên file/thư mục hiện tại với target
  if (strcmp(fmtname(path), target) == 0) {
    match(path, has_exec, exec_argv, exec_argc);
  }

  switch (st.type) {
  case T_DEVICE:
  case T_FILE:
    break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
      fprintf(2, "find: path too long\n");
      break;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0)
        continue;

      // Đọc tên entry và đảm bảo kết thúc bằng '\0'
      char name[DIRSIZ + 1];
      int i;
      for (i = 0; i < DIRSIZ && de.name[i] != 0; i++) {
        name[i] = de.name[i];
      }
      name[i] = 0;

      // Bỏ qua "." và ".." để tránh lặp vô tận
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

      // Ghép đường dẫn con và đệ quy
      memmove(p, name, strlen(name) + 1);
      find(buf, target, has_exec, exec_argv, exec_argc);
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(2, "Usage: find <path> <target> [-exec cmd [args...]]\n");
    exit(1);
  }

  int has_exec = 0;
  char **exec_argv = 0;
  int exec_argc = 0;

  if (argc > 3) {
    if (strcmp(argv[3], "-exec") != 0 || argc < 5) {
      fprintf(2, "Usage: find <path> <target> [-exec cmd [args...]]\n");
      exit(1);
    }
    has_exec = 1;
    exec_argv = &argv[4];
    exec_argc = argc - 4;
  }

  find(argv[1], argv[2], has_exec, exec_argv, exec_argc);
  exit(0);
}