#include <string.h>
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "userprog/syscall.h"

static unsigned vm_hash_func (const struct hash_elem *e, void *UNUSED);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
bool insert_vme (struct hash *vm, struct vm_entry *vme);
bool delete_vme (struct hash *vm, struct vm_entry *vme);
void vm_destroy_func (struct hash_elem *e, void *aux UNUSED);

//swapping



void vm_init(struct hash *vm){
	hash_init(vm,vm_hash_func,vm_less_func, NULL);
}

static unsigned vm_hash_func (const struct hash_elem *e, void *aux UNUSED){
	return (hash_int ((int) hash_entry(e, struct vm_entry, elem) -> vaddr) );
}

static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	struct vm_entry *vm_a = hash_entry(a, struct vm_entry, elem);
	struct vm_entry *vm_b = hash_entry(b, struct vm_entry, elem);
	return (vm_a->vaddr < vm_b->vaddr ? true : false);
}


bool insert_vme (struct hash *vm, struct vm_entry *vme){
	return hash_insert(vm, &(vme->elem)) == NULL ? true : false;
}

bool delete_vme (struct hash *vm, struct vm_entry *vme){
	if(hash_delete(vm,&(vme->elem))!=NULL){
		free(vme);
		return true;
	}
	else{
		free(vme);
		return false;
	}
}


struct vm_entry *find_vme (void *vaddr){
	void *page_num = pg_round_down(vaddr);	
	struct vm_entry vme;
	vme.vaddr = page_num;
	struct hash_elem *he = hash_find(&thread_current()->vm,&vme.elem);
	if(he==NULL) return NULL;
	return hash_entry(he, struct vm_entry, elem);
}

void vm_destroy (struct hash *vm){
	hash_destroy(vm, *vm_destroy_func);


}

//TODO Free the page
void vm_destroy_func (struct hash_elem *e, void *aux UNUSED){
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
	if (vme->is_loaded == true){
		void *physical_address = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
		free_page(physical_address);
		pagedir_clear_page(thread_current()->pagedir, vme->vaddr);

	}
	free(vme);

}

bool load_file(void *kaddr, struct vm_entry *vme){
	
	file_seek(vme->file,vme->offset);
	//printf("load file : %p, rb : %d, of : %d, vme : %p\n",vme->file,vme->read_bytes,vme->offset, vme);
	uint32_t f_read = file_read(vme->file,kaddr,vme->read_bytes);
	//file_seek(vme->file,vme->offset);	
	if(vme->file == NULL) printf("null file\n");
	if(f_read != vme->read_bytes) return false;	
	memset(kaddr+vme->read_bytes,0,vme->zero_bytes);
	return true;
}





