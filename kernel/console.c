//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100       // erase the last output character
#define C(x)      ((x) - '@') // Control-x

// [MODIFIED - Task 3b & 3c]: Tín hiệu phím đặc biệt gửi lên user-space shell
#define KEY_UP    0x10
#define KEY_DOWN  0x11

// [MODIFIED - Task 3c]: Trạng thái bộ máy FSM bắt chuỗi ANSI escape cho phím mũi tên (\033[A, \033[B)
static int esc_state = 0; // 0: Normal, 1: ESC (0x1b), 2: '['

//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printk and to echo input
// characters.
//
void
consputc(int c)
{
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;

  // input circular buffer
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index
} cons;

//
// user write() system calls to the console go here.
// uses sleep() and UART interrupts.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32]; // move batches from user space to uart.
  int i = 0;

  while (i < n) {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dst indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while (n > 0) {
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while (cons.r == cons.w) {
      if (killed(myproc())) {
        release(&cons.lock);
        return -1;
      }
      sleep_prepare(&cons.r);
      release(&cons.lock);
      sleep();
      acquire(&cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if (c == C('D')) { // end-of-file
      if (n < target) {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    // [MODIFIED - Task 3b & 3c]: Đánh thức consoleread() sớm khi gặp các phím điều khiển tương tác
    // (\n, \t, phím mũi tên, xóa lùi \b khi đệm rỗng, Ctrl+U)
    if (c == '\n' || c == '\t' || c == KEY_UP || c == KEY_DOWN ||
        c == '\b' || c == C('U')) {
      // Return immediately on newline or interactive control keys
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  // [MODIFIED - Task 3c]: State machine bắt chuỗi ANSI escape phím Mũi tên (\033[A, \033[B)
  // Đảm bảo không echo ra màn hình và giải phóng cons.lock đúng cách để chống deadlock.
  if (esc_state == 1) {
    if (c == '[') {
      esc_state = 2;
      release(&cons.lock);
      return;
    }
    esc_state = 0; // Không phải '[', reset về trạng thái bình thường và rơi xuống xử lý c
  } else if (esc_state == 2) {
    esc_state = 0;
    if (c == 'A') { // Up arrow -> gửi tín hiệu KEY_UP
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = KEY_UP;
        cons.w = cons.e;
        wakeup(&cons.r);
      }
      release(&cons.lock);
      return;
    } else if (c == 'B') { // Down arrow -> gửi tín hiệu KEY_DOWN
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = KEY_DOWN;
        cons.w = cons.e;
        wakeup(&cons.r);
      }
      release(&cons.lock);
      return;
    } else {
      // Bỏ qua các chuỗi CSI khác (Mũi tên Trái 'D', Phải 'C', ...) để không in rác ra màn hình
      release(&cons.lock);
      return;
    }
  }

  if (c == '\x1b') { // Bắt đầu chuỗi ESC (27)
    esc_state = 1;
    release(&cons.lock);
    return;
  }

  switch (c) {
  case C('P'): // Print process list.
    procdump();
    break;
  case C('U'): // Kill line.
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    // [MODIFIED - Task 3c]: Đẩy C('U') lên cho shell xóa phần text do Tab/History chèn vào trước đó
    if (cons.e - cons.r < INPUT_BUF_SIZE) {
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = C('U');
      cons.w = cons.e;
      wakeup(&cons.r);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (cons.e != cons.w) {
      cons.e--;
      consputc(BACKSPACE);
    } else if (cons.e - cons.r < INPUT_BUF_SIZE) {
      // [MODIFIED - Task 3b & 3c]: Khi bộ đệm kernel rỗng (cons.e == cons.w),
      // gửi '\b' lên shell để shell xóa lùi trên buf đã hoàn thành trước đó.
      // Tuyệt đối không gọi consputc ở đây vì shell sẽ tự xử lý hiển thị.
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\b';
      cons.w = cons.e;
      wakeup(&cons.r);
    }
    break;
  case '\t': // [MODIFIED - Task 3b]: Kích hoạt Tab completion
    // Không echo ký tự '\t', đẩy trực tiếp vào cons.buf và đánh thức consoleread()
    if (cons.e - cons.r < INPUT_BUF_SIZE) {
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\t';
      cons.w = cons.e;
      wakeup(&cons.r);
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }

  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
