#ifndef _FCNTL_H_
#define _FCNTL_H_

#include "riscv.h"

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

// + DEISO - P2
#define PROT_EXEC PTE_X
#define PROT_READ PTE_R
#define PROT_WRITE PTE_W
#define PROT_NONE 0

#define MAP_SHARED (1 << 0)
#define MAP_PRIVATE (1 << 1)
// - DEISO - P2

#endif // _FCNTL_H_