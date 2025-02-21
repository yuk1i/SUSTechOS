#include "console.h"

#include "debug.h"
#include "defs.h"
#include "riscv-io.h"
#include "sbi.h"

int uart0_irq;
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
        } else if (c == '\n') {
            uart_putchar('\r');
            uart_putchar('\n');
        } else {
            uart_putchar(c);
        }
    }
}

// riscv-io.h

extern int on_vf2_board;
static void set_reg(uint32 reg, uint32 val) {
    if (on_vf2_board) {
        reg = reg << 2;
        writel(val, Reg(reg));
    } else {
        writeb(val, Reg(reg));
    }
}

static uint32 read_reg(uint32 reg) {
    if (on_vf2_board) {
        reg = reg << 2;
        return readl(Reg(reg));
    } else {
        return readb(Reg(reg));
    }
}

static void uart_putchar(int ch) {
    int intr = intr_off();
    while ((read_reg(LSR) & LSR_TX_IDLE) == 0);

    set_reg(THR, ch);
    if (intr)
        intr_on();
}

static int uartgetc(void) {
    if (read_reg(LSR) & 0x01) {
        // input data is ready.
        return read_reg(RHR);
    } else {
        return -1;
    }
}

void console_init() {
    assert(!uart_inited);
    spinlock_init(&uart_tx_lock, "uart_tx");
    spinlock_init(&cons.lock, "cons");

    // no need to init uart8250, they are already inited by OpenSBI.

    // however, setup interrupt number for UART0.
    if (on_vf2_board) {
        uart0_irq = VF2_UART0_IRQ;
    } else {
        uart0_irq = QEMU_UART0_IRQ;
    }

    // disable interrupts.
    set_reg(IER, 0x00);

    // reset and enable FIFOs.
    set_reg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

    // enable receive interrupts.
    set_reg(IER, IER_RX_ENABLE | IER_TX_ENABLE);
    uart_inited = true;
}

static void consintr(int c) {
    acquire(&cons.lock);

    switch (c) {
        case C('P'):
            print_procs();
            break;
        case C('Q'):
            print_kpgmgr();
            break;
        case C('U'):  // Kill line.
            while (cons.e != cons.w && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
                cons.e--;
                consputc(BACKSPACE);
            }
            break;
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

    // we only sync on user's write.
    acquire(&uart_tx_lock);

    extern uint64 kernelprint_lock;
    while (__sync_lock_test_and_set(&kernelprint_lock, 1) != 0);
    __sync_synchronize();

    for (int64 i = 0; i < len; i++) {
        consputc(kbuf[i]);
    }

    __sync_synchronize();
    __sync_lock_release(&kernelprint_lock);

    release(&uart_tx_lock);

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