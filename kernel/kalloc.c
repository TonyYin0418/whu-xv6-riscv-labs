// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

#define NPAGE ((PHYSTOP - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  struct run *freelist;
  // 每项对应一个物理页，和空闲链表共用 kmem.lock。
  int refcnt[NPAGE];
} kmem;

static int
refindex(void *pa)
{
  return ((uint64)pa - KERNBASE) / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // 初始化时先建立一个临时引用，再由 kfree() 放入空闲链表。
    kaddref(p);
    kfree(p);
  }
}

// fork 共享物理页时增加一个引用。
void
kaddref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kaddref");

  acquire(&kmem.lock);
  kmem.refcnt[refindex(pa)]++;
  release(&kmem.lock);
}

// 返回物理页当前的引用数。
int
kgetref(void *pa)
{
  int n;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kgetref");

  acquire(&kmem.lock);
  n = kmem.refcnt[refindex(pa)];
  release(&kmem.lock);
  return n;
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  if(kmem.refcnt[refindex(pa)] < 1)
    panic("kfree ref");
  kmem.refcnt[refindex(pa)]--;
  if(kmem.refcnt[refindex(pa)] > 0){
    release(&kmem.lock);
    return;
  }

  // 只有最后一个引用消失，物理页才真正回到空闲链表。
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    kmem.refcnt[refindex(r)] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
