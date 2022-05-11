#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
//project 2
void argument_stack(char **parse, int count, void **esp);
struct thread *get_child_process(int pid);
void remove_child_process(struct thread *cp);

int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);

//pj3
struct mmap_file *find_mmap_file(int mapid);
void do_munmap(struct mmap_file *m_file);


//project 2
void argument_stack(char **parse, int count, void **esp){
	int i,j,k;
	int total_length=0;
	int address[count]; //for storing addresses
	for(i=count-1; i>-1; i--){
		for(j = strlen(parse[i]); j>-1; j--){
			*esp = *esp - 1;
			**(char **)esp = parse[i][j];
		}
		total_length = total_length + strlen(parse[i]) + 1; //including null byte
		address[i] = *(int *)esp;
	}
	if(total_length%4 != 0){
		for(k=0; k < 4-(total_length%4) ; k++){
			*esp = *esp - 1;
			**(uint8_t **)esp = 0;
		}
	
	}
	//address
	*esp = *esp - 4;
	**(char ***)esp = 0;
	for(i=count-1; i>-1 ; i--){
		*esp = *esp - 4;
		**(char ***)esp = (char *)address[i];
	}
	
	*esp = *esp - 4;
	**(char ****)esp = *esp + 4;
	*esp = *esp - 4;
	**(int **)esp = count;
	*esp = *esp - 4;
	**(void ***)esp = 0;
}

//find child process by pid
struct thread *get_child_process(int pid){
	struct list_elem *e;
	struct thread *cur = thread_current();
	for(e = list_begin(&(cur->sibling));e!=list_end(&(cur->sibling));e = list_next(e)){
		struct thread *t = list_entry(e,struct thread, child);
		if(t->tid == pid){
			return t;
		}
	}
	return NULL;
}

void remove_child_process(struct thread *cp){
	list_remove(&(cp->child));
	palloc_free_page(cp);
}

//add a file to file descriptor table
int process_add_file(struct file *f){
	struct thread *t = thread_current();
	int next_fd = t->next_fd;
	t->fd[next_fd] = f;
	/*
	int idx = 0;
	while(t->fd[idx]!=NULL) idx++;
	if(idx > t->next_fd+1) t->next_fd = ;
	else t->next_fd = idx;
	*/
	t->next_fd ++;
	return next_fd;
}

struct file *process_get_file(int fd){
	struct thread *t = thread_current();
	if(t->fd[fd]==NULL){
		return NULL;
	}
	return t->fd[fd];
	
}

void process_close_file(int fd){
	struct thread *t = thread_current();
	struct file *f = process_get_file(fd);
	file_close(f);
	t->fd[fd] = NULL;
}


//pj3

//find mmap_file with given mapid
struct mmap_file* find_mmap_file(int mapid){
	struct list_elem *e;
	struct thread *t = thread_current();
	for(e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e)){
		struct mmap_file *m_file = list_entry(e, struct mmap_file, elem);
		if(m_file->mapid == mapid){
			return m_file;
		}
	}
	return NULL;
}

//unmap given mmap_file 

void do_munmap(struct mmap_file *m_file){
	struct list_elem *e;
	struct thread *t = thread_current();
	//if(!list_empty(&m_file->vme_list)){
		for(e = list_begin(&m_file->vme_list); e != list_end(&m_file->vme_list); e= list_next(e)){
			struct vm_entry *vme = list_entry(e, struct vm_entry, mmap_elem);
			//file is modified in the memory
			
			void *physical_address = pagedir_get_page(t->pagedir, vme->vaddr);
			if(vme->is_loaded==true && pagedir_is_dirty(t->pagedir,vme->vaddr)){
				lock_acquire(&filesys_lock);
				file_write_at(vme->file,vme->vaddr,vme->read_bytes,vme->offset);
				lock_release(&filesys_lock);
				//free_page_vaddr(vme->vaddr);
		
			}
			pagedir_clear_page(t->pagedir, vme->vaddr);
			free_page(physical_address);

			vme->is_loaded = false;
			struct list_elem *prev = list_prev(e);
			list_remove(e);
			e = prev;
			delete_vme(&t->vm,vme);
		}
		list_remove(&m_file->elem);
	//}
}


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  char t_name[128];
  int index = 0;
  while(file_name[index]!=' '&&file_name[index]!='\0'){
  	t_name[index] = file_name[index];
  	index++;
  }
  t_name[index] = '\0';
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (t_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  //project 2

  char *parse[100];
  int count = 0;
  char *save_ptr;
  parse[0] = strtok_r(file_name, " ", &save_ptr);
  while(parse[count]!=NULL){
  	count++;
 	parse[count] = strtok_r(NULL, " ", &save_ptr);
  }


  vm_init(&(thread_current()->vm));
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (parse[0], &if_.eip, &if_.esp);
  //printf("success is%d",success);
  //finished loading
  sema_up(&(thread_current()->load_semaphore));

  //whether loading is successful or not
  thread_current()->load_process = success;
  if(success){
  	argument_stack(parse,count,&if_.esp);
  }
  //hex_dump(if_.esp,if_.esp,PHYS_BASE-if_.esp,true);
  
  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *child;
  child = get_child_process(child_tid);
  
  if(child==NULL) return -1;
  sema_down(&(child->exit_semaphore));
  int exit_status = child->exit_status;
  remove_child_process(child);

  return exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  int i = cur->next_fd - 1;
  /*close fdt except for STDIN,STDOUT
  while(i!=1){
  	process_close_file(i);
	//cur->fd[i] = NULL;
  	i--;
  }*/

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
 // printf("reach p_exit\n");

  // munmap when exiting
  /*int mapid;
  for(mapid = 1; mapid < cur->next_mapid; mapid++){
  	struct mmap_file *m_file = find_mmap_file(mapid);
	if(m_file!=NULL){
		do_munmap(m_file);
	}
  }*/
  munmap(CLOSE_ALL);

  vm_destroy(&(cur -> vm));

  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
       * cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  //file_close(cur->running_file);
  while(i!=1){
  	process_close_file(i);
	i--;
  }
  

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  lock_acquire(&filesys_lock);



  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      lock_release(&filesys_lock);
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  t->running_file = file;
  file_deny_write(file);
  lock_release(&filesys_lock);
  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{

  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

 
  //printf("load segment\n");
  struct file *reopen_file = file_reopen(file);
  file_seek (reopen_file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
	
	
	
      struct vm_entry *vme = (struct vm_entry *) malloc(sizeof(struct vm_entry));
      memset(vme, 0, sizeof(struct vm_entry));
      vme->type = VM_BIN;
      vme->vaddr =  upage;
      vme->writable = writable;
      vme->file = reopen_file;
      vme->offset = ofs;
      vme->read_bytes = page_read_bytes;
      vme->zero_bytes = page_zero_bytes;
      vme->pinned = false;
      insert_vme(&(thread_current())->vm, vme);

      //printf("load segment vme file is %p\n",vme->file);
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  //printf("fileread : %d\n",file_read(file,buffer,4096));
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  struct page *kpage;
  bool success = false;

  //printf("stack setup\n");

  kpage = alloc_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage->kaddr, true);
      if (success)
        *esp = PHYS_BASE;
      else {
        free_page (kpage->kaddr);
	return success;
	}
  
  	struct vm_entry *vme = (struct vm_entry *)malloc(sizeof(struct vm_entry));
	void *vme_vaddr = (uint8_t *)PHYS_BASE - PGSIZE;
	memset(vme,0,sizeof(struct vm_entry));
	kpage->vme = vme;
	vme ->type = VM_ANON;
	vme->vaddr = pg_round_down(vme_vaddr);
	vme->writable=true;
	vme->is_loaded = true;
	vme->pinned =true;
    	if(insert_vme(&thread_current()->vm, vme)) success = true;
    	//printf("vme file in setupstack %p\n",vme->file);

    }
   return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


bool handle_mm_fault (struct vm_entry *vme){
	struct page *phys_page = alloc_page(PAL_USER);
	phys_page -> vme = vme;
	uint8_t type = vme -> type;
	vme -> pinned = true;
	if(vme->is_loaded == true){
		free_page(phys_page);

		return false;
	}
	switch (type){
		case VM_BIN:
			if (!load_file(phys_page->kaddr,vme)){
				//free_page(phys_page->kaddr);
				//printf("load failed\n");
				return false;
			}
			break;
		case VM_FILE:
			if (!load_file(phys_page->kaddr,vme)){
				free_page(phys_page->kaddr);
				return false;
			}
			break;
		case VM_ANON:
			//printf("here it is\n");
			swap_in(vme->swap_slot, phys_page->kaddr);
			break;

	}
	if(!install_page(vme->vaddr,phys_page->kaddr,vme->writable)){
		free_page(phys_page->kaddr);
		printf("install failed\n");
		return false;
	}
	vme->is_loaded =true;
	return true;
}


