/* BEGIN A3 SETUP */
/* This file existed for A1 and A2, but has been completely replaced for A3.
 * We have kept the dumb versions of sys_read and sys_write to support early
 * testing, but they should be replaced with proper implementations that 
 * use your open file table to find the correct vnode given a file descriptor
 * number.  All the "dumb console I/O" code should be deleted.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <copyinout.h>
#include <synch.h>
#include <file.h>

/* This special-case global variable for the console vnode should be deleted 
 * when you have a proper open file table implementation.
 */
struct vnode *cons_vnode=NULL; 

/* This function should be deleted, including the call in main.c, when you
 * have proper initialization of the first 3 file descriptors in your 
 * open file table implementation.
 * You may find it useful as an example of how to get a vnode for the 
 * console device.
 */
void dumb_consoleIO_bootstrap()
{
  int result;
  char path[5];

  /* The path passed to vfs_open must be mutable.
   * vfs_open may modify it.
   */

  strcpy(path, "con:");
  result = vfs_open(path, O_RDWR, 0, &cons_vnode);

  if (result) {
    /* Tough one... if there's no console, there's not
     * much point printing a warning...
     * but maybe the bootstrap was just called in the wrong place
     */
    kprintf("Warning: could not initialize console vnode\n");
    kprintf("User programs will not be able to read/write\n");
    cons_vnode = NULL;
  }
}

/*
 * mk_useruio
 * sets up the uio for a USERSPACE transfer. 
 */
static
void
mk_useruio(struct iovec *iov, struct uio *u, userptr_t buf, 
	   size_t len, off_t offset, enum uio_rw rw)
{

	iov->iov_ubase = buf;
	iov->iov_len = len;
	u->uio_iov = iov;
	u->uio_iovcnt = 1;
	u->uio_offset = offset;
	u->uio_resid = len;
	u->uio_segflg = UIO_USERSPACE;
	u->uio_rw = rw;
	u->uio_space = curthread->t_addrspace;
}

/*
 * sys_open
 * just copies in the filename, then passes work to file_open.
 * You have to write file_open.
 * 
 */
int
sys_open(userptr_t filename, int flags, int mode, int *retval)
{
	char *fname;
	int result;

	if ( (fname = (char *)kmalloc(__PATH_MAX)) == NULL) {
		return ENOMEM;
	}

	result = copyinstr(filename, fname, __PATH_MAX, NULL);
	if (result) {
		kfree(fname);
		return result;
	}

	result =  file_open(fname, flags, mode, retval);
	kfree(fname);
	return result;
}

/* 
 * sys_close
 * You have to write file_close.
 */
int
sys_close(int fd)
{
	return file_close(fd);
}

/* 
 * sys_dup2
 * 
 */
int
sys_dup2(int oldfd, int newfd, int *retval)
{
	/* if either of new or old descriptors are out of range, return EBADF */
	if (oldfd < 0 || oldfd >= __OPEN_MAX || newfd < 0 || newfd >= __OPEN_MAX)
		return EBADF;
	
	/* if new = old, set return value to new */
	if (oldfd == newfd){
		*retval = newfd;
		return 0;
	}
	
	struct fdescript *old;
	struct fdescript *new;
	
	old = (struct fdescript *)kmalloc(sizeof(struct fdescript));
	if (old == NULL)
		return ENOMEM;
	
	lock_acquire(curthread->t_filetable->lock);
	
	/* entry in the table at position[old] */
	old = curthread->t_filetable->fdt[oldfd];
	if (old == NULL){
		lock_release(curthread->t_filetable->lock);
		return EBADF;
	}
	
	new = curthread->t_filetable->fdt[newfd];
	
	/* if it occupied, close it */
	if (new != NULL)
		file_close(newfd);
	
	new = (struct fdescript *)kmalloc(sizeof(struct fdescript));
	if (new == NULL)
		return ENOMEM;
	
	if(!(old->lock))
		old->lock = lock_create("old descripter");
	
	if (!(new->lock))
		new->lock = lock_create("new descripter");
	
	lock_release(curthread->t_filetable->lock);
	*retval = newfd;
	return 0;
}

/*
 * sys_read
 * calls VOP_READ.
 * 
 * A3: This is the "dumb" implementation of sys_write:
 * it only deals with file descriptors 1 and 2, and 
 * assumes they are permanently associated with the 
 * console vnode (which must have been previously initialized).
 *
 * In your implementation, you should use the file descriptor
 * to find a vnode from your file table, and then read from it.
 *
 * Note that any problems with the address supplied by the
 * user as "buf" will be handled by the VOP_READ / uio code
 * so you do not have to try to verify "buf" yourself.
 *
 * Most of this code should be replaced.
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
	struct uio user_uio; //user input/output
	struct iovec user_iov; //user input/output value
	int result;
	int offset = 0;
	struct vnode * dv = NULL; 
	struct fdescript * file = NULL;

	/* better be a valid file descriptor */
	/* Right now, only stdin (0), stdout (1) and stderr (2)
	 * are supported, and they can't be redirected to a file
	 */
	 
	 /* if not in range, return EBADF  */
	if (fd < 0 || fd >== __OPEN_MAX) {
	  return EBADF;
	}
	
	/* if a result from status, return  */
	if ((result = filetable_status(&file, fd)))
		return result;
	
	/* get file */
	dv = file->v;

	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&user_iov, &user_uio, buf, size, file->offset, UIO_READ);

	/* does the read */
	result = VOP_READ(cons_vnode, &user_uio);
	if (result) {
		return result;
	}
	
	file->offset += size;

	/*
	 * The amount read is the size of the buffer originally, minus
	 * how much is left in it.
	 */
	*retval = size - user_uio.uio_resid;

	return 0;
}

/*
 * sys_write
 * calls VOP_WRITE.
 *
 * A3: This is the "dumb" implementation of sys_write:
 * it only deals with file descriptors 1 and 2, and 
 * assumes they are permanently associated with the 
 * console vnode (which must have been previously initialized).
 *
 * In your implementation, you should use the file descriptor
 * to find a vnode from your file table, and then read from it.
 *
 * Note that any problems with the address supplied by the
 * user as "buf" will be handled by the VOP_READ / uio code
 * so you do not have to try to verify "buf" yourself.
 *
 * Most of this code should be replaced.
 */

int
sys_write(int fd, userptr_t buf, size_t len, int *retval) 
{
        struct uio user_uio;
        struct iovec user_iov;
        int result;
        int offset = 0;
		struct vnode * dv = NULL; 
		struct fdescript *file = NULL;
        /* Right now, only stdin (0), stdout (1) and stderr (2)
         * are supported, and they can't be redirected to a file
         */
        if (fd < 0 || fd >= __OPEN_MAX) {
          return EBADF;
        }
		
		if((result = filetable_status(&file, fd)))
			return result;
		dv = file->v;

        /* set up a uio with the buffer, its size, and the current offset */
        mk_useruio(&user_iov, &user_uio, buf, len, offset, UIO_WRITE);

        /* does the write */
        result = VOP_WRITE(cons_vnode, &user_uio);
        if (result) {
                return result;
        }

        /*
         * the amount written is the size of the buffer originally,
         * minus how much is left in it.
         */
        *retval = len - user_uio.uio_resid;

        return 0;
}

/*
 * sys_lseek
 * 
 */
int
sys_lseek(int fd, off_t offset, int whence, off_t *retval)
{
	off_t new_oft;
	int result;
	struct stat t;
	struct fdescript * d = NULL;
	
	if (fd < 0 || fd >= __OPEN_MAX)
		return EBADF;
	
	if((result = filetable_status(&d, fd)))
		return result;
	
	lock_acquire(d->lock);
	if (whence == SEEK_SET)
		new_oft = offset;
	else if (whence == SEEK_CUR)
		new_oft = d->offset + offset;
	else if (whence == SEEK_END){
		result = VOP_STAT(d->v, &t);
		if (result){
			lock_release(d->lock);
			return result;
		}
		new_oft = t.st_size + offset;
	}
	else {
		lock_release(d->lock);
		return EINVAL;
	}
	if (d = NULL)
		return ENOMEM;
	if (new_oft < 0){
		lock_release(d->lock);
		return EINVAL;
	}
	if ((result = VOP_TRYSEEK(d->v, new_oft))){
		lock_release(d->lock);
		return result;
	}
	*retval = new_oft;
	d->offset = new_oft;
	lock_release(d->lock);
	return 0;
}


/* really not "file" calls, per se, but might as well put it here */

/*
 * sys_chdir
 * 
 */
int
sys_chdir(userptr_t path)
{
    (void)path;
	char p[__NAME_MAX];
	int status;
	
	if((status = copyinstr(path, p, __NAME_MAX, NULL)))
		return status;

	return vfs_chdir(p);
}

/*
 * sys___getcwd
 * 
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{
    struct uio user_uio;
	struct iovec user_iov;
	int status;
	
	mk_useruio(&user_iov, &user_uio, buf, buflen, 0 , UIO_READ);
	if((status = vfs_getcwd(&user_uio)))
		return status;
	
	*retval = user_iov.iov_len;

	return 0;
}

/*
 * sys_fstat
 */
int
sys_fstat(int fd, userptr_t statptr)
{
    struct iovec iov;
	struct uio u;
	int status;
	struct fdescript* file;
	struct stat filestat;
	
	if (fd < 0 || fd >= __OPEN_MAX){
		return EBADF;
	}
	if (!(file = curthread->t_filetable->fdt[fd])){
		return EBADF;
		status = VOP_STAT(file->v, &filestat);
		if (status)
			return ENOENT;
	}
	
	copyout(&filestat, statptr, sizeof(struct stat));
	mk_useruio(&iov, &u, statptr, sizeof(struct stat), 0, UIO_READ);
	if((status = uiomove(&filestat, sizeof(struct stat), &u)))
		return status;
	
	return 0;
}

/*
 * sys_getdirentry
 */
int
sys_getdirentry(int fd, userptr_t buf, size_t buflen, int *retval)
{
    if (fd < 0 || fd >= __OPEN_MAX)
		return EBADF;
	
	if (fd > 0 && fd < 3)
		return ENOTDIR;
	
	struct uio user_io;
	struct iovec user_iovec;
	int err, result;
	struct fdescript *d;
	
	if (d == NULL)
		return EBADF;
	
	err = filetable_status(&fdesc, fd);
	if (err)
		return err;
	
	mk_useruio(&user_iovec, &user_io, buf, buflen, d->offset, UIO_READ);
	result = VOP_GETDIRENTRY(d->v, &user_io);
	
	if (result){
		*retval = -1;
		return ENOENT;
	}
	*retval = buflen - user_io.uio_resid;
	return 0;
}

/* END A3 SETUP */




