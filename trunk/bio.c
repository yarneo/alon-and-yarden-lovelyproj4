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

int
countblocks(uint inum, struct buf* tmpbuf)
{
	struct buf *b;
	struct buf *head;
	int count=0;

	acquire(&bcache.lock);

	for(b = bcache.head.next; b != &bcache.head; b = b->next){
		if(b->inum == inum) {
			count++;
			if (count == 1) {
				head = tmpbuf;
			}
			tmpbuf->searchnext = b;
			b->searchprev = tmpbuf;
			tmpbuf = b;
		}
	}

	release(&bcache.lock);
	head->searchprev = tmpbuf;
	tmpbuf->searchnext = head;
	tmpbuf = head;
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
		b->bnext = (struct buf*)-1;
		b->bprev = (struct buf*)-1;
	}

	int i;
	for(i=0;i<HASHSIZE;i++) {
		anchor_table[i] = (struct buf*)-1;
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


	uint hashval = hash(dev, sector);

	//  cprintf("hash index: %d, value: %d\n",hashval,anchor_table[hashval]);
	if(anchor_table[hashval] != (struct buf*)-1) {
		loop:
		// Try for cached block.
		for(b = anchor_table[hashval]; b != (struct buf*)-1; b = b->bnext){
			//	  cprintf("in loop, at: %d, prev: %d, next: %d\n",b,b->bprev,b->bnext);
			if(b->dev == dev && b->sector == sector){
				if(!(b->flags & B_BUSY)){
					b->flags |= B_BUSY;
					release(&bcache.lock);
					b->inum = inodenum;
					return b;
				}
				sleep(b, &bcache.lock);
				goto loop;
			}
		}
	}
	// Allocate fresh block.
	if (SRP >= 3) {
		struct buf* tmpbuf=0;
		int counter = countblocks(inodenum, tmpbuf);
	}
	if((counter < SRP) || (SRP < 3)) {
		loop2:
		for(b = bcache.head.searchprev; b != &bcache.head; b = b->searchprev){
			//	  cprintf("allocate fresh block %d\n",b);
			if((b->flags & B_BUSY) == 0){
				//  	  cprintf("allocate fresh block %d inside\n",b);
				if(b->dev != -1) {
					uint hashval2 = hash(b->dev, b->sector);
					if(anchor_table[hashval2] == b) {

						anchor_table[hashval2] = (struct buf*)-1;
					}
				}
				b->dev = dev;
				b->sector = sector;
				b->flags = B_BUSY;
				b->inum = inodenum;
				release(&bcache.lock);
				return b;
			}
		}
		sleep(b, &bcache.lock);
		goto loop2;
	} else {
		//Replace the block of the current inode
		if(tmpbuf == 0) {
			panic("Error finding sector of inode");
		}
		for(b = tmpbuf.prev; b != &tmpbuf; b = b->prev){
			if((b->flags & B_BUSY) == 0){
				uint hashval2 = hash(b->dev, b->sector);
				if(anchor_table[hashval2] == b) {

					anchor_table[hashval2] = (struct buf*)-1;
				}
				b->dev = dev;
				b->sector = sector;
				b->flags = B_BUSY;
				b->inum = inodenum;
				release(&bcache.lock);
				return b;
			}
		}
	}
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
	if(anchor_table[hashval] == (struct buf*)-1) {
		//	  cprintf("anchor is -1\n");
		anchor_table[hashval] = b;
		if(b->bprev != (struct buf*)-1)
			b->bprev->bnext = b->bnext;
		if(b->bnext != (struct buf*)-1)
			b->bnext->bprev = b->bprev;
		b->bprev = (struct buf*)-1;
		b->bnext = (struct buf*)-1;
	}
	else if(anchor_table[hashval] != b) {
		//	  cprintf("im here to work on %d\n",b);

		//close gaps of node before and after the current node.
		if((b->bprev != (struct buf*)-1) && (b->bnext != (struct buf*)-1)) {
			b->bprev->bnext = b->bnext;
			b->bnext->bprev = b->bprev;
		}
		else if((b->bprev != (struct buf*)-1) && (b->bnext == (struct buf*)-1)) {
			//		  cprintf("%d next is now -1,before it was: %d\n",b->bprev,b->bprev->bnext);
			b->bprev->bnext = (struct buf*)-1;
		}
		//	  else if((b->bprev == (struct buf*)-1) && (b->bnext != (struct buf*)-1)){
		//		  cprintf("IS IT POSSIBLE????\n\n");
		//	  }

		//make next the current anchor (put it first)
		b->bnext = anchor_table[hashval];
		//because it is first, prev is -1.
		b->bprev = (struct buf*)-1;

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


//void *lookup_data(hash_table *hashtable, uint dev, uint sector)
//{
//    uint hashval = hash(hashtable, str);
//    return hash_table.anchor_table[hashval];
//}
//
//int add_data(hash_table_t *hashtable, char *str)
//{
//    list_t *new_list;
//    list_t *current_list;
//    unsigned int hashval = hash(hashtable, str);
//
//    /* Attempt to allocate memory for list */
//    if ((new_list = malloc(sizeof(list_t))) == NULL) return 1;
//
//    /* Does item already exist? */
//    current_list = lookup_string(hashtable, str);
//        /* item already exists, don't insert it again. */
//    if (current_list != NULL) return 2;
//    /* Insert into list */
//    new_list->str = strdup(str);
//    new_list->next = hashtable->table[hashval];
//    hashtable->table[hashval] = new_list;
//
//    return 0;
//}
