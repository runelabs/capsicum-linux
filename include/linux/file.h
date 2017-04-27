/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/posix_types.h>
#include <linux/err.h>
#include <linux/capsicum.h>

struct file;

extern void fput(struct file *);

struct file_operations;
struct vfsmount;
struct dentry;
struct path;
extern struct file *alloc_file(const struct path *, fmode_t mode,
	const struct file_operations *fop);

static inline void fput_light(struct file *file, int fput_needed)
{
	if (fput_needed)
		fput(file);
}

struct fd {
	struct file *file;
	unsigned int flags;
};
#define FDPUT_FPUT       1
#define FDPUT_POS_UNLOCK 2

static inline void fdput(struct fd fd)
{
	if (fd.flags & FDPUT_FPUT)
		fput(fd.file);
}

/*
 * The base functions for converting a file descriptor to a struct file are:
 *  - fget() always increments refcount, doesn't work on O_PATH files.
 *  - fget_raw() always increments refcount, and does work on O_PATH files.
 *  - fdget() only increments refcount if needed, doesn't work on O_PATH files.
 *  - fdget_raw() only increments refcount if needed, works on O_PATH files.
 *  - fdget_pos() as fdget(), but also locks the file position lock (for
 *    operations that POSIX requires to be atomic w.r.t file position).
 * These functions return NULL on failure, and return the actual entry in the
 * fdtable (which may be a wrapper if the file is a Capsicum capability).
 *
 * These functions should normally only be used when a file is being
 * transferred (e.g. dup(2)) or manipulated as-is; normal users should stick
 * to the fgetr() variants below.
 */
extern struct file *fget(unsigned int fd);
extern struct file *fget_raw(unsigned int fd);
extern unsigned long __fdget(unsigned int fd);
extern unsigned long __fdget_raw(unsigned int fd);
extern unsigned long __fdget_pos(unsigned int fd);
extern void __f_unlock_pos(struct file *);

static inline struct fd __to_fd(unsigned long v)
{
	return (struct fd){(struct file *)(v & ~3),v & 3};
}

static inline struct fd fdget(unsigned int fd)
{
	return __to_fd(__fdget(fd));
}

static inline struct fd fdget_raw(unsigned int fd)
{
	return __to_fd(__fdget_raw(fd));
}

static inline struct fd fdget_pos(int fd)
{
	return __to_fd(__fdget_pos(fd));
}

static inline void fdput_pos(struct fd f)
{
	if (f.flags & FDPUT_POS_UNLOCK)
		__f_unlock_pos(f.file);
	fdput(f);
}

#ifdef CONFIG_SECURITY_CAPSICUM
/*
 * The full unwrapping variant functions are:
 *  - fget_rights()
 *  - fget_raw_rights()
 *  - fdget_rights()
 *  - fdget_raw_rights()
 * These versions have the same behavior as the equivalent base functions, but:
 *  - They also take a struct capsicum_rights argument describing the details
 *    of the operations to be performed on the file.
 *  - They remove any Capsicum capability wrapper for the file, returning the
 *    normal underlying file.
 *  - They return an ERR_PTR on failure (typically with either -EBADF for an
 *    unrecognized FD, or -ENOTCAPABLE for a Capsicum capability FD that does
 *    not have the requisite rights).
 *
 * These functions should normally only be used:
 *  - when the operation being performed on the file requires more detailed
 *    specification (in particular: the ioctl(2) or fcntl(2) command invoked)
 *  - (for fdget_raw_rights()) when a new file descriptor will be created from
 *    this file descriptor, and so should potentially inherit its rights (if
 *    it is a Capsicum capability file descriptor).
 * Otherwise users should stick to the simpler fgetr() variants below.
 */
extern struct file *fget_rights(unsigned int fd,
				const struct capsicum_rights *rights);
extern struct file *fget_raw_rights(unsigned int fd,
				    const struct capsicum_rights *rights);
extern struct fd fdget_rights(unsigned int fd,
			      const struct capsicum_rights *rights);
extern struct fd fdget_raw_rights(unsigned int fd,
				  const struct capsicum_rights *rights);

/*
 * The simple unwrapping variant functions are:
 *  - fgetr()
 *  - fgetr_raw()
 *  - fdgetr()
 *  - fdgetr_raw()
 *  - fdgetr_pos()
 * These versions have the same behavior as the equivalent base functions, but:
 *  - They also take variable arguments indicating the operations to be
 *    performed on the file.
 *  - They remove any Capsicum capability wrapper for the file, returning the
 *    normal underlying file.
 *  - They return an ERR_PTR on failure (typically with either -EBADF for an
 *    unrecognized FD, or -ENOTCAPABLE for a Capsicum capability FD that does
 *    not have the requisite rights).
 *
 * These functions should normally be used for FD->file conversion.
 */
#define fgetr(fd, ...)		_fgetr((fd), __VA_ARGS__, CAP_LIST_END)
#define fgetr_raw(fd, ...)	_fgetr_raw((fd), __VA_ARGS__, CAP_LIST_END)
#define fdgetr(fd, ...)	_fdgetr((fd), __VA_ARGS__, CAP_LIST_END)
#define fdgetr_raw(fd, ...)	_fdgetr_raw((fd), __VA_ARGS__, CAP_LIST_END)
#define fdgetr_pos(fd, ...)	_fdgetr_pos((fd), __VA_ARGS__, CAP_LIST_END)
struct file *_fgetr(unsigned int fd, ...);
struct file *_fgetr_raw(unsigned int fd, ...);
struct fd _fdgetr(unsigned int fd, ...);
struct fd _fdgetr_raw(unsigned int fd, ...);
struct fd _fdgetr_pos(unsigned int fd, ...);

/*
 * Check whether a file, which may be a Capsicum capability wrapper, has a
 * specified set of rights. If it does, return the normal underlying file and
 * (optionally) the actual rights associated with the capability.
 * If update_refcnt is set, then the refcount for the wrapper will be
 * decremented and the refcount for the underlying file incremented.
 */
struct file *file_unwrap(struct file *orig,
			 const struct capsicum_rights *required_rights,
			 const struct capsicum_rights **actual_rights,
			 bool update_refcnt);

#else
/*
 * In a non-Capsicum build, all rights-checking fget() variants fall back to the
 * normal versions (but still return errors as ERR_PTR values not just NULL).
 */
static inline struct file *fget_rights(unsigned int fd,
				       const struct capsicum_rights *rights)
{
	return fget(fd) ?: ERR_PTR(-EBADF);
}
static inline struct file *fget_raw_rights(unsigned int fd,
					   const struct capsicum_rights *rights)
{
	return fget_raw(fd) ?: ERR_PTR(-EBADF);
}
static inline struct fd fdget_rights(unsigned int fd,
				     const struct capsicum_rights *rights)
{
	struct fd f = fdget(fd);

	if (f.file == NULL)
		f.file = ERR_PTR(-EBADF);
	return f;
}
static inline struct fd
fdget_raw_rights(unsigned int fd,
		 const struct capsicum_rights *rights)
{
	struct fd f = fdget_raw(fd);

	if (f.file == NULL)
		f.file = ERR_PTR(-EBADF);
	return f;
}

#define fgetr(fd, ...)		(fget(fd) ?: ERR_PTR(-EBADF))
#define fgetr_raw(fd, ...)	(fget_raw(fd) ?: ERR_PTR(-EBADF))
#define fdgetr(fd, ...)	fdget_rights((fd), NULL)
#define fdgetr_raw(fd, ...)	fdget_raw_rights((fd), NULL)
static inline struct fd fdgetr_pos(int fd, ...)
{
	struct fd f = fdget_pos(fd);

	if (f.file == NULL)
		f.file = ERR_PTR(-EBADF);
	return f;
}

static inline struct file *file_unwrap(struct file *orig,
				const struct capsicum_rights *required_rights,
				const struct capsicum_rights **actual_rights,
				bool update_refcnt)
{
	if (orig == NULL)
		return ERR_PTR(-EBADF);
	return orig;
}
#endif

static inline int map_ebadf_to(const struct file *file, int err)
{
	return (PTR_ERR(file) == -EBADF) ? err : PTR_ERR(file);
}

extern int f_dupfd(unsigned int from, struct file *file, unsigned flags);
extern int replace_fd(unsigned fd, struct file *file, unsigned flags);
extern void set_close_on_exec(unsigned int fd, int flag);
extern bool get_close_on_exec(unsigned int fd);
extern void put_filp(struct file *);
extern int get_unused_fd_flags(unsigned flags);
extern void put_unused_fd(unsigned int fd);

extern void fd_install(unsigned int fd, struct file *file);

extern void flush_delayed_fput(void);
extern void __fput_sync(struct file *);

#endif /* __LINUX_FILE_H */
