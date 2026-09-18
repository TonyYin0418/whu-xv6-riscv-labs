// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

struct {
  // Serializes only cache misses that must recycle a buffer.
  struct spinlock evictlock;
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKET];
} bcache;

static uint
hash(uint blockno)
{
  return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evictlock, "bcache.evict");
  for(int i = 0; i < NBUCKET; i++)
    initlock(&bcache.bucket[i].lock, "bcache.bucket");

  // Initially put all unused buffers in one bucket; recycling rehashes them.
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->dev = (uint)-1;
    b->next = bcache.bucket[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[0].head = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf **pp;
  uint h, oldh;

  h = hash(blockno);
  acquire(&bcache.bucket[h].lock);
  for(b = bcache.bucket[h].head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[h].lock);

  // A miss is uncommon. Serialize recycling, then recheck the target bucket.
  acquire(&bcache.evictlock);
  acquire(&bcache.bucket[h].lock);
  for(b = bcache.bucket[h].head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);
      release(&bcache.evictlock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[h].lock);

  // Find and detach an unused buffer while holding its own bucket lock.
  b = 0;
  for(oldh = 0; oldh < NBUCKET && b == 0; oldh++){
    acquire(&bcache.bucket[oldh].lock);
    pp = &bcache.bucket[oldh].head;
    while(*pp){
      if((*pp)->refcnt == 0){
        b = *pp;
        *pp = b->next;
        break;
      }
      pp = &(*pp)->next;
    }
    release(&bcache.bucket[oldh].lock);
  }
  if(b == 0)
    panic("bget: no buffers");

  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  acquire(&bcache.bucket[h].lock);
  b->next = bcache.bucket[h].head;
  bcache.bucket[h].head = b;
  release(&bcache.bucket[h].lock);
  release(&bcache.evictlock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.bucket[hash(b->blockno)].lock);
  b->refcnt--;
  release(&bcache.bucket[hash(b->blockno)].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.bucket[hash(b->blockno)].lock);
  b->refcnt++;
  release(&bcache.bucket[hash(b->blockno)].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.bucket[hash(b->blockno)].lock);
  b->refcnt--;
  release(&bcache.bucket[hash(b->blockno)].lock);
}

