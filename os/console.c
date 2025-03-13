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
    set_reg(IER, IER_RX_ENABLE);
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

