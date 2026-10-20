#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

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

// Duyệt thư mục đệ quy
void
find(char *path, char *target)
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

  // So sánh tên file/thư mục hiện tại với target, khớp thì in đường dẫn
  if (strcmp(fmtname(path), target) == 0) {
    printf("%s\n", path);
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
      find(buf, target);
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc != 3) {
    fprintf(2, "Usage: find <path> <target>\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}
