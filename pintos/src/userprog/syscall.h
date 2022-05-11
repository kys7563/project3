#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
struct lock filesys_lock;
void exit (int status);
void munmap(int mapid);
struct vm_entry *check_address(void *addr, void *esp); 

#endif /* userprog/syscall.h */
