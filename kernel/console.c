//
// Console input and output, to the uart.
// Reads are line at a time, except while the shell enables raw input.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
// In raw mode, Tab and the up/down arrow keys are returned as key events.
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

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_TAB   0x82
#define KEY_KILL  0x83

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
  int raw;
  int escape;
} cons;

static void
rawinput(int c)
{
  if (cons.e - cons.r < INPUT_BUF_SIZE) {
    cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
    cons.w = cons.e;
    wakeup(&cons.r);
  }
}

void
console_set_raw(int enabled)
{
  acquire(&cons.lock);
  cons.raw = enabled != 0;
  cons.escape = 0;
  release(&cons.lock);
}

//
// user write() system calls to the console go here.
// uses sleep() and UART interrupts.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32]; // move batches from user space to uart.
  int i = 0;
  int raw;

  acquire(&cons.lock);
  raw = cons.raw;
  release(&cons.lock);

  while (i < n) {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    if (raw) {
      for (int j = 0; j < nn; j++)
        uartputc_sync(buf[j]);
    } else {
      uartwrite(buf, nn);
    }
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

    if (c == '\n') {
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons.buf, and wake up consoleread().
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  if (cons.raw && cons.escape) {
    if (cons.escape == 1 && c == '[') {
      cons.escape = 2;
      release(&cons.lock);
      return;
    }
    if (cons.escape == 2 && (c == 'A' || c == 'B')) {
      rawinput(c == 'A' ? KEY_UP : KEY_DOWN);
      cons.escape = 0;
      release(&cons.lock);
      return;
    }
    cons.escape = 0;
  }
  if (cons.raw && c == '\x1b') {
    cons.escape = 1;
    release(&cons.lock);
    return;
  }

  switch (c) {
  case C('P'): // Print process list.
    procdump();
    break;
  case '\t':
    if (cons.raw) {
      rawinput(KEY_TAB);
      break;
    }
    goto default_input;
  case C('U'): // Kill line.
    if (cons.raw) {
      rawinput(KEY_KILL);
      break;
    }
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (cons.raw) {
      rawinput('\b');
      break;
    }
    if (cons.e != cons.w) {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
  default_input:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;

      if (!cons.raw)
        consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (cons.raw) {
        cons.w = cons.e;
        wakeup(&cons.r);
      } else if (c == '\n' || c == C('D') ||
                 cons.e - cons.r == INPUT_BUF_SIZE) {
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
