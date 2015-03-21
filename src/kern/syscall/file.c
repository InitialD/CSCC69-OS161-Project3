/* BEGIN A3 SETUP */
/*
 * File handles and file tables.
 * New for ASST3
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <file.h>
#include <syscall.h>

/* new include files */
#include <vfs.h>
#include <vnode.h>
#include <copyinout.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <current.h>
#include <lib.h>

/*** openfile functions ***/

/*
 * file_open
 * opens a file, places it in the filetable, sets RETFD to the file
 * descriptor. the pointer arguments must be kernel pointers.
 * NOTE -- the passed in filename must be a mutable string.
 * 
 * A3: As per the OS/161 man page for open(), you do not need 
 * to do anything with the "mode" argument.
 */
int
file_open(char *filename, int flags, int mode, int *retfd)
{
	/*(void)filename;
	(void)flags;
	(void)retfd;
	(void)mode;*/
	
	int status; //status
	struct vnode *v; //reference node for vfs
	struct fdescript *file; // created in file.h 
	
	/* is a valid filename */
	if (filename == NULL)
		return ENOENT;
	
	/* Can the file be opened?, return only if open fails*/
	if ((status = vfs_open(filename, flags, mode, &v)))
		return status;
	
	/* create a filetable entry, return out of memory if can't */
	if(!(file = kmalloc(sizeof(struct fdescript))))
		vfs_close(v);
		return ENOMEM;
	
	/* Lock the fdescript, return EUNIMP */
	if (!(file->lock = lock_create("Fdiscriptor lock")))
		vfs_close(v);
		kfree(file); //clear the allocation of memory
		return EUNIMP;
		
	/* attach values to the file fields */
	file->flags = flags;
	file->offset = 0;
	file->ref_count = 0;
	file->v = v;
	
	/* Inject into kernel*/
	KASSERT(curthread->t_filetable != NULL);
	if((status = filetable_inject(file, retfd)))
		lock_destroy(file->lock);
		vfs_close(v);
		kfree(file);
		return status;
	
	return 0;
}


/* 
 * file_close
 * Called when a process closes a file descriptor.  Think about how you plan
 * to handle fork, and what (if anything) is shared between parent/child after
 * fork.  Your design decisions will affect what you should do for close.
 */
int
file_close(int fd)
{
    /*(void)fd;*/
	struct fdescript *file;
	int status;
	
	/* retrieve the file */
	if ((status = filetable_status(&file, fd)))
		return status;
	
	/* acquire lock so no others are pointing at it */
	lock_acquire(file->lock);
	
	/* alter the reference counter*/
	file->ref_count = ref_count - 1;
	
	/* if none are referencing*/
	if (file->ref_count <= 0)
		vfs_close(file->v); //close the vnode
		/* release and destroy the lock */
		lock_release(file->lock);
		lock_destroy(file->lock);
		kfree(file); //free memory
	else
		lock_release(file->lock);
	
	/* adjust the filetable entry, fd, with NULL */
	lock_acquire(curthread->t_filetable->lock);
	curthread-t_filetable->fdt[fd] = NULL;
	lock_release(curthread-t_filetable->lock);

	return 0;
}

/*** filetable functions ***/

/* 
 * filetable_init
 * pretty straightforward -- allocate the space, set up 
 * first 3 file descriptors for stdin, stdout and stderr,
 * and initialize all other entries to NULL.
 * 
 * Should set curthread->t_filetable to point to the
 * newly-initialized filetable.
 * 
 * Should return non-zero error code on failure.  Currently
 * does nothing but returns success so that loading a user
 * program will succeed even if you haven't written the
 * filetable initialization yet.
 */

int
filetable_init(void)
{
	return 0;
}	

/*
 * filetable_destroy
 * closes the files in the file table, frees the table.
 * This should be called as part of cleaning up a process (after kill
 * or exit).
 */
void
filetable_destroy(struct filetable *ft)
{
        (void)ft;
}	


/* 
 * You should add additional filetable utility functions here as needed
 * to support the system calls.  For example, given a file descriptor
 * you will want some sort of lookup function that will check if the fd is 
 * valid and return the associated vnode (and possibly other information like
 * the current file position) associated with that open file.
 */

 /* inject into table*/
 int filetable_inject(struct fdescript *file, int fd)
	return 0;
 
 int filetable_status(struct fdescript **file, int fd)
	return 0;

/* END A3 SETUP */
