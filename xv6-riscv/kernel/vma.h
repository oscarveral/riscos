#ifndef _VMA_H_
#define _VMA_H_

// + DEISO - P2

#include "types.h"

#define MAX_VMA 32

enum vma_type {
    NONE,
    FILE,
    PROGRAM,
};

struct vma
{
    uint64 start;
    uint64 len;
    enum vma_type type;
    struct file *file;
    struct inode *ip;
    uint64 off;
    int prot;
    int flags;
    struct vma *next;
    struct vma *prev;
};

struct mm
{
    struct vma *first_vma;
    struct vma vmas[MAX_VMA];
};

// - DEISO - P2

#endif // _VMA_H_