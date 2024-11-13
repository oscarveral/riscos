// + DEISO - P2

#include "vma.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void mm_init(struct mm *mm)
{
    mm->first_vma = 0;
    for (int i = 0; i < MAX_VMA; i++)
    {
        mm->vmas[i].start = 0;
        mm->vmas[i].len = 0;
        mm->vmas[i].file = 0;
        mm->vmas[i].prot = 0;
        mm->vmas[i].flags = 0;
        mm->vmas[i].next = 0;
    }
}

uint64 *create_mapping(struct mm *mm, uint64 len, struct file *f, int prot, int flags)
{
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
        return (uint64*)-1;
    }

    uint64 start = 0;
    struct vma *cur = mm->first_vma;
    if (cur == 0)
        start = PGROUNDDOWN(TRAPFRAME - len);
    else {
        while (cur->next != 0)
            cur = cur->next;
        start = PGROUNDDOWN(cur->start - len);
    }

    vma->start = start;
    vma->file = f;
    vma->len = len;
    vma->prot = prot;
    vma->flags = flags;
    vma->next = 0;

    if (mm->first_vma == 0)
    {
        mm->first_vma = vma;
    }
    else
    {
        cur->next = vma;
    }

    filedup(f);

    return (uint64*)vma->start;
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

void mm_destroy(struct mm *mm)
{
    struct vma *cur = mm->first_vma;
    while (cur != 0)
    {
        struct vma *next = cur->next;
        cur->start = 0;
        cur->len = 0;
        fileclose(cur->file);
        cur->file = 0;
        cur->prot = 0;
        cur->flags = 0;
        cur->next = 0;
        cur = next;
    }
    mm->first_vma = 0;
}

void mm_copy(struct mm *src, struct mm *dst)
{
    // TODO: Implement this function
}

// - DEISO - P2