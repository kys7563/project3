#include <stdio.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "vm/page.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "lib/kernel/list.h"

void lru_list_init (void);
void add_page_to_lru_list (struct page *page); 
void del_page_from_lru_list (struct page *page);
void free_page (void *kaddr);
void __free_page (struct page *page);
struct page *alloc_page (enum palloc_flags flags);
void try_to_free_pages (void);		
struct list_elem *get_next_lru_clock(void);		

void lru_list_init (void){
	list_init(&lru_list);
	lock_init(&lru_list_lock);
	lru_clock = NULL;

}

void add_page_to_lru_list (struct page *page){
	
	if(page != NULL){
		lock_acquire(&lru_list_lock);
		list_push_back(&lru_list, &page -> lru);
		lock_release(&lru_list_lock);
	}

}

void del_page_from_lru_list (struct page *page){
	if( page != NULL){
		if(lru_clock == page){
			lru_clock = list_entry(list_remove(&page->lru), struct page, lru);
		}
		else list_remove(&page->lru);
	}
}
     

struct page *alloc_page(enum palloc_flags flags){
	struct page *new_page;
	void *kaddr;
	if((flags&PAL_USER) == 0) return NULL;
	kaddr = palloc_get_page(flags);
	while (kaddr == NULL){
		try_to_free_pages();
		kaddr = palloc_get_page(flags);
	}	
	new_page = malloc(sizeof(struct page));

	if(new_page == NULL){
		palloc_free_page(kaddr);
		return NULL;
	}
	new_page -> kaddr = kaddr;
	new_page -> thread = thread_current();
	add_page_to_lru_list(new_page);
	return new_page;

}

void free_page (void *kaddr){
	struct page *lru_page;
	struct list_elem *e;
	lock_acquire(&lru_list_lock);
	for (e = list_begin(&lru_list); e != list_end(&lru_list); e = list_next(e)){
		lru_page = list_entry(e, struct page, lru);
		if(lru_page->kaddr == kaddr){
			__free_page(lru_page);
			break;
		}
	}
	lock_release(&lru_list_lock);
}

void __free_page (struct page *page){
	palloc_free_page(page->kaddr);
	del_page_from_lru_list(page);
	free(page);
}

struct list_elem *get_next_lru_clock(void){
	struct list_elem *e;
	if (lru_clock == NULL){
		e = list_begin (&lru_list);
		if (e != list_end (&lru_list)){
			lru_clock = list_entry(e, struct page, lru);
			return e;
		}
		else return NULL;
	}
	e = list_next(&lru_clock->lru);

	//list_begin = head, list_end = tail
	if(e == list_end(&lru_list)){
		if (&lru_clock->lru == list_begin(&lru_list))	return NULL;
		else e = list_begin(&lru_list);
	}
	lru_clock = list_entry(e, struct page, lru);
	return e;
	
}

void try_to_free_pages (void){
	struct vm_entry *vme;
	struct page *lru_page;
	struct thread *pg_thread;
	uint32_t *pagedir;
	void *vaddr;
	lock_acquire(&lru_list_lock);
	if(list_empty(&lru_list)) {
		lock_release(&lru_list_lock);
		return;
	}

	while (true){
		struct list_elem *e = get_next_lru_clock();
		if(e == NULL) {
			lock_release(&lru_list_lock);
			return;
		}
		
		lru_page = list_entry(e, struct page, lru);
		
		if(lru_page->vme->pinned == true){
			continue;
		}
		pg_thread = lru_page -> thread;
		pagedir = pg_thread -> pagedir;
		vme = lru_page -> vme;
		vaddr = vme -> vaddr;

		if(pagedir_is_accessed(pagedir, vaddr)){
			pagedir_set_accessed(pagedir, vaddr, false);
			continue;		
		}

		if(pagedir_is_dirty(pagedir, vaddr) || vme->type == VM_ANON){
			if(vme->type == VM_FILE){
				lock_acquire(&filesys_lock);
				file_write_at(vme->file, lru_page->kaddr, vme->read_bytes, vme->offset);
				lock_release(&filesys_lock);
			}
			else{
				vme->type = VM_ANON;
				vme->swap_slot = swap_out(lru_page->kaddr); 
			}
					

		}
		vme->is_loaded = false;
		pagedir_clear_page(pagedir, vaddr);
		__free_page(lru_page);
		break;

	}
	lock_release(&lru_list_lock);
	return;


}







