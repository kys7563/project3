#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "vm/page.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

void lru_list_init(void);
void add_page_to_lru_list (struct page *page);
void del_page_from_lru_list (struct page *page);
struct page *alloc_page (enum palloc_flags flags);
void free_page (void *kaddr);
void __free_page (struct page *page);
void try_to_free_pages (void);


#endif

