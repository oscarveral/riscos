#define MAX_VMAS 16

struct VMA{
    int used; 
    unsigned int length;
    unsigned int long vma_start;
    unsigned int long vma_end;
    struct file *fp;
    int prot;
    int flags;
    unsigned int long offset;
    struct VMA *vma_next;
};