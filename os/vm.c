#include "vm.h"

#include "defs.h"
#include "kalloc.h"

allocator_t mm_allocator;
allocator_t vma_allocator;

void uvm_init() {
    allocator_init(&mm_allocator, "mm", sizeof(struct mm), 16384);
    allocator_init(&vma_allocator, "vma", sizeof(struct vma), 16384);
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(struct mm *mm, uint64 va, int alloc) {
    assert(holding(&mm->lock));

    pagetable_t pagetable = mm->pgt;

    if (!IS_USER_VA(va))
        panic("invalid user VA");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PA_TO_KVA(PTE2PA(*pte));
        } else {
            if (!alloc || (pagetable = (pde_t *)PA_TO_KVA(kallocpage())) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(KVA_TO_PA(pagetable)) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 __pa walkaddr(struct mm *mm, uint64 va) {
    if (!IS_USER_VA(va))
        panic("invalid user VA");

    assert_str(PGALIGNED(va), "unaligned va %p", va);
    assert(holding(&mm->lock));

    pte_t *pte;
    uint64 pa;

    pte = walk(mm, va, 0);
    if (pte == NULL)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0) {
        warnf("walkaddr returns kernel pte: %p, %p", va, *pte);
        return 0;
    }
    pa = PTE2PA(*pte);
    return pa;
}

// Look up a virtual address, return the physical address,
uint64 useraddr(struct mm *mm, uint64 va) {
    uint64 page = walkaddr(mm, PGROUNDDOWN(va));
    if (page == 0)
        return 0;
    return page | (va & 0xFFFULL);
}

struct mm *mm_create() {
    struct mm *mm = kalloc(&mm_allocator);
    memset(mm, 0, sizeof(*mm));
    spinlock_init(&mm->lock, "mm");
    mm->vma    = NULL;
    mm->refcnt = 1;

    mm->pgt = (pagetable_t)PA_TO_KVA(kallocpage());
    if (!mm->pgt)
        goto free_mm;
    memset(mm->pgt, 0, PGSIZE);

    acquire(&mm->lock);
    return mm;

free_mm:
    kfree(&mm_allocator, mm);
    return NULL;
}

struct vma *mm_create_vma(struct mm *mm) {
    assert(holding(&mm->lock));

    struct vma *vma = kalloc(&vma_allocator);
    memset(vma, 0, sizeof(*vma));
    vma->owner = mm;
    return vma;
}

void mm_free_pages(struct mm *mm) {
    assert(holding(&mm->lock));

    struct vma *next, *vma = mm->vma;
    while (vma) {
        freevma(vma, true);
        next = vma->next;
        kfree(&vma_allocator, vma);
        vma = next;
    }
    mm->vma = NULL;
}

void mm_free(struct mm *mm) {
    acquire(&mm->lock);
    mm_free_pages(mm);
    release(&mm->lock);
    
    kfreepage((void *)KVA_TO_PA(mm->pgt));
    mm->refcnt--;
    if (mm->refcnt == 0) {
        kfree(&mm_allocator, mm);
    }
}

void freevma(struct vma *vma, int free_phy_page) {
    assert(holding(&vma->owner->lock));
    assert(PGALIGNED(vma->vm_start) && PGALIGNED(vma->vm_end));

    struct mm *mm = vma->owner;
    for (uint64 va = vma->vm_start; va < vma->vm_end; va += PGSIZE) {
        pte_t *pte = walk(mm, va, false);
        if ((*pte & PTE_V)) {
            if (free_phy_page)
                kfreepage((void *)PTE2PA(*pte));
            *pte = 0;
        } else {
            warnf("free unmapped address %p", va);
        }
    }
    sfence_vma();
}

/**
 * @brief Map virtual address defined in @vma.
 * Addresses must be aligned to PGSIZE.
 * Physical pages are allocated automatically.
 * Caller should then use walkaddr to resolve the mapped PA, and do initialization.
 *
 * @param vma
 * @return int
 */
int mm_mappages(struct vma *vma) {
    if (!IS_USER_VA(vma->vm_start) || !IS_USER_VA(vma->vm_end))
        panic("user mappages beyond USER_TOP, va: [%p, %p)", vma->vm_start, vma->vm_end);

    assert(PGALIGNED(vma->vm_start));
    assert(PGALIGNED(vma->vm_end));
    assert((vma->pte_flags & PTE_R) || (vma->pte_flags & PTE_W) || (vma->pte_flags & PTE_X));

    assert(holding(&vma->owner->lock));

    tracef("mappages: [%p, %p)", vma->vm_start, vma->vm_end);

    struct mm *mm = vma->owner;
    uint64 va;
    void *pa;
    pte_t *pte;

    for (va = vma->vm_start; va < vma->vm_end; va += PGSIZE) {
        if ((pte = walk(mm, va, 1)) == 0) {
            errorf("pte invalid, va = %p", va);
            return -1;
        }
        if (*pte & PTE_V) {
            errorf("remap %p", va);
            return -1;
        }
        pa = kallocpage();
        if (!pa) {
            errorf("kallocpage");
            return -1;
        }
        // memset((void *)PA_TO_KVA(pa), 0, PGSIZE);
        *pte = PA2PTE(pa) | vma->pte_flags | PTE_V;
    }
    sfence_vma();

    vma->next = mm->vma;
    mm->vma   = vma;

    return 0;
}

int mm_mappageat(struct mm *mm, uint64 va, uint64 __pa pa, uint64 flags) {
    assert(holding(&mm->lock));
    tracef("mappagesat: %p -> %p", va, pa);

    struct vma *vma = kalloc(&vma_allocator);
    memset(vma, 0, sizeof(*vma));
    vma->owner     = mm;
    vma->pte_flags = flags;
    vma->vm_start  = va;
    vma->vm_end    = va + PGSIZE;

    pte_t *pte;

    if ((pte = walk(mm, va, 1)) == 0) {
        errorf("pte invalid, va = %p", va);
        return -1;
    }
    if (*pte & PTE_V) {
        errorf("remap %p", va);
        vm_print(mm->pgt);
        return -1;
    }
    *pte = PA2PTE(pa) | vma->pte_flags | PTE_V;
    sfence_vma();

    return 0;
}

// Used in fork.
// Copy the pagetable page and all the user pages.
// Return 0 on success, -1 on error.
int mm_copy(struct mm *old, struct mm *new) {
    // infof("old mm:");
    // mm_print(old);
    // infof("new mm:");
    // mm_print(new);

    assert(holding(&old->lock));
    assert(holding(&new->lock));
    struct vma *vma = old->vma;

    while (vma) {
        tracef("fork: mapping [%p, %p)", vma->vm_start, vma->vm_end);
        struct vma *new_vma = mm_create_vma(new);
        new_vma->vm_start   = vma->vm_start;
        new_vma->vm_end     = vma->vm_end;
        new_vma->pte_flags  = vma->pte_flags;
        if (mm_mappages(new_vma)) {
            warnf("mm_mappages");
            goto err;
        }
        for (uint64 va = vma->vm_start; va < vma->vm_end; va += PGSIZE) {
            void *__kva pa_old = (void *)PA_TO_KVA(walkaddr(old, va));
            void *__kva pa_new = (void *)PA_TO_KVA(walkaddr(new, va));
            memmove(pa_new, pa_old, PGSIZE);
        }
        vma = vma->next;
    }

    return 0;
err:
    mm_free_pages(new);
    return -1;
}
