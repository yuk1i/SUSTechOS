#include "console.h"

#include "defs.h"
#include "sbi.h"

static int uart_inited = false;
static void uart_putchar(int);

static struct spinlock uart_tx_lock;
volatile int panicked = 0;

#define BACKSPACE 0x100
#define C(x)      ((x) - '@')  // Control-x

struct {
    spinlock_t lock;

    // input
#define INPUT_BUF_SIZE 128
    char buf[INPUT_BUF_SIZE];
    uint r;  // Read index
    uint w;  // Write index
    uint e;  // Edit index
} cons;

void consputc(int c) {
    if (!uart_inited || panicked)  // when panicked, use SBI output
        sbi_putchar(c);
    else {
        if (c == BACKSPACE) {
            uart_putchar('\b');
            uart_putchar(' ');
            uart_putchar('\b');
        } else {
            uart_putchar(c);
        }
    }
}

static void uart_putchar(int ch) {
    int intr = intr_off();
    while ((ReadReg(LSR) & LSR_TX_IDLE) == 0) MEMORY_FENCE();
    MEMORY_FENCE();

    WriteReg(THR, ch);
    MEMORY_FENCE();
    if (intr)
        intr_on();
}

void console_init() {
    assert(!uart_inited);
    spinlock_init(&uart_tx_lock, "uart_tx");
    spinlock_init(&cons.lock, "cons");

    // disable interrupts.
    WriteReg(IER, 0x00);
    MEMORY_FENCE();

    // special mode to set baud rate.
    WriteReg(LCR, LCR_BAUD_LATCH);
    MEMORY_FENCE();

    // LSB for baud rate of 38.4K.
    WriteReg(0, 0x03);
    MEMORY_FENCE();

    // MSB for baud rate of 38.4K.
    WriteReg(1, 0x00);
    MEMORY_FENCE();
    // leave set-baud mode,

    // and set word length to 8 bits, no parity.
    WriteReg(LCR, LCR_EIGHT_BITS);
    MEMORY_FENCE();

    // reset and enable FIFOs.
    WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
    MEMORY_FENCE();

    // enable receive interrupts.
    WriteReg(IER, IER_RX_ENABLE);
    MEMORY_FENCE();
    uart_inited = true;
}

static void consintr(int c) {
    acquire(&cons.lock);

    switch (c) {
        case C('P'):  // Print process list.
            break;
        case C('U'):  // Kill line.
            while (cons.e != cons.w && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
                cons.e--;
                consputc(BACKSPACE);
            }
            break;
        case C('H'):  // Backspace
        case '\x7f':  // Delete key
            if (cons.e != cons.w) {
                cons.e--;
                consputc(BACKSPACE);
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
                    wakeup(&cons);
                }
            }
            break;
    }

    release(&cons.lock);
}

static int uartgetc(void) {
    if (ReadReg(LSR) & 0x01) {
        // input data is ready.
        return ReadReg(RHR);
    } else {
        return -1;
    }
}

void uart_intr() {
    while (1) {
        int c = uartgetc();
        if (c == -1)
            break;
        // infof("uart: %c", c);
        consintr(c);
    }
}

int64 user_console_write(uint64 __user buf, int64 len) {
    if (len <= 0)
        return -1;

    struct proc *p = curr_proc();
    struct mm *mm;

    acquire(&p->lock);
    mm = p->mm;
    acquire(&mm->lock);
    release(&p->lock);

    char kbuf[len];
    copy_from_user(mm, kbuf, buf, len);
    release(&mm->lock);

    for (int64 i = 0; i < len; i++) {
        uart_putchar(kbuf[i]);
    }
    return len;
}

int64 user_console_read(uint64 __user buf, int64 n) {
    uint target;
    int c;
    char cbuf;

    target = n;
    acquire(&cons.lock);
    while (n > 0) {
        // wait until interrupt handler has put some
        // input into cons.buffer.
        while (cons.r == cons.w) {
            // if (curr_proc()->state != SLEEPING) {
            // 	release(&cons.lock);
            // 	return -1;
            // }
            sleep(&cons, &cons.lock);
        }

        c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

        if (c == C('D')) {  // end-of-file
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                cons.r--;
            }
            break;
        }

        // copy the input byte to the user-space buffer.
        cbuf = c;

        struct proc *p = curr_proc();
        acquire(&p->lock);
        struct mm *mm = p->mm;
        acquire(&mm->lock);
        release(&p->lock);

        if (copy_to_user(mm, (uint64)buf, &cbuf, 1) == -1) {
            release(&mm->lock);
            break;
        }
        release(&mm->lock);

        buf++;
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