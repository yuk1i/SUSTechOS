#include "trap.h"

#include "console.h"
#include "debug.h"
#include "defs.h"
#include "loader.h"
#include "plic.h"
#include "syscall.h"
#include "timer.h"

static int64 kp_print_lock = 0;
extern volatile int panicked;

void plic_handle() {
    int irq = plic_claim();
    if (irq == uart0_irq) {
        uart_intr();
        // printf("intr %d: UART0\n", r_tp());
    }

    if (irq)
        plic_complete(irq);
}

void kernel_trap(struct ktrapframe *ktf) {
    assert(!intr_get());

    if ((r_sstatus() & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");

    mycpu()->inkernel_trap++;

    uint64 cause          = r_scause();
    uint64 exception_code = cause & SCAUSE_EXCEPTION_CODE_MASK;
    if (cause & SCAUSE_INTERRUPT) {
        // correctness checking:
        if (mycpu()->inkernel_trap > 1) {
            // should never have nested interrupt
            print_sysregs(true);
            print_ktrapframe(ktf);
            panic("nested kerneltrap");
        }
        if (panicked) {
            panic("other CPU has panicked");
        }
        // handle interrupt
        switch (exception_code) {
            case SupervisorTimer:
                tracef("s-timer interrupt, cycle: %d", r_time());
                set_next_timer();
                // we never preempt kernel threads.
                break;
            case SupervisorExternal:
                tracef("s-external interrupt.");
                plic_handle();
                break;
            default:
                errorf("unhandled interrupt: %d", cause);
                goto kernel_panic;
        }
    } else {
        // kernel exception, unexpected.
        goto kernel_panic;
    }

    assert(!intr_get());
    assert(mycpu()->inkernel_trap == 1);

    mycpu()->inkernel_trap--;

    return;

kernel_panic:
    // lock against other cpu, to show a complete panic message.
    panicked = 1;

    while (__sync_lock_test_and_set(&kp_print_lock, 1) != 0);

    errorf("=========== Kernel Panic ===========");
    print_sysregs(true);
    print_ktrapframe(ktf);

    __sync_lock_release(&kp_print_lock);

    panic("kernel panic");
}

void set_kerneltrap() {
    assert(IS_ALIGNED((uint64)kernel_trap_entry, 4));
    w_stvec((uint64)kernel_trap_entry);  // DIRECT
}

// set up to take exceptions and traps while in the kernel.
void trap_init() {
    set_kerneltrap();
}

void unknown_trap() {
    print_sysregs(true);
    vm_print(curr_proc()->mm->pgt);
    errorf("unknown trap: %p, stval = %p", r_scause(), r_stval());
    exit(-1);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap() {
    set_kerneltrap();
    assert(!intr_get());

    struct trapframe *trapframe = curr_proc()->trapframe;
    tracef("trap from user epc = %p", trapframe->epc);

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    uint64 cause = r_scause();
    uint64 code  = cause & SCAUSE_EXCEPTION_CODE_MASK;
    if (cause & SCAUSE_INTERRUPT) {
        // check the 63-bit of scause: Interrupt
        switch (code) {
            case SupervisorTimer:
                tracef("time interrupt!");
                set_next_timer();
                yield();
                break;
            case SupervisorExternal:
                tracef("s-external interrupt from usertrap!");
                plic_handle();
                break;
            default:
                unknown_trap();
                break;
        }
    } else {
        switch (code) {
            case UserEnvCall:
                trapframe->epc += 4;
                intr_on();
                syscall();
                intr_off();
                break;
            case LoadPageFault:
            case StorePageFault:
            case InstructionPageFault: {
                uint64 addr    = r_stval();
                struct proc *p = curr_proc();
                acquire(&p->lock);
                struct mm *mm = p->mm;
                acquire(&mm->lock);
                release(&p->lock);
                pte_t *pte = walk(mm, addr, 0);
                release(&mm->lock);
                if (pte != NULL && (*pte & PTE_V)) {
                    *pte |= PTE_A;
                    if (cause == StorePageFault)
                        *pte |= PTE_D;
                    sfence_vma();
                    break;
                } else {
                infof("page fault in application, bad addr = %p, bad instruction = %p, core dumped.", r_stval(), trapframe->epc);
                    exit(-2);
                }
            }
            case StoreMisaligned:
            case InstructionMisaligned:
            case LoadMisaligned:
                errorf(
                    "%d in application, bad addr = %p, bad instruction = %p, "
                    "core dumped.",
                    cause,
                    r_stval(),
                    trapframe->epc);
                vm_print(curr_proc()->mm->pgt);
                exit(-2);
                break;
            case IllegalInstruction:
                errorf("IllegalInstruction in application, core dumped.");
                exit(-3);
                break;
            default:
                unknown_trap();
                break;
        }
    }
    assert(!intr_get());
    usertrapret();
}

//
// return to user space
//
void usertrapret() {
    if (intr_get())
        panic("usertrapret entered with intr on");

    struct trapframe *trapframe = curr_proc()->trapframe;
    trapframe->kernel_satp      = r_satp();                                 // kernel page table
    trapframe->kernel_sp        = curr_proc()->kstack + KERNEL_STACK_SIZE;  // process's kernel stack
    trapframe->kernel_trap      = (uint64)usertrap;
    trapframe->kernel_hartid    = r_tp();  // unuesd

    w_sepc(trapframe->epc);
    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE;  // enable interrupts in user mode
    w_sstatus(x);

    // tell trampoline.S the user page table to switch to.
    uint64 satp  = MAKE_SATP(KVA_TO_PA(curr_proc()->mm->pgt));
    uint64 stvec = (TRAMPOLINE + (uservec - trampoline)) & ~0x3;

    uint64 fn = TRAMPOLINE + (userret - trampoline);
    tracef("return to user @%p, fn %p", trapframe->epc);
    ((void (*)(uint64, uint64, uint64))fn)(TRAPFRAME, satp, stvec);
}