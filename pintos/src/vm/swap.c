#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"
#include "userprog/syscall.h"
#define BLOCK_PER_PAGE 8


struct lock swap_lock;
struct bitmap *swap_map;
struct block *swap_disk_blocks;

void swap_in (size_t used_index, void *kaddr);
size_t swap_out (void *kaddr);

void swap_init(void) {
	swap_disk_blocks = block_get_role(BLOCK_SWAP);
	
	if(swap_disk_blocks == NULL) return;	
	swap_map = bitmap_create(block_size(swap_disk_blocks) / BLOCK_PER_PAGE);
	
	if (swap_map == NULL) return;
	bitmap_set_all(swap_map, false);
	lock_init(&swap_lock);
}

void swap_in (size_t used_index, void *kaddr){
	lock_acquire(&swap_lock);

	//false means block is free, empty
	if(bitmap_test(swap_map, used_index) == false) return;

	size_t block_index;
	uint8_t *buffer_address;
	for (int i = 0; i < BLOCK_PER_PAGE; i++){
		block_index = used_index * BLOCK_PER_PAGE + i;
		buffer_address = kaddr + i * BLOCK_SECTOR_SIZE;
		block_read(swap_disk_blocks, block_index, buffer_address);
	}
	bitmap_flip(swap_map, used_index);
	lock_release(&swap_lock);

}

size_t swap_out (void *kaddr){
	size_t empty_block_index;
	size_t block_index;
	uint8_t * buffer_address;
	lock_acquire(&swap_lock);
	//find one empty block from 0 index
	empty_block_index = bitmap_scan_and_flip(swap_map, 0, 1, false);
	if(empty_block_index == BITMAP_ERROR) return BITMAP_ERROR;
	for (int i = 0; i < BLOCK_PER_PAGE; i++){
		block_index = empty_block_index * BLOCK_PER_PAGE + i;
		buffer_address = kaddr + i * BLOCK_SECTOR_SIZE;
		block_write(swap_disk_blocks, block_index, buffer_address);
	}
	lock_release(&swap_lock);
	return empty_block_index;
}










