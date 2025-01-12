// + DEISO - P2

#include "vma.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "file.h"
#include "fcntl.h"

uint64 *create_vma_program(struct mm *mm, uint64 addr, uint64 len, struct inode *ip, uint64 len_limit, uint64 off, int prot, int flags) {
    
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
    vma->len_limit = len_limit;
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

    idup(ip);

    return (uint64 *)vma->start;
}

uint64 *create_vma_stack(struct mm *mm, uint64 addr, uint64 len, int prot, int flags) {

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
    vma->type = STACK;
    vma->file = 0;
    vma->ip = 0;
    vma->len = len;
    vma->len_limit = len;
    vma->off = 0;
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


uint64 *create_vma_file(struct mm *mm, uint64 len, struct file *f, uint64 off, int prot, int flags) {

    if (f == 0) return (uint64 *) -1;
    if (prot & PROT_WRITE && flags & MAP_SHARED && f->writable == 0) return (uint64 *) -1;
    
    struct vma *vma = 0;
    for (int i = 0; i < MAX_VMA; ++i) 
        if (mm->vmas[i].type == NONE) {
            vma = &mm->vmas[i];
            break;
        }
    if (vma == 0) return (uint64 *) -1;

    struct vma *cur = mm->first_vma;
    uint64 start = PGROUNDDOWN(TRAPFRAME - len);
    if (cur != 0) {
        while (cur->next != 0) { 
            cur = cur->next;
            if (cur->type == FILE) start -= PGROUNDUP(cur->len);
        }
    }

    vma->start = start;
    vma->type = FILE;
    vma->file = f;
    vma->ip = f->ip;
    vma->len = len;
    vma->len_limit = len;
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

    if (vma->type == NONE) return -1;

    int prots = vma->prot | PTE_U;
    if (vma->flags & MAP_SHARED)  prots |= PTE_SHARED;

    uint64 user_mem = PGROUNDDOWN(addr);

    uint64 *mem = kalloc();
    if (mem == 0) return -1;
    memset(mem, 0, PGSIZE);

    if (mappages(pagetable, user_mem, PGSIZE, (uint64)mem, prots) != 0)
    {
        kfree(mem);
        return -1;
    }

    if (vma->type == STACK) return 0;

    uint64 page_count_bytes = PGROUNDDOWN(addr - vma->start);
    uint64 offset = page_count_bytes + vma->off;
    // Edge case where where it will try to read after the end if addr is big enough.
    if (vma->len_limit < page_count_bytes) return 0;
    uint64 rem = vma->len_limit - page_count_bytes;
    uint64 n = PGSIZE >= rem ? rem : PGSIZE;
    if (offset + n > vma->ip->size) n = vma->ip->size - offset;
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
    
    struct vma *vma = find_vma(mm, addr);
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
                uint64 page_count_bytes = PGROUNDDOWN(a - vma->start);
                uint64 offset = page_count_bytes + vma->off;
                uint64 rem = vma->len_limit - page_count_bytes;
                uint64 n = PGSIZE >= rem ? rem : PGSIZE;
                if (offset + n > vma->ip->size) n = vma->ip->size - offset;

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
            if (getref((void *)pa) == 1) kfree((void *)pa);
            else decref((void *)pa);
        }
        *pte = 0;
    }

    if (addr == vma->start && len == vma->len)
    {
        if (vma->type == FILE) fileclose(vma->file);
        if (vma->type == PROGRAM) iput(vma->ip);
        if (vma->prev != 0) vma->prev->next = vma->next;
        if (vma->next != 0) vma->next->prev = vma->prev;
        if (mm->first_vma == vma) mm->first_vma = vma->next;

        vma->start = 0;
        vma->len = 0;
        vma->type = NONE;
        vma->off = 0;
        vma->file = 0;
        vma->len_limit = 0;
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

struct vma *find_vma(struct mm *mm, uint64 addr)
{
    for (int i = 0; i < MAX_VMA; i++)
    {
        struct vma * vma = &mm->vmas[i];
        if (vma->type != NONE && (vma->start <= addr) && (addr < (vma->start + vma->len)))
        {
            return vma;
        }
    }
    return (struct vma *)-1;
}

void mm_init(struct mm *mm)
{
    mm->first_vma = 0;
    for (int i = 0; i < MAX_VMA; i++)
    {
        mm->vmas[i].start = 0;
        mm->vmas[i].len = 0;
        mm->vmas[i].type = NONE;
        mm->vmas[i].file = 0;
        mm->vmas[i].ip = 0;
        mm->vmas[i].off = 0;
        mm->vmas[i].len_limit = 0;
        mm->vmas[i].prot = 0;
        mm->vmas[i].flags = 0;
        mm->vmas[i].next = 0;
        mm->vmas[i].prev = 0;
    }
}

void mm_destroy(struct mm *mm, pagetable_t pagetable)
{
    for (int i = 0; i < MAX_VMA; i++)
    {
        if (mm->vmas[i].type != NONE)
        {
            struct vma *vma = &mm->vmas[i];
            delete_vma(mm, pagetable, vma->start, vma->len);
        }
    }
    mm->first_vma = 0;
}

void mm_copy(struct mm *src, struct mm *dst)
{
    struct vma * cur = src->first_vma;

    if (cur == 0)
        return;
    while (cur != 0)
    {
        switch(cur->type) {
            case PROGRAM:
                create_vma_program(dst, cur->start, cur->len, cur->ip, cur->len_limit, cur->off, cur->prot, cur->flags);
                break;
            case FILE:
                create_vma_file(dst, cur->len, cur->file, cur->off, cur->prot, cur->flags);
                break;
            case STACK:
                create_vma_stack(dst, cur->start, cur->len, cur->prot, cur->flags);
                break;
            default:
                break;
        }
        cur = cur->next;
    }
}

// - DEISO - P2