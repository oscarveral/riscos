// + DEISO - P2

#include "vma.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "file.h"
#include "fcntl.h"

uint64 *create_vma_program(struct mm *mm, uint64 addr, uint64 len, struct inode *ip, uint64 off, int prot, int flags) {

    if (ip == 0)  return (uint64 *) -1;

    struct vma *vma = 0;
    for (int i = 0; i < MAX_VMA; ++i) 
        if (mm->vmas[i].type == NONE) {
            vma = &mm->vmas[i];
            break;
        }
    if (vma == 0) return (uint64 *) -1;

    struct vma *cur = mm->first_vma;
    if (cur != 0) 
        while (cur->next != 0)
            cur = cur->next;

    uint64 start = PGROUNDDOWN(addr);

    vma->start = start;
    vma->type = PROGRAM;
    vma->file = 0;
    vma->ip = ip;
    vma->len = len;
    vma->off = off;
    vma->prot = prot;
    vma->flags = flags;
    vma->next = 0;
    vma->prev = 0;        

    if (mm->first_vma == 0) mm->first_vma = vma;
    else
    {
        cur->next = vma;
        vma->prev = cur;
    }

    return (uint64 *)vma->start;
}

uint64 *create_vma_file(struct mm *mm, int use_addr, uint64 addr, uint64 len, struct file *f, uint64 off, int prot, int flags) {
    
    if (f == 0) return (uint64 *) -1;
    if (prot & PROT_WRITE && flags & MAP_SHARED && f->writable == 0) return (uint64 *) -1;
    
    struct vma *vma = 0;
    for (int i = 0; i < MAX_VMA; ++i) 
        if (mm->vmas[i].type == NONE) {
            vma = &mm->vmas[i];
            break;
        }
    if (vma == 0) return (uint64 *) -1;

    uint64 start = PGROUNDDOWN(addr);
    struct vma *cur = mm->first_vma;
    if (!use_addr) {
        if (cur == 0) start = PGROUNDDOWN(TRAPFRAME - len);
        else {
            while (cur->next != 0) cur = cur->next;
            start = PGROUNDDOWN(cur->start - len);
        }
    }

    vma->start = start;
    vma->type = FILE;
    vma->file = f;
    vma->ip = f->ip;
    vma->len = len;
    vma->off = off;
    vma->prot = prot;
    vma->flags = flags;
    vma->next = 0;
    vma->prev = 0;

    if (mm->first_vma == 0) mm->first_vma = vma;
    else
    {
        cur->next = vma;
        vma->prev = cur;
    }

    filedup(f);

    return (uint64 *) vma->start;
}

int alloc_vma(struct mm *mm, pagetable_t pagetable, uint64 addr) {

    struct vma *vma = find_vma(mm, addr);
    if (vma == (struct vma *)-1) return -1;

    uint64 *mem = kalloc();
    if (mem == 0) return -1;

    memset(mem, 0, PGSIZE);
    uint64 user_mem = PGROUNDDOWN(addr);

    int prots = vma->prot | PTE_U;
    if (vma->flags & MAP_SHARED)  prots |= PTE_SHARED;

    if (mappages(pagetable, user_mem, PGSIZE, (uint64)mem, prots) != 0)
    {
        kfree(mem);
        return -1;
    }

    int offset = (user_mem - vma->start) + vma->off;
    int n = (vma->ip->size - offset) < PGSIZE ? (vma->ip->size - offset) : PGSIZE;
    ilock(vma->ip);
    if (readi(vma->ip, 0, (uint64)mem, offset, n) != n)
    {
        kfree(mem);
        iunlock(vma->ip);
        return -1;
    };
    iunlock(vma->ip);

    return 0;
}

int delete_vma(struct mm *mm, pagetable_t pagetable, uint64 addr, uint64 len) {
    
    struct vma *vma = find_vma(mm, (uint64)addr);
    if (vma == (struct vma *)-1) return -1;

    if (vma->prot & PROT_WRITE && vma->flags & MAP_SHARED && vma->file->writable == 0) return -1;
    if (addr % PGSIZE != 0) return -1;
    if (addr + len - 1 > vma->start + vma->len - 1) return -1;

    uint64 a = addr;
    uint64 npages = PGROUNDUP(len) / PGSIZE;
    pte_t *pte = 0;
    for (a = addr; a < addr + npages * PGSIZE; a += PGSIZE)
    {
        if ((pte = walk(pagetable, a, 0)) == 0) panic("delete_mapping: walk");
        if (PTE_FLAGS(*pte) == PTE_V) panic("delete_mapping: not a leaf");
        if ((*pte & PTE_V) != 0)
        {
            uint64 pa = PTE2PA(*pte);
            if (vma->type == FILE 
                && PTE_FLAGS(*pte) & PTE_D 
                && vma->prot & PROT_WRITE 
                && vma->flags & MAP_SHARED 
                && vma->file->writable != 0)
            {
                struct inode *ip = vma->ip;
                int offset = (a - vma->start) + vma->off;
                int n = (ip->size - offset) < PGSIZE ? (ip->size - offset) : PGSIZE;

                int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
                int i = 0;
                int r = 0;
                while (i < n)
                {
                    int n1 = n - i;
                    if (n1 > max) n1 = max;
                    begin_op(),
                    ilock(ip);
                    if ((r = writei(ip, 0, pa + i, offset, n1)) > 0) offset += r;
                    iunlock(ip);
                    end_op();
                    if (r != n1) break;
                    i += r;
                }
            }
            kfree((void *)pa);
        }
        *pte = 0;
    }

    if (addr == vma->start && len == vma->len)
    {
        if (vma->type == FILE) fileclose(vma->file);
        if (vma->prev != 0) vma->prev->next = vma->next;
        if (vma->next != 0) vma->next->prev = vma->prev;
        if (mm->first_vma == vma) mm->first_vma = vma->next;

        vma->start = 0;
        vma->len = 0;
        vma->type = NONE;
        vma->off = 0;
        vma->file = 0;
        vma->ip = 0;
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
    else vma->len -= len;

    return 0;
}

void clone_vma(struct vma* vma, struct mm *dst) {
    switch (vma->type)
    {
    case FILE:
        create_vma_file(dst, 1, vma->start, vma->len, vma->file, vma->off, vma->prot, vma->flags);
        break;
    case PROGRAM:
        create_vma_program(dst, vma->start, vma->len, vma->ip, vma->off, vma->prot, vma->flags);
        break;
    default:
        break;
    }

}

struct vma *find_vma(struct mm *mm, uint64 addr)
{
    for (int i = 0; i < MAX_VMA; i++)
    {
        if (mm->vmas[i].type != NONE && mm->vmas[i].start <= addr && addr < mm->vmas[i].start + mm->vmas[i].len)
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
        mm->vmas[i].type = NONE;
        mm->vmas[i].file = 0;
        mm->vmas[i].ip = 0;
        mm->vmas[i].off = 0;
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
            delete_vma(&p->mm, p->pagetable, vma->start, vma->len);
        }
    }
    mm->first_vma = 0;
}

void mm_copy(struct proc *src, struct proc *dst)
{
    struct vma *cur = src->mm.first_vma;

    if (cur == 0)
        return;
    while (cur != 0)
    {
        clone_vma(cur, &dst->mm);
        cur = cur->next;
    }
}

// - DEISO - P2