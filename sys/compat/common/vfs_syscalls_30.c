/*	$NetBSD: vfs_syscalls_30.c,v 1.43 2021/09/07 11:43:02 riastradh Exp $	*/

/*-
 * Copyright (c) 2005, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: vfs_syscalls_30.c,v 1.43 2021/09/07 11:43:02 riastradh Exp $");

#if defined(_KERNEL_OPT)
#include "opt_compat_netbsd.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socketvar.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/dirent.h>
#include <sys/malloc.h>
#include <sys/kauth.h>
#include <sys/vfs_syscalls.h>
#include <sys/syscall.h>
#include <sys/syscallvar.h>
#include <sys/syscallargs.h>

#include <compat/common/compat_mod.h>
#include <compat/common/compat_util.h>

#include <compat/sys/stat.h>
#include <compat/sys/dirent.h>
#include <compat/sys/mount.h>
#include <compat/sys/statvfs.h>

static const struct syscall_package vfs_syscalls_30_syscalls[] = {
	{ SYS_compat_30___fhstat30, 0, (sy_call_t *)compat_30_sys___fhstat30 },
	{ SYS_compat_30___fstat13, 0, (sy_call_t *)compat_30_sys___fstat13 },
	{ SYS_compat_30___lstat13, 0, (sy_call_t *)compat_30_sys___lstat13 }, 
	{ SYS_compat_30___stat13, 0, (sy_call_t *)compat_30_sys___stat13 },  
	{ SYS_compat_30_fhopen, 0, (sy_call_t *)compat_30_sys_fhopen },
	{ SYS_compat_30_fhstat, 0, (sy_call_t *)compat_30_sys_fhstat },  
	{ SYS_compat_30_fhstatvfs1, 0, (sy_call_t *)compat_30_sys_fhstatvfs1 },
	{ SYS_compat_30_getdents, 0, (sy_call_t *)compat_30_sys_getdents },
	{ SYS_compat_30_getfh, 0, (sy_call_t *)compat_30_sys_getfh },
	{ 0,0, NULL }
};

/*
 * Convert from a new to an old stat structure.
 */
static void
cvtstat(struct stat13 *ost, const struct stat *st)
{

	/* Handle any padding. */
	memset(ost, 0, sizeof(*ost));
	ost->st_dev = st->st_dev;
	ost->st_ino = (uint32_t)st->st_ino;
	ost->st_mode = st->st_mode;
	ost->st_nlink = st->st_nlink;
	ost->st_uid = st->st_uid;
	ost->st_gid = st->st_gid;
	ost->st_rdev = st->st_rdev;
	timespec_to_timespec50(&st->st_atimespec, &ost->st_atimespec);
	timespec_to_timespec50(&st->st_mtimespec, &ost->st_mtimespec);
	timespec_to_timespec50(&st->st_ctimespec, &ost->st_ctimespec);
	timespec_to_timespec50(&st->st_birthtimespec, &ost->st_birthtimespec);
	ost->st_size = st->st_size;
	ost->st_blocks = st->st_blocks;
	ost->st_blksize = st->st_blksize;
	ost->st_flags = st->st_flags;
	ost->st_gen = st->st_gen;
}

/*
 * Get file status; this version follows links.
 */
/* ARGSUSED */
int
compat_30_sys___stat13(struct lwp *l,
    const struct compat_30_sys___stat13_args *uap, register_t *retval)
{
	/* {
		syscallarg(const char *) path;
		syscallarg(struct stat13 *) ub;
	} */
	struct stat sb;
	struct stat13 osb;
	int error;

	error = do_sys_stat(SCARG(uap, path), FOLLOW, &sb);
	if (error)
		return error;
	cvtstat(&osb, &sb);
	return copyout(&osb, SCARG(uap, ub), sizeof(osb));
}


/*
 * Get file status; this version does not follow links.
 */
/* ARGSUSED */
int
compat_30_sys___lstat13(struct lwp *l,
    const struct compat_30_sys___lstat13_args *uap, register_t *retval)
{
	/* {
		syscallarg(const char *) path;
		syscallarg(struct stat13 *) ub;
	} */
	struct stat sb;
	struct stat13 osb;
	int error;

	error = do_sys_stat(SCARG(uap, path), NOFOLLOW, &sb);
	if (error)
		return error;
	cvtstat(&osb, &sb);
	return copyout(&osb, SCARG(uap, ub), sizeof(osb));
}

/* ARGSUSED */
int
compat_30_sys_fhstat(struct lwp *l,
    const struct compat_30_sys_fhstat_args *uap, register_t *retval)
{
	/* {
		syscallarg(const struct compat_30_fhandle *) fhp;
		syscallarg(struct stat13 *) sb;
	} */
	struct stat sb;
	struct stat13 osb;
	int error;

	error = do_fhstat(l, SCARG(uap, fhp), sizeof(*SCARG(uap, fhp)), &sb);
	if (error)
		return error;
	cvtstat(&osb, &sb);
	return copyout(&osb, SCARG(uap, sb), sizeof(osb));
}

/*
 * Return status information about a file descriptor.
 */
/* ARGSUSED */
int
compat_30_sys___fstat13(struct lwp *l,
    const struct compat_30_sys___fstat13_args *uap, register_t *retval)
{
	/* {
		syscallarg(int) fd;
		syscallarg(struct stat13 *) sb;
	} */
	struct stat sb;
	struct stat13 osb;
	int error;

	error = do_sys_fstat(SCARG(uap, fd), &sb);
	if (error)
		return error;
	cvtstat(&osb, &sb);
	return copyout(&osb, SCARG(uap, sb), sizeof(osb));
}

/*
 * Read a block of directory entries in a file system independent format.
 */
int
compat_30_sys_getdents(struct lwp *l,
    const struct compat_30_sys_getdents_args *uap, register_t *retval)
{
	/* {
		syscallarg(int) fd;
		syscallarg(char *) buf;
		syscallarg(size_t) count;
	} */
	struct dirent *bdp;
	struct vnode *vp;
	char *inp, *tbuf;	/* BSD-format */
	int len, reclen;	/* BSD-format */
	char *outp;		/* NetBSD-3.0-format */
	int resid;	
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	struct dirent12 idb;
	off_t off;		/* true file offset */
	int buflen, error, eofflag;
	off_t *cookiebuf = NULL, *cookie;
	int ncookies;

	/* fd_getvnode() will use the descriptor for us */
	if ((error = fd_getvnode(SCARG(uap, fd), &fp)) != 0)
		return error;

	if ((fp->f_flag & FREAD) == 0) {
		error = EBADF;
		goto out1;
	}

	vp = fp->f_vnode;
	if (vp->v_type != VDIR) {
		error = EINVAL;
		goto out1;
	}

	buflen = uimin(MAXBSIZE, SCARG(uap, count));
	tbuf = malloc(buflen, M_TEMP, M_WAITOK);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	off = fp->f_offset;
again:
	aiov.iov_base = tbuf;
	aiov.iov_len = buflen;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_resid = buflen;
	auio.uio_offset = off;
	UIO_SETUP_SYSSPACE(&auio);
	/*
         * First we read into the malloc'ed buffer, then
         * we massage it into user space, one record at a time.
         */
	error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag, &cookiebuf,
	    &ncookies);
	if (error)
		goto out;

	inp = tbuf;
	outp = SCARG(uap, buf);
	resid = SCARG(uap, count);
	if ((len = buflen - auio.uio_resid) == 0)
		goto eof;

	for (cookie = cookiebuf; len > 0; len -= reclen) {
		bdp = (struct dirent *)inp;
		reclen = bdp->d_reclen;
		if (reclen & _DIRENT_ALIGN(bdp))
			panic("%s: bad reclen %d", __func__, reclen);
		if (cookie)
			off = *cookie++; /* each entry points to the next */
		else
			off += reclen;
		if ((off >> 32) != 0) {
			compat_offseterr(vp, "netbsd30_getdents");
			error = EINVAL;
			goto out;
		}
		memset(&idb, 0, sizeof(idb));
		if (bdp->d_namlen >= sizeof(idb.d_name))
			idb.d_namlen = sizeof(idb.d_name) - 1;
		else
			idb.d_namlen = bdp->d_namlen;
		idb.d_reclen = _DIRENT_SIZE(&idb);
		if (reclen > len || resid < idb.d_reclen) {
			/* entry too big for buffer, so just stop */
			outp++;
			break;
		}
		/*
		 * Massage in place to make a NetBSD-3.0-shaped dirent
		 * (otherwise we have to worry about touching user memory
		 * outside of the copyout() call).
		 */
		idb.d_fileno = (u_int32_t)bdp->d_fileno;
		idb.d_type = bdp->d_type;
		(void)memcpy(idb.d_name, bdp->d_name, idb.d_namlen);
		memset(idb.d_name + idb.d_namlen, 0,
		    idb.d_reclen - _DIRENT_NAMEOFF(&idb) - idb.d_namlen);
		if ((error = copyout(&idb, outp, idb.d_reclen)) != 0)
			goto out;
		/* advance past this real entry */
		inp += reclen;
		/* advance output past NetBSD-3.0-shaped entry */
		outp += idb.d_reclen;
		resid -= idb.d_reclen;
	}

	/* if we squished out the whole block, try again */
	if (outp == SCARG(uap, buf)) {
		if (cookiebuf)
			free(cookiebuf, M_TEMP);
		cookiebuf = NULL;
		goto again;
	}
	fp->f_offset = off;	/* update the vnode offset */

eof:
	*retval = SCARG(uap, count) - resid;
out:
	VOP_UNLOCK(vp);
	if (cookiebuf)
		free(cookiebuf, M_TEMP);
	free(tbuf, M_TEMP);
out1:
	fd_putfile(SCARG(uap, fd));
	return error;
}

/*
 * Get file handle system call
 */
int
compat_30_sys_getfh(struct lwp *l, const struct compat_30_sys_getfh_args *uap,
    register_t *retval)
{
	/* {
		syscallarg(char *) fname;
		syscallarg(struct compat_30_fhandle *) fhp;
	} */
	struct vnode *vp;
	struct compat_30_fhandle fh;
	int error;
	struct pathbuf *pb;
	struct nameidata nd;
	size_t sz;

	/*
	 * Must be super user
	 */
	error = kauth_authorize_system(l->l_cred, KAUTH_SYSTEM_FILEHANDLE,
	    0, NULL, NULL, NULL);
	if (error)
		return (error);

	error = pathbuf_copyin(SCARG(uap, fname), &pb);
	if (error) {
		return error;
	}
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | TRYEMULROOT, pb);
	error = namei(&nd);
	pathbuf_destroy(pb);
	if (error)
		return error;
	vp = nd.ni_vp;

	sz = sizeof(struct compat_30_fhandle);
	error = vfs_composefh(vp, (void *)&fh, &sz);
	vput(vp);
	CTASSERT(FHANDLE_SIZE_COMPAT == sizeof(struct compat_30_fhandle));
	if (sz != FHANDLE_SIZE_COMPAT) {
		error = EINVAL;
	}
	if (error)
		return error;
	return copyout(&fh, SCARG(uap, fhp), sizeof(fh));
}

/*
 * Open a file given a file handle.
 *
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
int
compat_30_sys_fhopen(struct lwp *l,
    const struct compat_30_sys_fhopen_args *uap, register_t *retval)
{
	/* {
		syscallarg(const fhandle_t *) fhp;
		syscallarg(int) flags;
	} */

	return dofhopen(l, SCARG(uap, fhp), FHANDLE_SIZE_COMPAT,
	    SCARG(uap, flags), retval);
}

/* ARGSUSED */
int
compat_30_sys___fhstat30(struct lwp *l,
    const struct compat_30_sys___fhstat30_args *uap_30, register_t *retval)
{
	/* {
		syscallarg(const fhandle_t *) fhp;
		syscallarg(struct stat30 *) sb;
	} */
	struct stat sb;
	struct stat13 osb;
	int error;

	error = do_fhstat(l, SCARG(uap_30, fhp), FHANDLE_SIZE_COMPAT, &sb);
	if (error)
		return error;
	cvtstat(&osb, &sb);
	return copyout(&osb, SCARG(uap_30, sb), sizeof(osb));
}

/* ARGSUSED */
int
compat_30_sys_fhstatvfs1(struct lwp *l,
    const struct compat_30_sys_fhstatvfs1_args *uap, register_t *retval)
{
	/* {
		syscallarg(const fhandle_t *) fhp;
		syscallarg(struct statvfs90 *) buf;
		syscallarg(int)	flags;
	} */
	struct statvfs *sb = STATVFSBUF_GET();
	int error = do_fhstatvfs(l, SCARG(uap, fhp), FHANDLE_SIZE_COMPAT,
	    sb, SCARG(uap, flags));

	if (!error) {
		error = statvfs_to_statvfs90_copy(sb, SCARG(uap, buf),
		    sizeof(struct statvfs90));
	}

	STATVFSBUF_PUT(sb);

	return error;
}

int
vfs_syscalls_30_init(void)
{

	return syscall_establish(NULL, vfs_syscalls_30_syscalls);
}

int
vfs_syscalls_30_fini(void)
{

	return syscall_disestablish(NULL, vfs_syscalls_30_syscalls);
}
