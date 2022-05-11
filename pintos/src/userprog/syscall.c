#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <devices/shutdown.h>
#include <filesys/filesys.h>
#include <userprog/process.h>
#include <filesys/file.h>
#include <devices/input.h>
#include <string.h>
#include <threads/vaddr.h>
#include "vm/page.h"
#include <threads/malloc.h>
typedef int mapid_t; 
static void syscall_handler (struct intr_frame *);

//project 2
struct vm_entry *check_address(void *addr, void *esp);
void get_argument(void *esp, int *arg, int count);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
tid_t exec(const char *cmd_line);
int wait(tid_t tid);
int open(const char*file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
void sigaction(int signum, void(*handler)(void));
void sendsig(tid_t pid, int signum);
void sched_yield(void);
void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write);
void check_valid_string (const void *str, void *esp);
void unpin_ptr (void *vaddr);
void unpin_string (void *str);
void unpin_buffer (void *buffer, unsigned size);


int mmap(int fd, void *addr);
void munmap(mapid_t mapid);

 


//project 2
struct vm_entry *check_address(void *addr, void *esp){
	if(addr==NULL||!is_user_vaddr(addr)){
		//printf("A\n");
		exit(-1);
	}
	if((unsigned int *)addr > (unsigned int *) 0xc0000000 || (unsigned int *)addr < (unsigned int *) 0x8048000 ){
		//printf("B\n");
		exit(-1);
	}
	struct vm_entry *vme = find_vme(addr);
	if (vme == NULL){
		//printf("C\n");
		exit(-1);
	}

	return vme;
}

void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write){
	struct vm_entry *vme;
	unsigned i;
	void *buffer_copy;
        buffer_copy = buffer;
	for(i=0; i<size; i++){
		//check_address(buffer_copy, esp);
		vme = check_address(buffer_copy,esp);
		//if(vme==NULL) exit(-1);
		if(to_write==true&&vme->writable==false) exit(-1);
		buffer_copy++;
	}	
}


void check_valid_string (const void *str, void *esp){
	char *str_copy;
	str_copy = (char *)str;
	check_address(str_copy,esp);
	while(str_copy!=NULL){
		str_copy++;
		check_address(str_copy,esp);
	}
}


//put stacked arguments in arg
void get_argument(void *esp, int *arg, int count){
	int i;
	void *sp = esp + 4;
	for(i = 0; i<count; i++){
		//printf("%p\n",sp);
		check_address(sp,esp);
		arg[i] = *(int *)sp;
		sp = sp + 4;
	}
}

void halt(void){
	shutdown_power_off();	
}

void exit(int status){
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, status);
	thread_exit();
}

int wait(tid_t tid){
	return process_wait(tid); //exit status of tid
}

bool create(const char *file, unsigned initial_size){
	bool success = false;
	if(file!= NULL){
		lock_acquire(&filesys_lock);
		success = filesys_create(file, initial_size);
		lock_release(&filesys_lock);
	}
	return success;
}

bool remove(const char *file){
	return filesys_remove(file);
}


tid_t exec(const char *cmd_line){
	tid_t pid = process_execute(cmd_line); //pid : pid of child process 
	struct thread *child = get_child_process((int)pid);
	sema_down(&(child->load_semaphore)); //wait until load by sema_down
	if(child->load_process){
		return pid;
	}
	else return -1;
}

int open(const char *file){
	if(file!=NULL){
		struct file *f = filesys_open(file);

		if(f==NULL) {
			return -1;
		}
		if(strcmp(thread_name(),file)==0) file_deny_write(f);
		int next_fd = process_add_file(f);
		return next_fd;}
	return -1;
}

int filesize(int fd){
	struct file *f = process_get_file(fd);
	if(f==NULL) return -1;
	return file_length(f);
}

int read(int fd, void *buffer, unsigned size){
	lock_acquire(&filesys_lock);
	struct file *f = process_get_file(fd);
	if(fd==0){
		char input_byte;
		input_byte = input_getc();
		unsigned int i=0;
		for(i=0; i<size; i++){
			((char *)buffer)[i] = input_byte;
		}
		lock_release(&filesys_lock);	
		return strlen((char *)buffer);
	}
	else{
		unsigned int f_size =file_read(f,buffer,size);
		lock_release(&filesys_lock);
		return f_size>size? size : f_size;	
	}	

}

int write(int fd, void *buffer, unsigned size){
	lock_acquire(&filesys_lock);
	struct file *f = process_get_file(fd);

	if(fd ==1){
		putbuf((const char *)buffer,size);
		lock_release(&filesys_lock);
		return strlen(buffer);
	}
	else{
		int bw = file_write(f,buffer,size);
		lock_release(&filesys_lock);
		return bw;
	}
}

void seek(int fd, unsigned position){
	struct file *f = process_get_file(fd);
	file_seek(f, position);

}
unsigned tell(int fd){
	struct file *f = process_get_file(fd);
	return file_tell(f);

}
void close(int fd){
	//printf("close system call\n");
	struct file *f;
	struct thread *t = thread_current();
	f = process_get_file(fd);
	if(f != NULL){
		//printf("closing file %p\n",f);
		file_close(f);
		t->fd[fd] = NULL;
	}
}


void sigaction(int signum, void(*handler)(void)){
	struct thread *t = thread_current();
	t->handler[signum] = handler;
//	printf("handler address is %p\n",handler);
}


void sendsig(tid_t pid, int signum){
	struct thread *t;
	t = get_child_process(pid);
	void (*address)(void) =  t->handler[signum];
	if(address==NULL) return;	
	printf("Signum: %d, Action: %p\n",signum,address);
}

void sched_yield(void){
	thread_yield();
}

int mmap(int fd, void* addr){
	struct file *f = process_get_file(fd);
	struct file *f_reopen = file_reopen(f);
	//initializing mmaped file
	if(addr == NULL) return -1;
	if(addr != pg_round_down(addr)) return -1;

	struct mmap_file *m_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
	memset(m_file,0,sizeof(struct mmap_file));
	m_file->file = f_reopen;
	list_init(&m_file->vme_list);
	
	thread_current()->next_mapid++;
	m_file->mapid = thread_current()->next_mapid;	
	
	list_push_back(&thread_current()->mmap_list, &m_file->elem);	
	//vm entry initialziation
	int f_length = file_length(m_file->file);
	int offset = 0;
	while(f_length > 0){
		if(find_vme(addr)!=NULL) return -1;
		struct vm_entry *vme = (struct vm_entry *) malloc(sizeof(struct vm_entry));
		memset(vme,0,sizeof(struct vm_entry));
		
		vme->type = VM_FILE;
		vme->vaddr = addr;
		vme->writable = true;
		vme->file = m_file->file;
		vme->offset= offset;
		
		if(f_length > PGSIZE){ 
			vme->read_bytes = PGSIZE;
			vme->zero_bytes = 0;
		}
		else {
			vme->read_bytes = f_length;
			vme->zero_bytes = PGSIZE - f_length;
		}
		
		list_push_back(&m_file->vme_list,&vme->mmap_elem);
		if(!insert_vme(&thread_current()->vm, vme)) return -1;
		
		addr = addr + PGSIZE;
		offset = offset + PGSIZE;
		f_length = f_length - PGSIZE;
	
	}

	return m_file->mapid;


}
//munmapping given mapid
void munmap(int mapid){
	struct list_elem *e;
	struct thread *t = thread_current();
	//if(!list_empty(&t->mmap_list)){	
		for(e = list_begin(&t->mmap_list);e!=list_end(&t->mmap_list); e= list_next(e)){
			struct mmap_file *m_file = list_entry(e, struct mmap_file, elem);
			if(mapid == CLOSE_ALL || m_file->mapid == mapid){
				//do_munmap: destroy all vme related to m_file
				do_munmap(m_file);
				file_close(m_file->file);
				if(e == list_begin(&t->mmap_list)){
					list_remove(e);
				}
				else{
					struct list_elem *prev = list_prev(e);
					list_remove(e);
					e = prev;}
				free(m_file);
				if (mapid != CLOSE_ALL) break;
			}
	
		}

	
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  //lock_init(&filesys_lock); 
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{

  int arg[3];
  uint32_t *sp = f->esp;
  check_address((void *)sp,f->esp);
 

  int number = *sp;


  switch(number){
  	case SYS_HALT:
		halt();
		return;

	case SYS_EXIT:
	       	get_argument(sp,arg,1);
		exit(arg[0]);
		return;	
	case SYS_EXEC:
		get_argument(sp,arg,1);
		check_valid_string((const char *)arg[0],f->esp);
		f->eax = exec((const char *)arg[0]);
		unpin_string((void *)arg[0]);
		return;
	case SYS_WAIT:
		get_argument(sp,arg,1);
		f->eax = wait((tid_t)arg[0]);
		return;
	case SYS_CREATE:
		get_argument(sp,arg,2);
		if((void *)arg[0] == NULL) exit(-1);
		check_valid_string((const char *)arg[0],f->esp);
		f->eax = create((const char *)arg[0],(unsigned)arg[1]);	
		//printf("create : %d \n", f->eax);
		unpin_string((void *)arg[0]);
		return;
	case SYS_REMOVE:
		get_argument(sp,arg,1);
		check_valid_string((const char *)arg[0],f->esp);
		f->eax = remove((const char *)arg[0]); 
		return;
	case SYS_READ:
		get_argument(sp,arg,3);
		//check_address((void *)arg,sp);
		check_valid_buffer((void *) arg[1], (unsigned) arg[2], f->esp, true);
		f->eax = read((int)arg[0], (void *)arg[1],(unsigned)arg[2]);
		unpin_buffer((void *)arg[1], (unsigned) arg[2]);
		return;
	case SYS_WRITE:
		get_argument(sp,arg,3);
		//check_address((void *)arg,sp);
		check_valid_buffer((void *) arg[1], (unsigned) arg[2], f->esp, false);
		f->eax = write((int)arg[0], (void *)arg[1],(unsigned)arg[2]);
		return;
	case SYS_OPEN:
		//printf("sysopen\n");
		get_argument(sp,arg,1);
		check_valid_string((const char *)arg[0],f->esp);
		f->eax = open((const char *)arg[0]);
		unpin_string((void *)arg[0]);
		return;
	case SYS_CLOSE: 
		//printf("sysclose\n");
		get_argument(sp,arg,1);
		close((int)arg[0]);
		return;
	case SYS_FILESIZE:
		get_argument(sp,arg,1);
		f->eax = filesize((int)arg[0]);
		return;
	case SYS_TELL:
		get_argument(sp,arg,1);
		f->eax = tell((int)arg[0]);
		return;
	
	case SYS_SEEK:
		get_argument(sp,arg,2);
		seek((int)arg[0], (unsigned)arg[1]);
		return;
	case SYS_SIGACTION:
		get_argument(sp,arg,2);
		sigaction((int)arg[0], (void *)arg[1] );
		return;
	case SYS_SENDSIG:
		get_argument(sp,arg,2);
		sendsig((tid_t)arg[0],(int)arg[1]);
		return;
	case SYS_YIELD:
		sched_yield();
		return;
	//pj3
	case SYS_MMAP:
		get_argument(sp,arg,2);
		//check_valid_string((const void *)arg[1],f->esp);
		f->eax = mmap((int)arg[0],(void *)arg[1]);
		return;
	case SYS_MUNMAP:
		get_argument(sp,arg,1);
		munmap((int)arg[0]);
		return;


  }
  unpin_ptr(sp);
  thread_exit ();
}

void unpin_ptr (void *vaddr){
	struct vm_entry *vme = find_vme(vaddr);
	if (vme != NULL){
		vme->pinned = false;
	}
}

void unpin_string (void *str){
	unpin_ptr(str);
	while (*(char *) str +1){
		str = (char *) str +1;
		unpin_ptr(str);
	}
}

void unpin_buffer (void *buffer, unsigned size){
	char *local_buffer = (char *)buffer;
	unsigned i;
	for (i = 0; i < size; i++){
		unpin_ptr(local_buffer);
		local_buffer++;
	}
}


