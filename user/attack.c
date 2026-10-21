#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define SCANPAGES 128

int
main(int argc, char *argv[])
{
  static char prefix[] = "Here it is: ";
  char *memory = sbrk(SCANPAGES * PGSIZE);

  if (memory == SBRK_ERROR)
    exit(1);

  for (int i = 0; i <= SCANPAGES * PGSIZE - (int)sizeof(prefix); i++) {
    if (memcmp(memory + i, prefix, sizeof(prefix) - 1) == 0) {
      printf("%s\n", memory + i + sizeof(prefix) - 1);
      exit(0);
    }
  }

  exit(1);
}
