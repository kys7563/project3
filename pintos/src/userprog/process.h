#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct vm_entry *vm;

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


//PROJECT 2
struct thread *get_child_process(int pid);
void remove_child_process(struct thread *cp);
struct file *process_get_file(int fd);
void process_close_file(int fd);
int process_add_file(struct file *f);
bool handle_mm_fault (struct vm_entry *vm);
bool load_file (void *kaddr, struct vm_entry *vme);
//PJ3
struct mmap_file *find_mmap_file(int mapid);
void do_munmap(struct mmap_file *m_file);
#endif /* userprog/process.h */
