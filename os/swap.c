#include "debug.h"
#include "defs.h"

#define SWAPAREA_SIZE (8000 * PGSIZE)

void* swaparea_base = (void*)0xffffffe000000000ull;
int used[SWAPAREA_SIZE / PGSIZE];

#define SWAPADDR_TO_INDEX(addr) (((addr) - swaparea_base) / PGSIZE)
#define SWAPINDEX_TO_ADDR(idx)  (swaparea_base + (idx) * PGSIZE)

/**
 * Swap: We create a swap-area just inside the DRAM, instead of disk.
 *  because this is a simulator, thus we don't use a disk to simplify the implementation and focus on the core concepts.
 *
 * The swap area is a region of memory, both contiguous in physical memory and virtual memory.
 *
 * For the Virtual Memory, we put it at         0xffff_ffe0_0000_0000.
 *  Notice that the Kernel Direct Mapping is at 0xffff_ffc0_0000_0000. (see memlayout.h)
 * For the Physical Memory, we put it after all physical memory that we will used.
 *  See memlayout.h, the physical area begins at RISCV_DDR_BASE (0x8000_0000),
 *    the kernel image starts at 0x8020_0000,
 *    and we will only use up to RISCV_DDR_BASE + PHYS_MEM_SIZE (0x8000_0000 + 0x800_0000).
 */

void swap_init() {
    memset(used, 0, sizeof(used));

    // map the swap area in the kernel page table.
    kvmmap(kernel_pagetable, (uint64)swaparea_base, RISCV_DDR_BASE + PHYS_MEM_SIZE, SWAPAREA_SIZE, PTE_R | PTE_W | PTE_A | PTE_D);
    sfence_vma();
}

int swap_in(struct mm* mm, uint64 va) {
    assert(holding(&mm->lock));
    assert(PGALIGNED(va));

    uint64 pa  = (uint64)kallocpage();
    pte_t* pte = walk(mm, va, 0);
    assert(pte != NULL);

    int idx        = (*pte >> 16) & 0xffffffff;
    void* swapaddr = SWAPINDEX_TO_ADDR(idx);
    memmove((void*)PA_TO_KVA(pa), swapaddr, PGSIZE);
    used[idx] = 0;

    // reset the PTE.
    *pte = (*pte & PTE_RWX) | PTE_V | PTE_U | PA2PTE(pa);

    return 0;
}

static int find_swap_idx(void) {
    for (int i = 0; i < SWAPAREA_SIZE / PGSIZE; i++) {
        if (used[i] == 0) {
            used[i] = 1;
            return i;
        }
    }
    return -1;
}

static uint64 grasp_page(struct mm* mm, int check_ad) {
    uint64 pa       = 0;
    struct vma* vma = mm->vma;
    while (vma) {
        for (uint64 va = vma->vm_start; va < vma->vm_end; va += PGSIZE) {
            pte_t* pte = walk(mm, va, 0);
            if (pte != NULL && (*pte & PTE_V) && (*pte & PTE_U)) {
                if (check_ad && ((*pte & PTE_A) || (*pte & PTE_D))) {
                    // this page is used, skip it.
                    continue;
                }
                // it's ok to swap out this page.
                infof("swap out: %p, pa = %p", va, PTE2PA(*pte));

                // find a page in the swap area.
                int idx = find_swap_idx();
                if (idx < 0)
                    goto out;
                void* swap_addr = SWAPINDEX_TO_ADDR(idx);

                // copy the page to the swap area.
                pa = PTE2PA(*pte);
                memmove(swap_addr, (void*)PA_TO_KVA(pa), PGSIZE);

                // clear the page table entry, but keep the permission RWX.
                *pte = (*pte & PTE_RWX) | 0xbbbb000000000000ull | (idx << 16);
                goto out;
            }
        }
        vma = vma->next;
    }
out:
    return pa;
}

/**
 * @brief Choose a page to swap out, return the physical address of the page.
 */
uint64 swap_out() {
    uint64 pa = 0;
    struct proc* p;
    struct proc* cp = curr_proc();
    extern struct proc* pool[];
    int locked = 0;
    for (int i = 0; i < NPROC; i++) {
        p = pool[i];
        if (p == cp || holding(&p->lock))
            continue;
        acquire(&p->lock);
        if (p->state == RUNNABLE || p->state == SLEEPING || p->state == ZOMBIE) {
            acquire(&p->mm->lock);
            pa = grasp_page(p->mm, 1);
            release(&p->mm->lock);
        }
        release(&p->lock);
        if (pa != 0)
            break;
    }
    if (pa == 0) {
        for (int i = 0; i < NPROC; i++) {
            p = pool[i];
            if (p == cp || holding(&p->lock))
                continue;
            acquire(&p->lock);

            if (p->state == RUNNABLE || p->state == SLEEPING || p->state == ZOMBIE) {
                acquire(&p->mm->lock);

                pa = grasp_page(p->mm, 0);  // <-- really critical now, ignore A/D bit

                release(&p->mm->lock);
            }
            release(&p->lock);
            if (pa != 0)
                break;
        }
    }
    assert(pa);
    return pa;
}