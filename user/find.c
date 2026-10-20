#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

char*
fmtname(char *path)
{
  char *p;
  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}

// Thực thi câu lệnh cho tham số -exec
void
run_exec(char **cmd_args, int cmd_count, char *path)
{
  char *exec_argv[MAXARG];

  if (cmd_count + 1 >= MAXARG) {
    fprintf(2, "find: exec arguments too long\n");
    return;
  }

  // Copy các tham số lệnh
  for (int i = 0; i < cmd_count; i++) {
    exec_argv[i] = cmd_args[i];
  }
  // Đưa đường dẫn file tìm được vào làm tham số cuối
  exec_argv[cmd_count] = path;
  exec_argv[cmd_count + 1] = 0;

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "find: fork failed\n");
    return;
  }

  if (pid == 0) {
    exec(exec_argv[0], exec_argv);
    fprintf(2, "find: exec %s failed\n", exec_argv[0]);
    exit(1);
  } else {
    wait(0);
  }
}

void
find(char *path, char *target, char **cmd_args, int cmd_count)
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

  if (strcmp(fmtname(path), target) == 0) {
    if (cmd_count > 0) {
      // Nếu có tùy chọn -exec thì thực thi lệnh thay vì in tên
      run_exec(cmd_args, cmd_count, path);
    } else {
      printf("%s\n", path);
    }
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

      char name[DIRSIZ + 1];
      int i;
      for (i = 0; i < DIRSIZ && de.name[i] != 0; i++) {
        name[i] = de.name[i];
      }
      name[i] = 0;

      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

      memmove(p, name, strlen(name) + 1);
      find(buf, target, cmd_args, cmd_count);
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(2, "Usage: find <path> <target> [-exec cmd ...]\n");
    exit(1);
  }

  char *path = argv[1];
  char *target = argv[2];
  char **cmd_args = 0;
  int cmd_count = 0;

  // Tách lấy lệnh đằng sau cờ -exec (nếu có)
  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "-exec") == 0) {
      if (i + 1 < argc) {
        cmd_args = &argv[i + 1];
        cmd_count = argc - (i + 1);
      } else {
        fprintf(2, "find: -exec requires a command\n");
        exit(1);
      }
      break;
    }
  }

  find(path, target, cmd_args, cmd_count);
  exit(0);
}
