/* 
   Unix SMB/CIFS implementation.
   Samba system utilities
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Jeremy Allison  1998-2005
   Copyright (C) Timur Bakeyev        2005
   Copyright (C) Bjoern Jacke    2006-2007

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "system/syslog.h"
#include "system/capability.h"
#include "system/passwd.h"
#include "system/filesys.h"
#include "../lib/util/setid.h"

#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

/*
   The idea is that this file will eventually have wrappers around all
   important system calls in samba. The aims are:

   - to enable easier porting by putting OS dependent stuff in here

   - to allow for hooks into other "pseudo-filesystems"

   - to allow easier integration of things like the japanese extensions

   - to support the philosophy of Samba to expose the features of
     the OS within the SMB model. In general whatever file/printer/variable
     expansions/etc make sense to the OS should be acceptable to Samba.
*/



/*******************************************************************
A read wrapper that will deal with EINTR.
********************************************************************/

ssize_t sys_read(int fd, void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = read(fd, buf, count);
#if defined(EWOULDBLOCK)
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
#else
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
#endif
	return ret;
}

/*******************************************************************
A write wrapper that will deal with EINTR.
********************************************************************/

ssize_t sys_write(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
#if defined(EWOULDBLOCK)
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
#else
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
#endif
	return ret;
}

/*******************************************************************
A writev wrapper that will deal with EINTR.
********************************************************************/

ssize_t sys_writev(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t ret;

#if 0
	/* Try to confuse write_data_iov a bit */
	if ((random() % 5) == 0) {
		return sys_write(fd, iov[0].iov_base, iov[0].iov_len);
	}
	if (iov[0].iov_len > 1) {
		return sys_write(fd, iov[0].iov_base,
				 (random() % (iov[0].iov_len-1)) + 1);
	}
#endif

	do {
		ret = writev(fd, iov, iovcnt);
#if defined(EWOULDBLOCK)
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
#else
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
#endif
	return ret;
}

/*******************************************************************
A pread wrapper that will deal with EINTR
********************************************************************/

#if defined(HAVE_PREAD)
ssize_t sys_pread(int fd, void *buf, size_t count, off_t off)
{
	ssize_t ret;

	do {
		ret = pread(fd, buf, count, off);
	} while (ret == -1 && errno == EINTR);
	return ret;
}
#endif

/*******************************************************************
A write wrapper that will deal with EINTR
********************************************************************/

#if defined(HAVE_PWRITE)
ssize_t sys_pwrite(int fd, const void *buf, size_t count, off_t off)
{
	ssize_t ret;

	do {
		ret = pwrite(fd, buf, count, off);
	} while (ret == -1 && errno == EINTR);
	return ret;
}
#endif

/*******************************************************************
A send wrapper that will deal with EINTR or EAGAIN or EWOULDBLOCK.
********************************************************************/

ssize_t sys_send(int s, const void *msg, size_t len, int flags)
{
	ssize_t ret;

	do {
		ret = send(s, msg, len, flags);
#if defined(EWOULDBLOCK)
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
#else
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
#endif
	return ret;
}

/*******************************************************************
A recvfrom wrapper that will deal with EINTR.
NB. As used with non-blocking sockets, return on EAGAIN/EWOULDBLOCK
********************************************************************/

ssize_t sys_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
	ssize_t ret;

	do {
		ret = recvfrom(s, buf, len, flags, from, fromlen);
	} while (ret == -1 && (errno == EINTR));
	return ret;
}

/*******************************************************************
A fcntl wrapper that will deal with EINTR.
********************************************************************/

int sys_fcntl_ptr(int fd, int cmd, void *arg)
{
	int ret;

	do {
		ret = fcntl(fd, cmd, arg);
	} while (ret == -1 && errno == EINTR);
	return ret;
}

/*******************************************************************
A fcntl wrapper that will deal with EINTR.
********************************************************************/

int sys_fcntl_long(int fd, int cmd, long arg)
{
	int ret;

	do {
		ret = fcntl(fd, cmd, arg);
	} while (ret == -1 && errno == EINTR);
	return ret;
}

/****************************************************************************
 Get/Set all the possible time fields from a stat struct as a timespec.
****************************************************************************/

static struct timespec get_atimespec(const struct stat *pst)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	struct timespec ret;

	/* Old system - no ns timestamp. */
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = 0;
	return ret;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_atim.tv_sec;
	ret.tv_nsec = pst->st_atim.tv_nsec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = pst->st_atimensec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	struct timespec ret;
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = pst->st_atime_n;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	struct timespec ret;
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = pst->st_uatime * 1000;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	return pst->st_atimespec;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT
#endif
#endif
}

static struct timespec get_mtimespec(const struct stat *pst)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	struct timespec ret;

	/* Old system - no ns timestamp. */
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = 0;
	return ret;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_mtim.tv_sec;
	ret.tv_nsec = pst->st_mtim.tv_nsec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = pst->st_mtimensec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	struct timespec ret;
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = pst->st_mtime_n;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	struct timespec ret;
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = pst->st_umtime * 1000;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	return pst->st_mtimespec;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT
#endif
#endif
}

static struct timespec get_ctimespec(const struct stat *pst)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	struct timespec ret;

	/* Old system - no ns timestamp. */
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = 0;
	return ret;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_ctim.tv_sec;
	ret.tv_nsec = pst->st_ctim.tv_nsec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = pst->st_ctimensec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	struct timespec ret;
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = pst->st_ctime_n;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	struct timespec ret;
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = pst->st_uctime * 1000;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	return pst->st_ctimespec;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT
#endif
#endif
}

/****************************************************************************
 Return the best approximation to a 'create time' under UNIX from a stat
 structure.
****************************************************************************/

static struct timespec calc_create_time_stat(const struct stat *st)
{
	struct timespec ret, ret1;
	struct timespec c_time = get_ctimespec(st);
	struct timespec m_time = get_mtimespec(st);
	struct timespec a_time = get_atimespec(st);

	ret = timespec_compare(&c_time, &m_time) < 0 ? c_time : m_time;
	ret1 = timespec_compare(&ret, &a_time) < 0 ? ret : a_time;

	if(!null_timespec(ret1)) {
		return ret1;
	}

	/*
	 * One of ctime, mtime or atime was zero (probably atime).
	 * Just return MIN(ctime, mtime).
	 */
	return ret;
}

/****************************************************************************
 Return the best approximation to a 'create time' under UNIX from a stat_ex
 structure.
****************************************************************************/

static struct timespec calc_create_time_stat_ex(const struct stat_ex *st)
{
	struct timespec ret, ret1;
	struct timespec c_time = st->st_ex_ctime;
	struct timespec m_time = st->st_ex_mtime;
	struct timespec a_time = st->st_ex_atime;

	ret = timespec_compare(&c_time, &m_time) < 0 ? c_time : m_time;
	ret1 = timespec_compare(&ret, &a_time) < 0 ? ret : a_time;

	if(!null_timespec(ret1)) {
		return ret1;
	}

	/*
	 * One of ctime, mtime or atime was zero (probably atime).
	 * Just return MIN(ctime, mtime).
	 */
	return ret;
}

/****************************************************************************
 Return the 'create time' from a stat struct if it exists (birthtime) or else
 use the best approximation.
****************************************************************************/

static void make_create_timespec(const struct stat *pst, struct stat_ex *dst,
				 bool fake_dir_create_times)
{
	if (S_ISDIR(pst->st_mode) && fake_dir_create_times) {
		dst->st_ex_btime.tv_sec = 315493200L;          /* 1/1/1980 */
		dst->st_ex_btime.tv_nsec = 0;
	}

	dst->st_ex_calculated_birthtime = false;

#if defined(HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC_TV_NSEC)
	dst->st_ex_btime = pst->st_birthtimespec;
#elif defined(HAVE_STRUCT_STAT_ST_BIRTHTIMENSEC)
	dst->st_ex_btime.tv_sec = pst->st_birthtime;
	dst->st_ex_btime.tv_nsec = pst->st_birthtimenspec;
#elif defined(HAVE_STRUCT_STAT_ST_BIRTHTIME)
	dst->st_ex_btime.tv_sec = pst->st_birthtime;
	dst->st_ex_btime.tv_nsec = 0;
#else
	dst->st_ex_btime = calc_create_time_stat(pst);
	dst->st_ex_calculated_birthtime = true;
#endif

	/* Deal with systems that don't initialize birthtime correctly.
	 * Pointed out by SATOH Fumiyasu <fumiyas@osstech.jp>.
	 */
	if (null_timespec(dst->st_ex_btime)) {
		dst->st_ex_btime = calc_create_time_stat(pst);
		dst->st_ex_calculated_birthtime = true;
	}
}

/****************************************************************************
 If we update a timestamp in a stat_ex struct we may have to recalculate
 the birthtime. For now only implement this for write time, but we may
 also need to do it for atime and ctime. JRA.
****************************************************************************/

void update_stat_ex_mtime(struct stat_ex *dst,
				struct timespec write_ts)
{
	dst->st_ex_mtime = write_ts;

	/* We may have to recalculate btime. */
	if (dst->st_ex_calculated_birthtime) {
		dst->st_ex_btime = calc_create_time_stat_ex(dst);
	}
}

void update_stat_ex_create_time(struct stat_ex *dst,
                                struct timespec create_time)
{
	dst->st_ex_btime = create_time;
	dst->st_ex_calculated_birthtime = false;
}

void init_stat_ex_from_stat (struct stat_ex *dst,
			    const struct stat *src,
			    bool fake_dir_create_times)
{
	dst->st_ex_dev = src->st_dev;
	dst->st_ex_ino = src->st_ino;
	dst->st_ex_mode = src->st_mode;
	dst->st_ex_nlink = src->st_nlink;
	dst->st_ex_uid = src->st_uid;
	dst->st_ex_gid = src->st_gid;
	dst->st_ex_rdev = src->st_rdev;
	dst->st_ex_size = src->st_size;
	dst->st_ex_atime = get_atimespec(src);
	dst->st_ex_mtime = get_mtimespec(src);
	dst->st_ex_ctime = get_ctimespec(src);
	make_create_timespec(src, dst, fake_dir_create_times);
#ifdef HAVE_STAT_ST_BLKSIZE
	dst->st_ex_blksize = src->st_blksize;
#else
	dst->st_ex_blksize = STAT_ST_BLOCKSIZE;
#endif

#ifdef HAVE_STAT_ST_BLOCKS
	dst->st_ex_blocks = src->st_blocks;
#else
	dst->st_ex_blocks = src->st_size / dst->st_ex_blksize + 1;
#endif

#ifdef HAVE_STAT_ST_FLAGS
	dst->st_ex_flags = src->st_flags;
#else
	dst->st_ex_flags = 0;
#endif
}

/*******************************************************************
A stat() wrapper.
********************************************************************/

int sys_stat(const char *fname, SMB_STRUCT_STAT *sbuf,
	     bool fake_dir_create_times)
{
	int ret;
	struct stat statbuf;
	ret = stat(fname, &statbuf);
	if (ret == 0) {
		/* we always want directories to appear zero size */
		if (S_ISDIR(statbuf.st_mode)) {
			statbuf.st_size = 0;
		}
		init_stat_ex_from_stat(sbuf, &statbuf, fake_dir_create_times);
	}
	return ret;
}

/*******************************************************************
 An fstat() wrapper.
********************************************************************/

int sys_fstat(int fd, SMB_STRUCT_STAT *sbuf, bool fake_dir_create_times)
{
	int ret;
	struct stat statbuf;
	ret = fstat(fd, &statbuf);
	if (ret == 0) {
		/* we always want directories to appear zero size */
		if (S_ISDIR(statbuf.st_mode)) {
			statbuf.st_size = 0;
		}
		init_stat_ex_from_stat(sbuf, &statbuf, fake_dir_create_times);
	}
	return ret;
}

/*******************************************************************
 An lstat() wrapper.
********************************************************************/

int sys_lstat(const char *fname,SMB_STRUCT_STAT *sbuf,
	      bool fake_dir_create_times)
{
	int ret;
	struct stat statbuf;
	ret = lstat(fname, &statbuf);
	if (ret == 0) {
		/* we always want directories to appear zero size */
		if (S_ISDIR(statbuf.st_mode)) {
			statbuf.st_size = 0;
		}
		init_stat_ex_from_stat(sbuf, &statbuf, fake_dir_create_times);
	}
	return ret;
}

/*******************************************************************
 An posix_fallocate() wrapper.
********************************************************************/
int sys_posix_fallocate(int fd, off_t offset, off_t len)
{
#if defined(HAVE_POSIX_FALLOCATE) && !defined(HAVE_BROKEN_POSIX_FALLOCATE)
	return posix_fallocate(fd, offset, len);
#elif defined(F_RESVSP64)
	/* this handles XFS on IRIX */
	struct flock64 fl;
	off_t new_len = offset + len;
	int ret;
	struct stat64 sbuf;

	/* unlikely to get a too large file on a 64bit system but ... */
	if (new_len < 0)
		return EFBIG;

	fl.l_whence = SEEK_SET;
	fl.l_start = offset;
	fl.l_len = len;

	ret=fcntl(fd, F_RESVSP64, &fl);

	if (ret != 0)
		return errno;

	/* Make sure the file gets enlarged after we allocated space: */
	fstat64(fd, &sbuf);
	if (new_len > sbuf.st_size)
		ftruncate64(fd, new_len);
	return 0;
#else
	return ENOSYS;
#endif
}

/*******************************************************************
 An fallocate() function that matches the semantics of the Linux one.
********************************************************************/

#ifdef HAVE_LINUX_FALLOC_H
#include <linux/falloc.h>
#endif

int sys_fallocate(int fd, enum vfs_fallocate_mode mode, off_t offset, off_t len)
{
#if defined(HAVE_LINUX_FALLOCATE64) || defined(HAVE_LINUX_FALLOCATE)
	int lmode;
	switch (mode) {
	case VFS_FALLOCATE_EXTEND_SIZE:
		lmode = 0;
		break;
	case VFS_FALLOCATE_KEEP_SIZE:
		lmode = FALLOC_FL_KEEP_SIZE;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
#if defined(HAVE_LINUX_FALLOCATE)
	return fallocate(fd, lmode, offset, len);
#endif
#else
	/* TODO - plumb in fallocate from other filesysetms like VXFS etc. JRA. */
	errno = ENOSYS;
	return -1;
#endif
}

#if HAVE_KERNEL_SHARE_MODES
#ifndef LOCK_MAND
#define LOCK_MAND	32	/* This is a mandatory flock */
#define LOCK_READ	64	/* ... Which allows concurrent read operations */
#define LOCK_WRITE	128	/* ... Which allows concurrent write operations */
#define LOCK_RW		192	/* ... Which allows concurrent read & write ops */
#endif
#endif

/*******************************************************************
 A flock() wrapper that will perform the kernel flock.
********************************************************************/

void kernel_flock(int fd, uint32 share_mode, uint32 access_mask)
{
#if HAVE_KERNEL_SHARE_MODES
	int kernel_mode = 0;
	if (share_mode == FILE_SHARE_WRITE) {
		kernel_mode = LOCK_MAND|LOCK_WRITE;
	} else if (share_mode == FILE_SHARE_READ) {
		kernel_mode = LOCK_MAND|LOCK_READ;
	} else if (share_mode == FILE_SHARE_NONE) {
		kernel_mode = LOCK_MAND;
	}
	if (kernel_mode) {
		flock(fd, kernel_mode);
	}
#endif
	;
}



/*******************************************************************
 An fdopendir wrapper.
********************************************************************/

DIR *sys_fdopendir(int fd)
{
#if defined(HAVE_FDOPENDIR)
	return fdopendir(fd);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

/*******************************************************************
 An mknod() wrapper.
********************************************************************/

int sys_mknod(const char *path, mode_t mode, SMB_DEV_T dev)
{
#if defined(HAVE_MKNOD)
	return mknod(path, mode, dev);
#else
	/* No mknod system call. */
	errno = ENOSYS;
	return -1;
#endif
}

/*******************************************************************
The wait() calls vary between systems
********************************************************************/

int sys_waitpid(pid_t pid,int *status,int options)
{
#ifdef HAVE_WAITPID
	return waitpid(pid,status,options);
#else /* HAVE_WAITPID */
	return wait4(pid, status, options, NULL);
#endif /* HAVE_WAITPID */
}

/*******************************************************************
 System wrapper for getwd. Always returns MALLOC'ed memory, or NULL
 on error (malloc fail usually).
********************************************************************/

char *sys_getwd(void)
{
#ifdef GETCWD_TAKES_NULL
	return getcwd(NULL, 0);
#elif HAVE_GETCWD
	char *wd = NULL, *s = NULL;
	size_t allocated = PATH_MAX;

	while (1) {
		s = SMB_REALLOC_ARRAY(s, char, allocated);
		if (s == NULL) {
			return NULL;
		}
		wd = getcwd(s, allocated);
		if (wd) {
			break;
		}
		if (errno != ERANGE) {
			SAFE_FREE(s);
			break;
		}
		allocated *= 2;
		if (allocated < PATH_MAX) {
			SAFE_FREE(s);
			break;
		}
	}
	return wd;
#else
	char *s = SMB_MALLOC_ARRAY(char, PATH_MAX);
	if (s == NULL) {
		return NULL;
	}
	return getwd(s);
#endif
}

#if defined(HAVE_POSIX_CAPABILITIES)

/**************************************************************************
 Try and abstract process capabilities (for systems that have them).
****************************************************************************/

/* Set the POSIX capabilities needed for the given purpose into the effective
 * capability set of the current process. Make sure they are always removed
 * from the inheritable set, because there is no circumstance in which our
 * children should inherit our elevated privileges.
 */
static bool set_process_capability(enum smbd_capability capability,
				   bool enable)
{
	cap_value_t cap_vals[2] = {0};
	int num_cap_vals = 0;

	cap_t cap;

#if defined(HAVE_PRCTL) && defined(PR_GET_KEEPCAPS) && defined(PR_SET_KEEPCAPS)
	/* On Linux, make sure that any capabilities we grab are sticky
	 * across UID changes. We expect that this would allow us to keep both
	 * the effective and permitted capability sets, but as of circa 2.6.16,
	 * only the permitted set is kept. It is a bug (which we work around)
	 * that the effective set is lost, but we still require the effective
	 * set to be kept.
	 */
	if (!prctl(PR_GET_KEEPCAPS)) {
		prctl(PR_SET_KEEPCAPS, 1);
	}
#endif

	cap = cap_get_proc();
	if (cap == NULL) {
		DEBUG(0,("set_process_capability: cap_get_proc failed: %s\n",
			strerror(errno)));
		return False;
	}

	switch (capability) {
		case KERNEL_OPLOCK_CAPABILITY:
#ifdef CAP_NETWORK_MGT
			/* IRIX has CAP_NETWORK_MGT for oplocks. */
			cap_vals[num_cap_vals++] = CAP_NETWORK_MGT;
#endif
			break;
		case DMAPI_ACCESS_CAPABILITY:
#ifdef CAP_DEVICE_MGT
			/* IRIX has CAP_DEVICE_MGT for DMAPI access. */
			cap_vals[num_cap_vals++] = CAP_DEVICE_MGT;
#elif CAP_MKNOD
			/* Linux has CAP_MKNOD for DMAPI access. */
			cap_vals[num_cap_vals++] = CAP_MKNOD;
#endif
			break;
		case LEASE_CAPABILITY:
#ifdef CAP_LEASE
			cap_vals[num_cap_vals++] = CAP_LEASE;
#endif
			break;
	}

	SMB_ASSERT(num_cap_vals <= ARRAY_SIZE(cap_vals));

	if (num_cap_vals == 0) {
		cap_free(cap);
		return True;
	}

	cap_set_flag(cap, CAP_EFFECTIVE, num_cap_vals, cap_vals,
		enable ? CAP_SET : CAP_CLEAR);

	/* We never want to pass capabilities down to our children, so make
	 * sure they are not inherited.
	 */
	cap_set_flag(cap, CAP_INHERITABLE, num_cap_vals, cap_vals, CAP_CLEAR);

	if (cap_set_proc(cap) == -1) {
		DEBUG(0, ("set_process_capability: cap_set_proc failed: %s\n",
			strerror(errno)));
		cap_free(cap);
		return False;
	}

	cap_free(cap);
	return True;
}

#endif /* HAVE_POSIX_CAPABILITIES */

/****************************************************************************
 Gain the oplock capability from the kernel if possible.
****************************************************************************/

void set_effective_capability(enum smbd_capability capability)
{
#if defined(HAVE_POSIX_CAPABILITIES)
	set_process_capability(capability, True);
#endif /* HAVE_POSIX_CAPABILITIES */
}

void drop_effective_capability(enum smbd_capability capability)
{
#if defined(HAVE_POSIX_CAPABILITIES)
	set_process_capability(capability, False);
#endif /* HAVE_POSIX_CAPABILITIES */
}

/**************************************************************************
 Wrapper for random().
****************************************************************************/

long sys_random(void)
{
#if defined(HAVE_RANDOM)
	return (long)random();
#elif defined(HAVE_RAND)
	return (long)rand();
#else
	DEBUG(0,("Error - no random function available !\n"));
	exit(1);
#endif
}

/**************************************************************************
 Wrapper for srandom().
****************************************************************************/

void sys_srandom(unsigned int seed)
{
#if defined(HAVE_SRANDOM)
	srandom(seed);
#elif defined(HAVE_SRAND)
	srand(seed);
#else
	DEBUG(0,("Error - no srandom function available !\n"));
	exit(1);
#endif
}

#ifndef NGROUPS_MAX
#define NGROUPS_MAX 32 /* Guess... */
#endif

/**************************************************************************
 Returns equivalent to NGROUPS_MAX - using sysconf if needed.
****************************************************************************/

int groups_max(void)
{
#if defined(SYSCONF_SC_NGROUPS_MAX)
	int ret = sysconf(_SC_NGROUPS_MAX);
	return (ret == -1) ? NGROUPS_MAX : ret;
#else
	return NGROUPS_MAX;
#endif
}

/**************************************************************************
 Wrap setgroups and getgroups for systems that declare getgroups() as
 returning an array of gid_t, but actuall return an array of int.
****************************************************************************/

#if defined(HAVE_BROKEN_GETGROUPS)

#ifdef HAVE_BROKEN_GETGROUPS
#define GID_T int
#else
#define GID_T gid_t
#endif

static int sys_broken_getgroups(int setlen, gid_t *gidset)
{
	GID_T gid;
	GID_T *group_list;
	int i, ngroups;

	if(setlen == 0) {
		return getgroups(setlen, &gid);
	}

	/*
	 * Broken case. We need to allocate a
	 * GID_T array of size setlen.
	 */

	if(setlen < 0) {
		errno = EINVAL; 
		return -1;
	} 

	if (setlen == 0)
		setlen = groups_max();

	if((group_list = SMB_MALLOC_ARRAY(GID_T, setlen)) == NULL) {
		DEBUG(0,("sys_getgroups: Malloc fail.\n"));
		return -1;
	}

	if((ngroups = getgroups(setlen, group_list)) < 0) {
		int saved_errno = errno;
		SAFE_FREE(group_list);
		errno = saved_errno;
		return -1;
	}

	for(i = 0; i < ngroups; i++)
		gidset[i] = (gid_t)group_list[i];

	SAFE_FREE(group_list);
	return ngroups;
}

static int sys_broken_setgroups(int setlen, gid_t *gidset)
{
	GID_T *group_list;
	int i ; 

	if (setlen == 0)
		return 0 ;

	if (setlen < 0 || setlen > groups_max()) {
		errno = EINVAL; 
		return -1;   
	}

	/*
	 * Broken case. We need to allocate a
	 * GID_T array of size setlen.
	 */

	if((group_list = SMB_MALLOC_ARRAY(GID_T, setlen)) == NULL) {
		DEBUG(0,("sys_setgroups: Malloc fail.\n"));
		return -1;    
	}

	for(i = 0; i < setlen; i++) 
		group_list[i] = (GID_T) gidset[i]; 

	if(samba_setgroups(setlen, group_list) != 0) {
		int saved_errno = errno;
		SAFE_FREE(group_list);
		errno = saved_errno;
		return -1;
	}

	SAFE_FREE(group_list);
	return 0 ;
}

#endif /* HAVE_BROKEN_GETGROUPS */

/* This is a list of systems that require the first GID passed to setgroups(2)
 * to be the effective GID. If your system is one of these, add it here.
 */
#if defined (FREEBSD) || defined (DARWINOS)
#define USE_BSD_SETGROUPS
#endif

#if defined(USE_BSD_SETGROUPS)
/* Depending on the particular BSD implementation, the first GID that is
 * passed to setgroups(2) will either be ignored or will set the credential's
 * effective GID. In either case, the right thing to do is to guarantee that
 * gidset[0] is the effective GID.
 */
static int sys_bsd_setgroups(gid_t primary_gid, int setlen, const gid_t *gidset)
{
	gid_t *new_gidset = NULL;
	int max;
	int ret;

	/* setgroups(2) will fail with EINVAL if we pass too many groups. */
	max = groups_max();

	/* No group list, just make sure we are setting the efective GID. */
	if (setlen == 0) {
		return samba_setgroups(1, &primary_gid);
	}

	/* If the primary gid is not the first array element, grow the array
	 * and insert it at the front.
	 */
	if (gidset[0] != primary_gid) {
	        new_gidset = SMB_MALLOC_ARRAY(gid_t, setlen + 1);
	        if (new_gidset == NULL) {
			return -1;
	        }

		memcpy(new_gidset + 1, gidset, (setlen * sizeof(gid_t)));
		new_gidset[0] = primary_gid;
		setlen++;
	}

	if (setlen > max) {
		DEBUG(3, ("forced to truncate group list from %d to %d\n",
			setlen, max));
		setlen = max;
	}

#if defined(HAVE_BROKEN_GETGROUPS)
	ret = sys_broken_setgroups(setlen, new_gidset ? new_gidset : gidset);
#else
	ret = samba_setgroups(setlen, new_gidset ? new_gidset : gidset);
#endif

	if (new_gidset) {
		int errsav = errno;
		SAFE_FREE(new_gidset);
		errno = errsav;
	}

	return ret;
}

#endif /* USE_BSD_SETGROUPS */

/**************************************************************************
 Wrapper for getgroups. Deals with broken (int) case.
****************************************************************************/

int sys_getgroups(int setlen, gid_t *gidset)
{
#if defined(HAVE_BROKEN_GETGROUPS)
	return sys_broken_getgroups(setlen, gidset);
#else
	return getgroups(setlen, gidset);
#endif
}

/**************************************************************************
 Wrapper for setgroups. Deals with broken (int) case and BSD case.
****************************************************************************/

int sys_setgroups(gid_t UNUSED(primary_gid), int setlen, gid_t *gidset)
{
#if !defined(HAVE_SETGROUPS)
	errno = ENOSYS;
	return -1;
#endif /* HAVE_SETGROUPS */

#if defined(USE_BSD_SETGROUPS)
	return sys_bsd_setgroups(primary_gid, setlen, gidset);
#elif defined(HAVE_BROKEN_GETGROUPS)
	return sys_broken_setgroups(setlen, gidset);
#else
	return samba_setgroups(setlen, gidset);
#endif
}

/**************************************************************************
 Extract a command into an arg list.
****************************************************************************/

static char **extract_args(TALLOC_CTX *mem_ctx, const char *command)
{
	char *trunc_cmd;
	char *saveptr;
	char *ptr;
	int argcl;
	char **argl = NULL;
	int i;

	if (!(trunc_cmd = talloc_strdup(mem_ctx, command))) {
		DEBUG(0, ("talloc failed\n"));
		goto nomem;
	}

	if(!(ptr = strtok_r(trunc_cmd, " \t", &saveptr))) {
		TALLOC_FREE(trunc_cmd);
		errno = EINVAL;
		return NULL;
	}

	/*
	 * Count the args.
	 */

	for( argcl = 1; ptr; ptr = strtok_r(NULL, " \t", &saveptr))
		argcl++;

	TALLOC_FREE(trunc_cmd);

	if (!(argl = talloc_array(mem_ctx, char *, argcl + 1))) {
		goto nomem;
	}

	/*
	 * Now do the extraction.
	 */

	if (!(trunc_cmd = talloc_strdup(mem_ctx, command))) {
		goto nomem;
	}

	ptr = strtok_r(trunc_cmd, " \t", &saveptr);
	i = 0;

	if (!(argl[i++] = talloc_strdup(argl, ptr))) {
		goto nomem;
	}

	while((ptr = strtok_r(NULL, " \t", &saveptr)) != NULL) {

		if (!(argl[i++] = talloc_strdup(argl, ptr))) {
			goto nomem;
		}
	}

	argl[i++] = NULL;
	TALLOC_FREE(trunc_cmd);
	return argl;

 nomem:
	DEBUG(0, ("talloc failed\n"));
	TALLOC_FREE(trunc_cmd);
	TALLOC_FREE(argl);
	errno = ENOMEM;
	return NULL;
}

/**************************************************************************
 Wrapper for popen. Safer as it doesn't search a path.
 Modified from the glibc sources.
 modified by tridge to return a file descriptor. We must kick our FILE* habit
****************************************************************************/

typedef struct _popen_list
{
	int fd;
	pid_t child_pid;
	struct _popen_list *next;
} popen_list;

static popen_list *popen_chain;

int sys_popen(const char *command)
{
//   int parent_end, child_end;
//   int pipe_fds[2];
//   popen_list *entry = NULL;
//   char **argl = NULL;
//   int ret;
//
//   if (!*command) {
//     errno = EINVAL;
//     return -1;
//   }
//
//   ret = pipe(pipe_fds);
//   if (ret < 0) {
//     DEBUG(0, ("sys_popen: error opening pipe: %s\n",
//         strerror(errno)));
//     return -1;
//   }
//
//   parent_end = pipe_fds[0];
//   child_end = pipe_fds[1];
//
//   entry = SMB_MALLOC_P(popen_list);
//   if (entry == NULL) {
//     DEBUG(0, ("sys_popen: malloc failed\n"));
//     goto err_exit;
//   }
//
//   ZERO_STRUCTP(entry);
//
//   /*
//    * Extract the command and args into a NULL terminated array.
//    */
//
//   argl = extract_args(NULL, command);
//   if (argl == NULL) {
//     DEBUG(0, ("sys_popen: extract_args() failed: %s\n", strerror(errno)));
//     goto err_exit;
//   }
//
//   entry->child_pid = fork();
//
//   if (entry->child_pid == -1) {
//     DEBUG(0, ("sys_popen: fork failed: %s\n", strerror(errno)));
//     goto err_exit;
//   }
//
//   if (entry->child_pid == 0) {
//
//     /*
//      * Child !
//      */
//
//     int child_std_end = STDOUT_FILENO;
//     popen_list *p;
//
//     close(parent_end);
//     if (child_end != child_std_end) {
//       dup2 (child_end, child_std_end);
//       close (child_end);
//     }
//
//     /*
//      * POSIX.2:  "popen() shall ensure that any streams from previous
//      * popen() calls that remain open in the parent process are closed
//      * in the new child process."
//      */
//
//     for (p = popen_chain; p; p = p->next)
//       close(p->fd);
//
//     ret = execv(argl[0], argl);
//     if (ret == -1) {
//       DEBUG(0, ("sys_popen: ERROR executing command "
//           "'%s': %s\n", command, strerror(errno)));
//     }
//     _exit (127);
//   }
//
//   /*
//    * Parent.
//    */
//
//   close (child_end);
//   TALLOC_FREE(argl);
//
//   /* Link into popen_chain. */
//   entry->next = popen_chain;
//   popen_chain = entry;
//   entry->fd = parent_end;
//
//   return entry->fd;
//
// err_exit:
//
//   SAFE_FREE(entry);
//   TALLOC_FREE(argl);
//   close(pipe_fds[0]);
//   close(pipe_fds[1]);
	return -1;
}

/**************************************************************************
 Wrapper for pclose. Modified from the glibc sources.
****************************************************************************/

int sys_pclose(int fd)
{
	int wstatus;
	popen_list **ptr = &popen_chain;
	popen_list *entry = NULL;
	pid_t wait_pid;
	int status = -1;

	/* Unlink from popen_chain. */
	for ( ; *ptr != NULL; ptr = &(*ptr)->next) {
		if ((*ptr)->fd == fd) {
			entry = *ptr;
			*ptr = (*ptr)->next;
			status = 0;
			break;
		}
	}

	if (status < 0 || close(entry->fd) < 0)
		return -1;

	/*
	 * As Samba is catching and eating child process
	 * exits we don't really care about the child exit
	 * code, a -1 with errno = ECHILD will do fine for us.
	 */

	do {
		wait_pid = sys_waitpid (entry->child_pid, &wstatus, 0);
	} while (wait_pid == -1 && errno == EINTR);

	SAFE_FREE(entry);

	if (wait_pid == -1)
		return -1;
	return wstatus;
}

/**************************************************************************
 Wrapper for Admin Logs.
****************************************************************************/

 void sys_adminlog(int priority, const char *format_str, ...) 
{
	va_list ap;
	int ret;
	char *msgbuf = NULL;

	va_start( ap, format_str );
	ret = vasprintf( &msgbuf, format_str, ap );
	va_end( ap );

	if (ret == -1)
		return;

#if defined(HAVE_SYSLOG)
	syslog( priority, "%s", msgbuf );
#else
	DEBUG(0,("%s", msgbuf ));
#endif
	SAFE_FREE(msgbuf);
}

/****************************************************************************
 Return the major devicenumber for UNIX extensions.
****************************************************************************/

uint32 unix_dev_major(SMB_DEV_T dev)
{
#if defined(HAVE_DEVICE_MAJOR_FN)
        return (uint32)major(dev);
#else
        return (uint32)(dev >> 8);
#endif
}

/****************************************************************************
 Return the minor devicenumber for UNIX extensions.
****************************************************************************/

uint32 unix_dev_minor(SMB_DEV_T dev)
{
#if defined(HAVE_DEVICE_MINOR_FN)
        return (uint32)minor(dev);
#else
        return (uint32)(dev & 0xff);
#endif
}

#if 0
/*******************************************************************
 Return the number of CPUs.
********************************************************************/

int sys_get_number_of_cores(void)
{
	int ret = -1;

#if defined(HAVE_SYSCONF)
#if defined(_SC_NPROCESSORS_ONLN)
	ret = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
#if defined(_SC_NPROCESSORS_CONF)
	if (ret < 1) {
		ret = (int)sysconf(_SC_NPROCESSORS_CONF);
	}
#endif
#elif defined(HAVE_SYSCTL) && defined(CTL_HW)
	int name[2];
	unsigned int len = sizeof(ret);

	name[0] = CTL_HW;
#if defined(HW_AVAILCPU)
	name[1] = HW_AVAILCPU;

	if (sysctl(name, 2, &ret, &len, NULL, 0) == -1) {
		ret = -1;
	}
#endif
#if defined(HW_NCPU)
	if(ret < 1) {
		name[0] = CTL_HW;
		name[1] = HW_NCPU;
		if (sysctl(nm, 2, &count, &len, NULL, 0) == -1) {
			ret = -1;
		}
	}
#endif
#endif
	if (ret < 1) {
		ret = 1;
	}
	return ret;
}
#endif
