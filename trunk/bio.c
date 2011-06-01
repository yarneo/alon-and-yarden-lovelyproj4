// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
// 
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to flush it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
// 
// The implementation uses three state flags internally:
// * B_BUSY: the block has been returned from bread
//     and has not been passed back to brelse.  
// * B_VALID: the buffer data has been initialized
//     with the associated disk block contents.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "buf.h"

struct {
	struct spinlock lock;
	struct buf buf[NBUF];

	// Linked list of all buffers, through prev/next.
	// head.next is most recently used.
	struct buf head;
} bcache;

struct buf* anchor_table[HASHSIZE]; /* the table elements */

int newaddition = 0;

uint hash(uint dev, uint sector)
{
	uint key = (dev+sector)*(dev+sector+1)+sector;
	key = (key << 15) - key - 1;
	key = key ^ (key >> 12);
	key = key + (key << 2);
	key = key ^ (key >> 4);
	key = (key + (key << 3)) + (key << 11);
	key = key ^ (key >> 16);
	return key % HASHSIZE;
}

void printcache(void) {
	//BC = [<d#,s#,i#> ,  <d#,s#,i#>, <d#,s#,i#> , <d#,s#,i#> �. <d#,s#,i#>]

	struct buf* b;

	cprintf("BC = [");
	for(b = bcache.head.next; b != &bcache.head; b = b->next){
		if(b == bcache.head.prev) {
			if(b->inum == 0 && b->dev != -1)
				cprintf("<%d,K,K>",b->dev);
			else
				cprintf("<%d,%d,%d>",b->dev,b->sector,b->inum);
		}
		else {
			if(b->inum == 0 && b->dev != -1)
				cprintf("<%d,K,K> , ",b->dev);
			else
				cprintf("<%d,%d,%d> , ",b->dev,b->sector,b->inum);
		}
	}
	cprintf("]\n");
}

int
countblocks(uint dev, uint inum, struct buf** tmpbuf)
{

	struct buf *b;
	struct buf *head = 0;
	int count=0;


	for(b = bcache.head.next; b != &bcache.head; b = b->next){
		if((b->dev == dev) && (b->inum == inum)) {
			count++;
			if (count == 1) {
				head = b;
				*tmpbuf = b;
			}
			b->searchprev = *tmpbuf;
			*tmpbuf = b;
		}
	}

	head->searchprev = *tmpbuf;
	*tmpbuf = head;
	//cprintf("device: %d, inode: %d, tmpbuf: %d count: %d\n",dev,inum,*tmpbuf,count);
	return count;
}

void
binit(void)
{
	struct buf *b;

	initlock(&bcache.lock, "bcache");

	// Create linked list of buffers
	bcache.head.prev = &bcache.head;
	bcache.head.next = &bcache.head;
	for(b = bcache.buf; b < bcache.buf+NBUF; b++){
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		b->dev = -1;
		bcache.head.next->prev = b;
		bcache.head.next = b;
		b->bnext = 0;
		b->bprev = 0;
		b->searchnext = 0;
		b->searchprev = 0;
	}

	int i;
	for(i=0;i<HASHSIZE;i++) {
		anchor_table[i] = 0;
	}
}

// Look through buffer cache for sector on device dev.
// If not found, allocate fresh block.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint sector, uint inodenum)
{

	struct buf *b;
	acquire(&bcache.lock);
	int counter = 0;
	struct buf* tmpbuf=0;
	uint hashval = hash(dev, sector);

	if(anchor_table[hashval] != 0) {
		loop:
		// Try for cached block.
		for(b = anchor_table[hashval]; b != 0; b = b->bnext){
			//	  cprintf("in loop, at: %d, prev: %d, next: %d\n",b,b->bprev,b->bnext);
			if(b->dev == dev && b->sector == sector){
				if(!(b->flags & B_BUSY)){
					b->flags |= B_BUSY;
					release(&bcache.lock);
					return b;
				}
				sleep(b, &bcache.lock);
				goto loop;
			}
		}
	}
	// Allocate fresh block.
	if ((SRP >= 3) && (inodenum != 0)) {
		counter = countblocks(dev, inodenum, &tmpbuf);
		//cprintf("tmpbuf from bget: %d\n",tmpbuf);
	}
	if((counter < SRP) || (SRP < 3) || (inodenum == 0)) {
		for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
			//cprintf("allocate fresh block %d\n",b);
			if((b->flags & B_BUSY) == 0){
				//cprintf("allocate fresh block %d inside\n",b);
				if(b->dev != -1) {
					uint hashval2 = hash(b->dev, b->sector);
					if(anchor_table[hashval2] == b) {
						anchor_table[hashval2] = 0;
					}
				}
				b->dev = dev;
				b->sector = sector;
				b->flags = B_BUSY;
				b->inum = inodenum;
#ifdef TRUE
				printcache();
#endif
				release(&bcache.lock);
				return b;
			}
		}
	}
	else {
		//Replace the block of the current inode
		if(tmpbuf == 0) {
			panic("Error finding block of inode");
		}
		loop2:
		for(b = tmpbuf->searchprev; ;b = b->searchprev){
			if((b->flags & B_BUSY) == 0){
				uint hashval2 = hash(b->dev, b->sector);
				if(anchor_table[hashval2] == b) {
					anchor_table[hashval2] = 0;
				}
				b->dev = dev;
				b->sector = sector;
				b->flags = B_BUSY;
				b->inum = inodenum;
#ifdef TRUE
				printcache();
#endif
				release(&bcache.lock);
				return b;
			}
			if(b == tmpbuf)
				break;
		}
		sleep(tmpbuf, &bcache.lock);
		goto loop2;
	}
	panic("bget: no buffers");

}

// Return a B_BUSY buf with the contents of the indicated disk sector.
struct buf*
bread(uint dev, uint sector, uint inodenum)
{
	struct buf *b;

	b = bget(dev, sector, inodenum);
	if(!(b->flags & B_VALID))
		iderw(b);
	return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
	if((b->flags & B_BUSY) == 0)
		panic("bwrite");
	b->flags |= B_DIRTY;
	iderw(b);
}

// Release the buffer b.
void
brelse(struct buf *b)
{
	if((b->flags & B_BUSY) == 0)
		panic("brelse");

	acquire(&bcache.lock);

	uint hashval = hash(b->dev, b->sector);
	if(anchor_table[hashval] == 0) {
		//	  cprintf("anchor is -1\n");
		anchor_table[hashval] = b;
		if(b->bprev != 0)
			b->bprev->bnext = b->bnext;
		if(b->bnext != 0)
			b->bnext->bprev = b->bprev;
		b->bprev = 0;
		b->bnext = 0;
	}
	else if(anchor_table[hashval] != b) {
		//	  cprintf("im here to work on %d\n",b);

		//close gaps of node before and after the current node.
		if((b->bprev != 0) && (b->bnext != 0)) {
			b->bprev->bnext = b->bnext;
			b->bnext->bprev = b->bprev;
		}
		else if((b->bprev != 0) && (b->bnext == 0)) {
			b->bprev->bnext = 0;
		}

		//make next the current anchor (put it first)
		b->bnext = anchor_table[hashval];
		//because it is first, prev is -1.
		b->bprev = 0;

		//previous of current anchor is b
		anchor_table[hashval]->bprev = b;

		//the new anchor is now b
		anchor_table[hashval] = b;
		//	  cprintf("im done, current is %d %d %d, his prev is %d, his next is %d %d\n",b,b->bprev->bnext,b->bnext->bprev,b->bprev,b->bnext);
	}

	b->next->prev = b->prev;
	b->prev->next = b->next;
	b->next = bcache.head.next;
	b->prev = &bcache.head;
	bcache.head.next->prev = b;
	bcache.head.next = b;

	b->flags &= ~B_BUSY;
	wakeup(b);

	release(&bcache.lock);
}