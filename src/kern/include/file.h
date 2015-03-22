/* BEGIN A3 SETUP */
/*
 * Declarations for file handle and file table management.
 * New for A3.
 */

#ifndef _FILE_H_
#define _FILE_H_

#include <kern/limits.h>

struct vnode;

 /* file descriptor struct */
 struct fdescript{
	int flags; //flag
	off_t offset; // offset of the file
	int ref_count; //reference count
	struct lock* lock; //lock the file 
	struct vnode* v; //reference node
 };
 
 /* filetable struct */
 struct filetable{
	struct fdescript* fdt[__OPEN_MAX]; //array of fd
	struct lock* lock;
 };
 
 
/* these all have an implicit arg of the curthread's filetable */
int filetable_init(struct filetable *ft);
void filetable_destroy(struct filetable *ft);

/* opens a file (must be kernel pointers in the args) */
int file_open(char *filename, int flags, int mode, int *retfd);

/* closes a file */
int file_close(int fd);

/* A3: You should add additional functions that operate on
 * the filetable to help implement some of the filetable-related
 * system calls.
 */

#endif /* _FILE_H_ */

/* END A3 SETUP */
