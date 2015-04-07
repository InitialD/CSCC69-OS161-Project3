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
filetable_init(struct filetable *ft)
{
	int flag, err;
	struct fdescript fdep[3];
	struct vnode *v;
	
	/* Lock the table */
	ft->lock = lock_create("filetable");
	for(int i = 0; i < 3; i++)
		/* allocate memory for node and descriptor */
		fdep[i] = (struct fdescript *)kmalloc(sizeof(struct fdescript));
		v = (struct vnode *)kmalloc(sizeof(struct vnode));
		
		/* if NULL destroy filetable */
		if (v == NULL)
			return ENOMEM;
		if (fdep[i] == NULL)
			filetable_destroy(ft);
			return ENOMEM;
		
		/* set flags */
		if (i == 0)
			flag = O_RDONLY;
		else
			flag = O_WRONLY;
		
		/* copy the path */
		strcpy(path, "con:");
		
		/*open the path*/
		if((err = vfs_open(path, flag, mode, &v)))
			vfs_close(v);
			filetable_destroy(ft);
			return err;
		
		/* set the fileds of the descriptor*/
		fdesc[i]->v = v;
		fdesc[i]->flags = flag;
		fdesc[i]->offset = 0;
		fdesc[i]->ref_count = 0;
		ft->fdt[i] = fdep[i];
	
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
    //(void)ft;
	/* if null, panic*/
	if (ft == NULL)
		panic("Table is Null");
	/* for each entry of the table, close it and assign it null */
	for (int i = 0; i < __OPEN_MAX; i++) {
		if (ft->fdt[i]) {
			file_close(i);
			ft->fdt[i] = NULL;
		}
	}
	/* free the table */
	kfree(ft);
}	


/* 
 * You should add additional filetable utility functions here as needed
 * to support the system calls.  For example, given a file descriptor
 * you will want some sort of lookup function that will check if the fd is 
 * valid and return the associated vnode (and possibly other information like
 * the current file position) associated with that open file.
 */

 /* inject into table*/
 int filetable_inject(struct fdescript *file, int fd){
	 struct filetable *ft = curthread->t_filetable;
	// get lock
	KASSERT(curthread->t_filetable != NULL);
	lock_acquire(ft->lock);
	/* Look through the filetable for the first opening */
	for (int i = 0; i < __OPEN_MAX; i++)
		if (ft->fdt[i] == NULL) {
			lock_acquire(file->lock);
			file->ref_count++;
			lock_release(file->lock);
			ft->fdt[i] = file;
			if (fd)
				*fd = i;
			lock_release(ft->lock);
			return 0;
		}
	lock_release(ft->lock);
	return EMFILE;
 }
 
 int filetable_status(struct fdescript **file, int fd){
	/* check file descriptor */
	lock_acquire(curthread->t_filetable->lock);
	
	/* if descriptor isn't in range */
	if (fd < 0 || fd >= __OPEN_MAX) {
		lock_release(curthread->t_filetable->lock);
		return EBADF;
	}
	
	/* If that entry is NULL */
	else if (curthread->t_filetable->fdt[fd] == NULL) {
		lock_release(curthread->t_filetable->lock);
		return ENOENT;
	}
	
	/* otherwise, retrieve the status */
	*file = curthread->t_filetable->fdt[fd];
	lock_release(curthread->t_filetable->lock);
	
	return 0;
 }

/* END A3 SETUP */
