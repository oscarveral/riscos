// + DEISO - P2

#include "vma.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "file.h"
#include "fcntl.h"

uint64 *create_mapping(struct proc *p, uint64 addr, uint64 len, struct file *f, int prot, int flags)
{
    if (prot & PROT_WRITE && flags & MAP_SHARED && f->writable == 0)
    {
        return (uint64 *)-1;
    }

    struct mm *mm = &p->mm;
    struct vma *vma = 0;
    for (int i = 0; i < MAX_VMA; i++)
    {
        if (mm->vmas[i].start == 0)
        {
            vma = &mm->vmas[i];
            break;
        }
    }
    if (vma == 0)
    {
        return (uint64 *)-1;
    }

    uint64 start = 0;
    struct vma *cur = mm->first_vma;
    if (cur == 0)
        start = PGROUNDDOWN(TRAPFRAME - len);
    else
    {
        while (cur->next != 0)
        {
            cur = cur->next;
        }
        start = PGROUNDDOWN(cur->start - len);
    }

    if (addr != 0) {
        start = PGROUNDDOWN(start);
    }

    vma->start = start;
    vma->file = f;
    vma->len = len;
    vma->prot = prot;
    vma->flags = flags;
    vma->next = 0;
    vma->prev = 0;

    if (mm->first_vma == 0)
    {
        mm->first_vma = vma;
    }
    else
    {
        cur->next = vma;
        vma->prev = cur;
    }

    filedup(f);

    return (uint64 *)vma->start;
}

int alloc_mapping(struct proc *p, uint64 addr)
{
    struct vma *mapping = find_vma(&p->mm, addr);
    if (mapping == (struct vma *)-1)
    {
        printf("alloc_mapping(): find_vma failed pid=%d va=0x%lx\n", p->pid, addr);
        return -1;
    }
    uint64 *mem = kalloc();
    if (mem == 0)
    {
        printf("alloc_mapping(): out of memory pid=%d va=0x%lx\n", p->pid, addr);
        return -1;
    }

    memset(mem, 0, PGSIZE);
    uint64 user_mem = PGROUNDDOWN(addr);
    if (mappages(p->pagetable, user_mem, PGSIZE, (uint64)mem, mapping->prot | PTE_U) != 0)
    {
        printf("alloc_mapping(): mappages failed pid=%d va=0x%lx\n", p->pid, addr);
        kfree(mem);
        return -1;
    }

    if (mapping->prot & PROT_WRITE && mapping->file->writable == 0)
    {
        printf("alloc_mapping(): write to read-only file pid=%d va=0x%lx\n", p->pid, addr);
        return -1;
    }

    struct inode *ip = mapping->file->ip;
    int offset = user_mem - mapping->start;
    int n = (ip->size - offset) < PGSIZE ? (ip->size - offset) : PGSIZE;
    ilock(ip);
    if (readi(ip, 0, (uint64)mem, offset, n) != n)
    {
        printf("usertrap(): readi failed pid=%d va=0x%lx\n", p->pid, addr);
        kfree(mem);
        iunlock(ip);
        return -1;
    };
    iunlock(ip);

    return 0;
}

int delete_mapping(struct proc *p, uint64 addr, uint64 len)
{
    struct mm *mm = &p->mm;
    struct vma *vma = find_vma(mm, (uint64)addr);
    if (vma == (struct vma *)-1)
        return -1;

    if (addr % PGSIZE != 0)
        return -1;

    if (addr + len - 1 > vma->start + vma->len - 1)
        return -1;

    dealloc_mapping(p, addr, len, vma);

    if (addr == vma->start && len == vma->len)
    {
        fileclose(vma->file);
        if (vma->prev != 0)
            vma->prev->next = vma->next;
        if (vma->next != 0)
            vma->next->prev = vma->prev;
        if (mm->first_vma == vma)
            mm->first_vma = vma->next;

        vma->start = 0;
        vma->len = 0;
        vma->file = 0;
        vma->prot = 0;
        vma->flags = 0;
        vma->next = 0;
        vma->prev = 0;
    }
    else if (addr == vma->start)
    {
        vma->start += len;
        vma->len -= len;
    }
    else
    {
        vma->len -= len;
    }

    return 0;
}

void dealloc_mapping(struct proc *p, uint64 addr, uint64 len, struct vma *vma)
{
    if (vma->prot & PROT_WRITE && vma->flags & MAP_SHARED && vma->file->writable == 0) {
        panic("dealloc_mapping: bad options");
    }

    if ((addr % PGSIZE) != 0) {
        panic("dealloc_mapping: unaligned addr");
    }

    uint64 a = addr;
    uint64 npages = PGROUNDUP(len) / PGSIZE;
    pte_t *pte = 0;
    for (a = addr; a < addr + npages * PGSIZE; a += PGSIZE) {
        // Basic checks.
        if((pte = walk(p->pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        if(PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        // We can only free valid pages.
        if((*pte & PTE_V) != 0){
            uint64 pa = PTE2PA(*pte);
            // Before freeing memory we must writeback the data if necesary.
            if (vma->prot & PROT_WRITE && vma->flags & MAP_SHARED && vma->file->writable != 0) {
                // Calculations to write only inside file size range (even if file is smaller than allocated pages)
                struct inode *ip = vma->file->ip;
                int offset = a - vma->start;
                int n =  (ip->size - offset) < PGSIZE ? (ip->size - offset) : PGSIZE;
                // Translated from filewrite on file.c
                int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
                int i = 0;
                int r = 0;
                while (i < n)
                {
                    int n1 = n - i;
                    if(n1 > max) n1 = max;
                    begin_op(),
                    ilock(ip); 
                    if ((r = writei(ip, 0, pa + i, offset, n1)) > 0) offset += r;
                    iunlock(ip);
                    end_op();
                    if(r != n1) break;
                    i += r;            
                }
            }
            kfree((void*)pa);
        }
        *pte = 0;
    }
}

struct vma *find_vma(struct mm *mm, uint64 addr)
{
    for (int i = 0; i < MAX_VMA; i++)
    {
        if (mm->vmas[i].start <= addr && addr < mm->vmas[i].start + mm->vmas[i].len)
        {
            return &mm->vmas[i];
        }
    }
    return (struct vma *)-1;
}

void mm_init(struct proc *p)
{
    struct mm *mm = &p->mm;
    mm->first_vma = 0;
    for (int i = 0; i < MAX_VMA; i++)
    {
        mm->vmas[i].start = 0;
        mm->vmas[i].len = 0;
        mm->vmas[i].file = 0;
        mm->vmas[i].prot = 0;
        mm->vmas[i].flags = 0;
        mm->vmas[i].next = 0;
        mm->vmas[i].prev = 0;
    }
}

void mm_destroy(struct proc *p)
{
    struct mm *mm = &p->mm;
    for (int i = 0; i < MAX_VMA; i++)
    {
        if (mm->vmas[i].start != 0)
        {
            struct vma *vma = &mm->vmas[i];      
            dealloc_mapping(p, vma->start, vma->len, vma);
            fileclose(vma->file);
            vma->start = 0;
            vma->len = 0;
            vma->file = 0;
            vma->prot = 0;
            vma->flags = 0;
            vma->next = 0;
            vma->prev = 0;
        }
    }
    mm->first_vma = 0;
}

void mm_copy(struct proc *src, struct proc *dst)
{
    struct vma *cur = src->mm.first_vma;
    
    if (cur == 0) return;
    while (cur != 0) {
        create_mapping(dst, cur->start, cur->len, cur->file, cur->prot, cur->flags);
        cur = cur->next;
    }
}

// - DEISO - P2