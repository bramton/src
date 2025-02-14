/*	$NetBSD: ufs_vnops.c,v 1.261 2021/11/26 17:35:12 christos Exp $	*/

/*-
 * Copyright (c) 2008, 2020 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Wasabi Systems, Inc.
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

/*
 * Copyright (c) 1982, 1986, 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_vnops.c	8.28 (Berkeley) 7/31/95
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ufs_vnops.c,v 1.261 2021/11/26 17:35:12 christos Exp $");

#if defined(_KERNEL_OPT)
#include "opt_ffs.h"
#include "opt_quota.h"
#include "opt_uvmhist.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/fstrans.h>
#include <sys/kmem.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/lockf.h>
#include <sys/kauth.h>
#include <sys/wapbl.h>

#include <miscfs/specfs/specdev.h>
#include <miscfs/fifofs/fifo.h>
#include <miscfs/genfs/genfs.h>

#include <ufs/ufs/acl.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_bswap.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/ufs_wapbl.h>
#ifdef UFS_DIRHASH
#include <ufs/ufs/dirhash.h>
#endif
#include <ufs/ext2fs/ext2fs_extern.h>
#include <ufs/ext2fs/ext2fs_dir.h>
#include <ufs/ffs/ffs_extern.h>
#include <ufs/lfs/lfs_extern.h>
#include <ufs/lfs/lfs.h>

#ifdef UVMHIST
#include <uvm/uvm.h>
#endif
#include <uvm/uvm_extern.h>
#include <uvm/uvm_stat.h>

__CTASSERT(EXT2FS_MAXNAMLEN == FFS_MAXNAMLEN);
__CTASSERT(LFS_MAXNAMLEN == FFS_MAXNAMLEN);

static int ufs_chmod(struct vnode *, int, kauth_cred_t, struct lwp *);
static int ufs_chown(struct vnode *, uid_t, gid_t, kauth_cred_t,
    struct lwp *);
static int ufs_makeinode(struct vattr *, struct vnode *,
    const struct ufs_lookup_results *, struct vnode **, struct componentname *);

/*
 * A virgin directory (no blushing please).
 */
static const struct dirtemplate mastertemplate = {
	0,	12,			DT_DIR,	1,	".",
	0,	UFS_DIRBLKSIZ - 12,	DT_DIR,	2,	".."
};

/*
 * Create a regular file
 */
int
ufs_create(void *v)
{
	struct vop_create_v3_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
	} */ *ap = v;
	int	error;
	struct vnode *dvp = ap->a_dvp;
	struct ufs_lookup_results *ulr;

	/* XXX should handle this material another way */
	ulr = &VTOI(dvp)->i_crap;
	UFS_CHECK_CRAPCOUNTER(VTOI(dvp));

	/*
	 * UFS_WAPBL_BEGIN(dvp->v_mount) performed by successful
	 * ufs_makeinode
	 */
	error = ufs_makeinode(ap->a_vap, dvp, ulr, ap->a_vpp, ap->a_cnp);
	if (error) {
		return (error);
	}
	UFS_WAPBL_END(dvp->v_mount);
	VOP_UNLOCK(*ap->a_vpp);
	return (0);
}

/*
 * Mknod vnode call
 */
/* ARGSUSED */
int
ufs_mknod(void *v)
{
	struct vop_mknod_v3_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
	} */ *ap = v;
	struct vattr	*vap;
	struct vnode	**vpp;
	struct inode	*ip;
	int		error;
	struct ufs_lookup_results *ulr;

	vap = ap->a_vap;
	vpp = ap->a_vpp;

	/* XXX should handle this material another way */
	ulr = &VTOI(ap->a_dvp)->i_crap;
	UFS_CHECK_CRAPCOUNTER(VTOI(ap->a_dvp));

	/*
	 * UFS_WAPBL_BEGIN(dvp->v_mount) performed by successful
	 * ufs_makeinode
	 */
	if ((error = ufs_makeinode(vap, ap->a_dvp, ulr, vpp, ap->a_cnp)) != 0)
		goto out;
	ip = VTOI(*vpp);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	UFS_WAPBL_UPDATE(*vpp, NULL, NULL, 0);
	UFS_WAPBL_END(ap->a_dvp->v_mount);
	VOP_UNLOCK(*vpp);
out:
	if (error != 0) {
		*vpp = NULL;
		return (error);
	}
	return (0);
}

/*
 * Open called.
 *
 * Nothing to do.
 */
/* ARGSUSED */
int
ufs_open(void *v)
{
	struct vop_open_args /* {
		struct vnode	*a_vp;
		int		a_mode;
		kauth_cred_t	a_cred;
	} */ *ap = v;

	/*
	 * Files marked append-only must be opened for appending.
	 */
	if ((VTOI(ap->a_vp)->i_flags & APPEND) &&
	    (ap->a_mode & (FWRITE | O_APPEND)) == FWRITE)
		return (EPERM);
	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode.
 */
/* ARGSUSED */
int
ufs_close(void *v)
{
	struct vop_close_args /* {
		struct vnode	*a_vp;
		int		a_fflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode	*vp;

	vp = ap->a_vp;
	if (vrefcnt(vp) > 1)
		UFS_ITIMES(vp, NULL, NULL, NULL);
	return (0);
}

static int
ufs_check_possible(struct vnode *vp, struct inode *ip, accmode_t accmode,
    kauth_cred_t cred)
{
#if defined(QUOTA) || defined(QUOTA2)
	int error;
#endif

	/*
	 * Disallow write attempts on read-only file systems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (accmode & VMODIFY_PERMS) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return EROFS;
#if defined(QUOTA) || defined(QUOTA2)
			error = chkdq(ip, 0, cred, 0);
			if (error != 0)
				return error;
#endif
			break;
		case VBAD:
		case VBLK:
		case VCHR:
		case VSOCK:
		case VFIFO:
		case VNON:
		default:
			break;
		}
	}

	/* If it is a snapshot, nobody gets access to it. */
	if ((ip->i_flags & SF_SNAPSHOT))
		return EPERM;
	/*
	 * If immutable bit set, nobody gets to write it.  "& ~VADMIN_PERMS"
	 * permits the owner of the file to remove the IMMUTABLE flag.
	 */
	if ((accmode & (VMODIFY_PERMS & ~VADMIN_PERMS)) &&
	    (ip->i_flags & IMMUTABLE))
		return EPERM;

	return 0;
}

static int
ufs_check_permitted(struct vnode *vp, struct inode *ip,
    struct acl *acl, accmode_t accmode, kauth_cred_t cred,
    int (*func)(struct vnode *, kauth_cred_t, uid_t, gid_t, mode_t,
    struct acl *, accmode_t))
{

	return kauth_authorize_vnode(cred, KAUTH_ACCESS_ACTION(accmode,
	    vp->v_type, ip->i_mode & ALLPERMS), vp, NULL, (*func)(vp, cred,
	    ip->i_uid, ip->i_gid, ip->i_mode & ALLPERMS, acl, accmode));
}

int
ufs_accessx(void *v)
{
	struct vop_accessx_args /* {
		struct vnode *a_vp;
		accmode_t a_accmode;
		kauth_cred_t a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	accmode_t accmode = ap->a_accmode;
	int error;
#ifdef UFS_ACL
	struct acl *acl;
	acl_type_t type;
#endif

	error = ufs_check_possible(vp, ip, accmode, ap->a_cred);
	if (error)
		return error;

#ifdef UFS_ACL
	if ((vp->v_mount->mnt_flag & (MNT_POSIX1EACLS | MNT_NFS4ACLS)) != 0) {
		if (vp->v_mount->mnt_flag & MNT_NFS4ACLS)
			type = ACL_TYPE_NFS4;
		else
			type = ACL_TYPE_ACCESS;

		acl = acl_alloc(KM_SLEEP);
		if (type == ACL_TYPE_NFS4)
			error = ufs_getacl_nfs4_internal(vp, acl, curlwp);
		else
			error = VOP_GETACL(vp, type, acl, ap->a_cred);
		if (!error) {
			if (type == ACL_TYPE_NFS4) {
				error = ufs_check_permitted(vp,
				    ip, acl, accmode, ap->a_cred,
				    genfs_can_access_acl_nfs4);
			} else {
				error = vfs_unixify_accmode(&accmode);
				if (error == 0)
					error = ufs_check_permitted(vp,
					    ip, acl, accmode, ap->a_cred,
					    genfs_can_access_acl_posix1e);
			}
			acl_free(acl);
			return error;
		}
		if (error != EOPNOTSUPP)
			printf("%s: Error retrieving ACL: %d\n",
			    __func__, error);
		/*
		 * XXX: Fall back until debugged.  Should
		 * eventually possibly log an error, and return
		 * EPERM for safety.
		 */
		acl_free(acl);
	}
#endif /* !UFS_ACL */
	error = vfs_unixify_accmode(&accmode);
	if (error)
		return error;
	return ufs_check_permitted(vp, ip,
	    NULL, accmode, ap->a_cred, genfs_can_access);
}

/* ARGSUSED */
int
ufs_getattr(void *v)
{
	struct vop_getattr_args /* {
		struct vnode	*a_vp;
		struct vattr	*a_vap;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode	*vp;
	struct inode	*ip;
	struct vattr	*vap;

	vp = ap->a_vp;
	ip = VTOI(vp);
	vap = ap->a_vap;
	UFS_ITIMES(vp, NULL, NULL, NULL);

	/*
	 * Copy from inode table
	 */
	vap->va_fsid = ip->i_dev;
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mode & ALLPERMS;
	vap->va_nlink = ip->i_nlink;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_size = vp->v_size;
	if (ip->i_ump->um_fstype == UFS1) {
		switch (vp->v_type) {
		    case VBLK:
		    case VCHR:
			vap->va_rdev = (dev_t)ufs_rw32(ip->i_ffs1_rdev,
			    UFS_MPNEEDSWAP(ip->i_ump));
			break;
		    default:
			vap->va_rdev = NODEV;
			break;
		}
		vap->va_atime.tv_sec = ip->i_ffs1_atime;
		vap->va_atime.tv_nsec = ip->i_ffs1_atimensec;
		vap->va_mtime.tv_sec = ip->i_ffs1_mtime;
		vap->va_mtime.tv_nsec = ip->i_ffs1_mtimensec;
		vap->va_ctime.tv_sec = ip->i_ffs1_ctime;
		vap->va_ctime.tv_nsec = ip->i_ffs1_ctimensec;
		vap->va_birthtime.tv_sec = 0;
		vap->va_birthtime.tv_nsec = 0;
		vap->va_bytes = dbtob((u_quad_t)ip->i_ffs1_blocks);
	} else {
		switch (vp->v_type) {
		    case VBLK:
		    case VCHR:
			vap->va_rdev = (dev_t)ufs_rw64(ip->i_ffs2_rdev,
			    UFS_MPNEEDSWAP(ip->i_ump));
			break;
		    default:
			vap->va_rdev = NODEV;
			break;
		}
		vap->va_atime.tv_sec = ip->i_ffs2_atime;
		vap->va_atime.tv_nsec = ip->i_ffs2_atimensec;
		vap->va_mtime.tv_sec = ip->i_ffs2_mtime;
		vap->va_mtime.tv_nsec = ip->i_ffs2_mtimensec;
		vap->va_ctime.tv_sec = ip->i_ffs2_ctime;
		vap->va_ctime.tv_nsec = ip->i_ffs2_ctimensec;
		vap->va_birthtime.tv_sec = ip->i_ffs2_birthtime;
		vap->va_birthtime.tv_nsec = ip->i_ffs2_birthnsec;
		vap->va_bytes = dbtob(ip->i_ffs2_blocks);
	}
	vap->va_gen = ip->i_gen;
	vap->va_flags = ip->i_flags;

	/* this doesn't belong here */
	if (vp->v_type == VBLK)
		vap->va_blocksize = BLKDEV_IOSIZE;
	else if (vp->v_type == VCHR)
		vap->va_blocksize = MAXBSIZE;
	else
		vap->va_blocksize = vp->v_mount->mnt_stat.f_iosize;
	vap->va_type = vp->v_type;
	vap->va_filerev = ip->i_modrev;
	return (0);
}

/*
 * Set attribute vnode op. called from several syscalls
 */
int
ufs_setattr(void *v)
{
	struct vop_setattr_args /* {
		struct vnode	*a_vp;
		struct vattr	*a_vap;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vattr	*vap;
	struct vnode	*vp;
	struct inode	*ip;
	kauth_cred_t	cred;
	struct lwp	*l;
	int		error;
	kauth_action_t	action;
	bool		changing_sysflags;

	vap = ap->a_vap;
	vp = ap->a_vp;
	ip = VTOI(vp);
	cred = ap->a_cred;
	l = curlwp;
	action = KAUTH_VNODE_WRITE_FLAGS;
	changing_sysflags = false;

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}

	UFS_WAPBL_JUNLOCK_ASSERT(vp->v_mount);

	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}

		/* Snapshot flag cannot be set or cleared */
		if ((vap->va_flags & (SF_SNAPSHOT | SF_SNAPINVAL)) !=
		    (ip->i_flags & (SF_SNAPSHOT | SF_SNAPINVAL))) {
			error = EPERM;
			goto out;
		}

		if (ip->i_flags & (SF_IMMUTABLE | SF_APPEND)) {
			action |= KAUTH_VNODE_HAS_SYSFLAGS;
		}

		if ((vap->va_flags & SF_SETTABLE) !=
		    (ip->i_flags & SF_SETTABLE)) {
			action |= KAUTH_VNODE_WRITE_SYSFLAGS;
			changing_sysflags = true;
		}

		error = kauth_authorize_vnode(cred, action, vp, NULL,
		    genfs_can_chflags(vp, cred, ip->i_uid, changing_sysflags));
		if (error)
			goto out;

		if (changing_sysflags) {
			error = UFS_WAPBL_BEGIN(vp->v_mount);
			if (error)
				goto out;
			ip->i_flags = vap->va_flags;
			DIP_ASSIGN(ip, flags, ip->i_flags);
		} else {
			error = UFS_WAPBL_BEGIN(vp->v_mount);
			if (error)
				goto out;
			ip->i_flags &= SF_SETTABLE;
			ip->i_flags |= (vap->va_flags & UF_SETTABLE);
			DIP_ASSIGN(ip, flags, ip->i_flags);
		}
		ip->i_flag |= IN_CHANGE;
		UFS_WAPBL_UPDATE(vp, NULL, NULL, 0);
		UFS_WAPBL_END(vp->v_mount);
		if (vap->va_flags & (IMMUTABLE | APPEND)) {
			error = 0;
			goto out;
		}
	}
	if (ip->i_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out;
	}
	/*
	 * Go through the fields and update iff not VNOVAL.
	 */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		error = UFS_WAPBL_BEGIN(vp->v_mount);
		if (error)
			goto out;
		error = ufs_chown(vp, vap->va_uid, vap->va_gid, cred, l);
		UFS_WAPBL_END(vp->v_mount);
		if (error)
			goto out;
	}
	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the file system.
		 */
		switch (vp->v_type) {
		case VDIR:
			error = EISDIR;
			goto out;
		case VCHR:
		case VBLK:
		case VFIFO:
			break;
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY) {
				error = EROFS;
				goto out;
			}
			if ((ip->i_flags & SF_SNAPSHOT) != 0) {
				error = EPERM;
				goto out;
			}
			error = ufs_truncate_retry(vp, 0, vap->va_size, cred);
			if (error)
				goto out;
			break;
		default:
			error = EOPNOTSUPP;
			goto out;
		}
	}
	ip = VTOI(vp);
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL ||
	    vap->va_birthtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		if ((ip->i_flags & SF_SNAPSHOT) != 0) {
			error = EPERM;
			goto out;
		}
		error = kauth_authorize_vnode(cred, KAUTH_VNODE_WRITE_TIMES, vp,
		    NULL, genfs_can_chtimes(vp, cred, ip->i_uid,
		    vap->va_vaflags));
		if (error)
			goto out;
		error = UFS_WAPBL_BEGIN(vp->v_mount);
		if (error)
			goto out;
		if (vap->va_atime.tv_sec != VNOVAL)
			if (!(vp->v_mount->mnt_flag & MNT_NOATIME))
				ip->i_flag |= IN_ACCESS;
		if (vap->va_mtime.tv_sec != VNOVAL) {
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			if (vp->v_mount->mnt_flag & MNT_RELATIME)
				ip->i_flag |= IN_ACCESS;
		}
		if (vap->va_birthtime.tv_sec != VNOVAL &&
		    ip->i_ump->um_fstype == UFS2) {
			ip->i_ffs2_birthtime = vap->va_birthtime.tv_sec;
			ip->i_ffs2_birthnsec = vap->va_birthtime.tv_nsec;
		}
		error = UFS_UPDATE(vp, &vap->va_atime, &vap->va_mtime, 0);
		UFS_WAPBL_END(vp->v_mount);
		if (error)
			goto out;
	}
	error = 0;
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		if ((ip->i_flags & SF_SNAPSHOT) != 0 &&
		    (vap->va_mode & (S_IXUSR | S_IWUSR | S_IXGRP | S_IWGRP |
		     S_IXOTH | S_IWOTH))) {
			error = EPERM;
			goto out;
		}
		error = UFS_WAPBL_BEGIN(vp->v_mount);
		if (error)
			goto out;
		error = ufs_chmod(vp, (int)vap->va_mode, cred, l);
		UFS_WAPBL_END(vp->v_mount);
	}
out:
	cache_enter_id(vp, ip->i_mode, ip->i_uid, ip->i_gid, !HAS_ACLS(ip));
	return (error);
}

#ifdef UFS_ACL
static int
ufs_update_nfs4_acl_after_mode_change(struct vnode *vp, int mode,
    int file_owner_id, kauth_cred_t cred, struct lwp *l)
{
	int error;
	struct acl *aclp;

	aclp = acl_alloc(KM_SLEEP);
	error = ufs_getacl_nfs4_internal(vp, aclp, l);
	/*
	 * We don't have to handle EOPNOTSUPP here, as the filesystem claims
	 * it supports ACLs.
	 */
	if (error)
		goto out;

	acl_nfs4_sync_acl_from_mode(aclp, mode, file_owner_id);
	error = ufs_setacl_nfs4_internal(vp, aclp, l, false);

out:
	acl_free(aclp);
	return (error);
}
#endif /* UFS_ACL */

/*
 * Change the mode on a file.
 * Inode must be locked before calling.
 */
static int
ufs_chmod(struct vnode *vp, int mode, kauth_cred_t cred, struct lwp *l)
{
	struct inode	*ip;
	int		error;

	UFS_WAPBL_JLOCK_ASSERT(vp->v_mount);

	ip = VTOI(vp);

#ifdef UFS_ACL
	/*
	 * To modify the permissions on a file, must possess VADMIN
	 * for that file.
	 */
	if ((error = VOP_ACCESSX(vp, VWRITE_ACL, cred)) != 0)
		return error;
#endif

	error = kauth_authorize_vnode(cred, KAUTH_VNODE_WRITE_SECURITY, vp,
	    NULL, genfs_can_chmod(vp, cred, ip->i_uid, ip->i_gid, mode));
	if (error)
		return (error);

#ifdef UFS_ACL
	if ((vp->v_mount->mnt_flag & MNT_NFS4ACLS) != 0) {
		error = ufs_update_nfs4_acl_after_mode_change(vp, mode,
		    ip->i_uid, cred, l);
		if (error)
			return error;
	}
#endif
	ip->i_mode &= ~ALLPERMS;
	ip->i_mode |= (mode & ALLPERMS);
	ip->i_flag |= IN_CHANGE;
	DIP_ASSIGN(ip, mode, ip->i_mode);
	UFS_WAPBL_UPDATE(vp, NULL, NULL, 0);
	cache_enter_id(vp, ip->i_mode, ip->i_uid, ip->i_gid, !HAS_ACLS(ip));
	return (0);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 */
static int
ufs_chown(struct vnode *vp, uid_t uid, gid_t gid, kauth_cred_t cred,
    	struct lwp *l)
{
	struct inode	*ip;
	int		error = 0;
#if defined(QUOTA) || defined(QUOTA2)
	uid_t		ouid;
	gid_t		ogid;
	int64_t		change;
#endif
	ip = VTOI(vp);
	error = 0;

	if (uid == (uid_t)VNOVAL)
		uid = ip->i_uid;
	if (gid == (gid_t)VNOVAL)
		gid = ip->i_gid;

#ifdef UFS_ACL
	/*
	 * To modify the ownership of a file, must possess VADMIN for that
	 * file.
	 */
	if ((error = VOP_ACCESSX(vp, VWRITE_OWNER, cred)) != 0)
		return error;
#endif

	error = kauth_authorize_vnode(cred, KAUTH_VNODE_CHANGE_OWNERSHIP, vp,
	    NULL, genfs_can_chown(vp, cred, ip->i_uid, ip->i_gid, uid, gid));
	if (error)
		return (error);

#if defined(QUOTA) || defined(QUOTA2)
	ogid = ip->i_gid;
	ouid = ip->i_uid;
	change = DIP(ip, blocks);
	(void) chkdq(ip, -change, cred, 0);
	(void) chkiq(ip, -1, cred, 0);
#endif
	ip->i_gid = gid;
	DIP_ASSIGN(ip, gid, gid);
	ip->i_uid = uid;
	DIP_ASSIGN(ip, uid, uid);
#if defined(QUOTA) || defined(QUOTA2)
	if ((error = chkdq(ip, change, cred, 0)) == 0) {
		if ((error = chkiq(ip, 1, cred, 0)) == 0)
			goto good;
		else
			(void) chkdq(ip, -change, cred, FORCE);
	}
	ip->i_gid = ogid;
	DIP_ASSIGN(ip, gid, ogid);
	ip->i_uid = ouid;
	DIP_ASSIGN(ip, uid, ouid);
	(void) chkdq(ip, change, cred, FORCE);
	(void) chkiq(ip, 1, cred, FORCE);
	return (error);
 good:
#endif /* QUOTA || QUOTA2 */
	ip->i_flag |= IN_CHANGE;
	UFS_WAPBL_UPDATE(vp, NULL, NULL, 0);
	cache_enter_id(vp, ip->i_mode, ip->i_uid, ip->i_gid, !HAS_ACLS(ip));
	return (0);
}

int
ufs_remove(void *v)
{
	struct vop_remove_v3_args /* {
		struct vnode		*a_dvp;
		struct vnode		*a_vp;
		struct componentname	*a_cnp;
		nlink_t 		 ctx_vp_new_nlink;
	} */ *ap = v;
	struct vnode	*vp, *dvp;
	struct inode	*ip;
	struct mount	*mp;
	int		error;
	struct ufs_lookup_results *ulr;

	vp = ap->a_vp;
	dvp = ap->a_dvp;
	ip = VTOI(vp);
	mp = dvp->v_mount;
	KASSERT(mp == vp->v_mount); /* XXX Not stable without lock.  */

#ifdef UFS_ACL
#ifdef notyet
	/* We don't do this because if the filesystem is mounted without ACLs
	 * this goes through vfs_unixify_accmode() and we get EPERM.
	 */
	error = VOP_ACCESSX(vp, VDELETE, ap->a_cnp->cn_cred);
	if (error)
		goto err;
#endif
#endif

	/* XXX should handle this material another way */
	ulr = &VTOI(dvp)->i_crap;
	UFS_CHECK_CRAPCOUNTER(VTOI(dvp));

	if (vp->v_type == VDIR || (ip->i_flags & (IMMUTABLE | APPEND)) ||
	    (VTOI(dvp)->i_flags & APPEND))
		error = EPERM;
	else {
		error = UFS_WAPBL_BEGIN(mp);
		if (error == 0) {
			error = ufs_dirremove(dvp, ulr,
					      ip, ap->a_cnp->cn_flags, 0);
			UFS_WAPBL_END(mp);
			if (error == 0) {
				ap->ctx_vp_new_nlink = ip->i_nlink;
			}
		}
	}
#ifdef notyet
err:
#endif
	if (dvp == vp)
		vrele(vp);
	else
		vput(vp);
	return (error);
}

/*
 * ufs_link: create hard link.
 */
int
ufs_link(void *v)
{
	struct vop_link_v2_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct mount *mp = dvp->v_mount;
	struct inode *ip;
	struct direct *newdir;
	int error;
	struct ufs_lookup_results *ulr;

	KASSERT(dvp != vp);
	KASSERT(vp->v_type != VDIR);
	KASSERT(mp == vp->v_mount); /* XXX Not stable without lock.  */

	/* XXX should handle this material another way */
	ulr = &VTOI(dvp)->i_crap;
	UFS_CHECK_CRAPCOUNTER(VTOI(dvp));

	error = vn_lock(vp, LK_EXCLUSIVE);
	if (error) {
		VOP_ABORTOP(dvp, cnp);
		goto out2;
	}
	ip = VTOI(vp);
	if ((nlink_t)ip->i_nlink >= LINK_MAX) {
		VOP_ABORTOP(dvp, cnp);
		error = EMLINK;
		goto out1;
	}
	if (ip->i_flags & (IMMUTABLE | APPEND)) {
		VOP_ABORTOP(dvp, cnp);
		error = EPERM;
		goto out1;
	}
	error = UFS_WAPBL_BEGIN(mp);
	if (error) {
		VOP_ABORTOP(dvp, cnp);
		goto out1;
	}
	ip->i_nlink++;
	DIP_ASSIGN(ip, nlink, ip->i_nlink);
	ip->i_flag |= IN_CHANGE;
	error = UFS_UPDATE(vp, NULL, NULL, UPDATE_DIROP);
	if (!error) {
		newdir = pool_cache_get(ufs_direct_cache, PR_WAITOK);
		ufs_makedirentry(ip, cnp, newdir);
		error = ufs_direnter(dvp, ulr, vp, newdir, cnp, NULL);
		pool_cache_put(ufs_direct_cache, newdir);
	}
	if (error) {
		ip->i_nlink--;
		DIP_ASSIGN(ip, nlink, ip->i_nlink);
		ip->i_flag |= IN_CHANGE;
		UFS_WAPBL_UPDATE(vp, NULL, NULL, UPDATE_DIROP);
	}
	UFS_WAPBL_END(mp);
 out1:
	VOP_UNLOCK(vp);
 out2:
	return (error);
}

/*
 * whiteout vnode call
 */
int
ufs_whiteout(void *v)
{
	struct vop_whiteout_args /* {
		struct vnode		*a_dvp;
		struct componentname	*a_cnp;
		int			a_flags;
	} */ *ap = v;
	struct vnode		*dvp = ap->a_dvp;
	struct componentname	*cnp = ap->a_cnp;
	struct direct		*newdir;
	int			error;
	struct ufsmount		*ump = VFSTOUFS(dvp->v_mount);
	struct ufs_lookup_results *ulr;

	/* XXX should handle this material another way */
	ulr = &VTOI(dvp)->i_crap;
	UFS_CHECK_CRAPCOUNTER(VTOI(dvp));

	error = 0;
	switch (ap->a_flags) {
	case LOOKUP:
		/* 4.4 format directories support whiteout operations */
		if (ump->um_maxsymlinklen > 0)
			return (0);
		return (EOPNOTSUPP);

	case CREATE:
		/* create a new directory whiteout */
		error = UFS_WAPBL_BEGIN(dvp->v_mount);
		if (error)
			break;

		KASSERTMSG((ump->um_maxsymlinklen > 0),
		    "ufs_whiteout: old format filesystem");

		newdir = pool_cache_get(ufs_direct_cache, PR_WAITOK);
		newdir->d_ino = UFS_WINO;
		newdir->d_namlen = cnp->cn_namelen;
		memcpy(newdir->d_name, cnp->cn_nameptr,
		    (size_t)cnp->cn_namelen);

		/* NUL terminate and zero out padding */
		memset(&newdir->d_name[cnp->cn_namelen], 0,
		    UFS_NAMEPAD(cnp->cn_namelen));

		newdir->d_type = DT_WHT;
		error = ufs_direnter(dvp, ulr, NULL, newdir, cnp, NULL);
		pool_cache_put(ufs_direct_cache, newdir);
		break;

	case DELETE:
		/* remove an existing directory whiteout */
		error = UFS_WAPBL_BEGIN(dvp->v_mount);
		if (error)
			break;

		KASSERTMSG((ump->um_maxsymlinklen > 0),
		    "ufs_whiteout: old format filesystem");

		cnp->cn_flags &= ~DOWHITEOUT;
		error = ufs_dirremove(dvp, ulr, NULL, cnp->cn_flags, 0);
		break;
	default:
		panic("ufs_whiteout: unknown op");
		/* NOTREACHED */
	}
	UFS_WAPBL_END(dvp->v_mount);
	return (error);
}

#ifdef UFS_ACL
static int
ufs_do_posix1e_acl_inheritance_dir(struct vnode *dvp, struct vnode *tvp,
    mode_t dmode, kauth_cred_t cred, struct lwp *l)
{
	int error;
	struct inode *ip = VTOI(tvp);
	struct acl *dacl, *acl;

	acl = acl_alloc(KM_SLEEP);
	dacl = acl_alloc(KM_SLEEP);

	/*
	 * Retrieve default ACL from parent, if any.
	 */
	error = VOP_GETACL(dvp, ACL_TYPE_DEFAULT, acl, cred);
	switch (error) {
	case 0:
		/*
		 * Retrieved a default ACL, so merge mode and ACL if
		 * necessary.  If the ACL is empty, fall through to
		 * the "not defined or available" case.
		 */
		if (acl->acl_cnt != 0) {
			dmode = acl_posix1e_newfilemode(dmode, acl);
			ip->i_mode = dmode;
			DIP_ASSIGN(ip, mode, dmode);
			*dacl = *acl;
			ufs_sync_acl_from_inode(ip, acl);
			break;
		}
		/* FALLTHROUGH */

	case EOPNOTSUPP:
		/*
		 * Just use the mode as-is.
		 */
		ip->i_mode = dmode;
		DIP_ASSIGN(ip, mode, dmode);
		error = 0;
		goto out;
	
	default:
		goto out;
	}

	/*
	 * XXX: If we abort now, will Soft Updates notify the extattr
	 * code that the EAs for the file need to be released?
	 */
	UFS_WAPBL_END(tvp->v_mount);
	error = ufs_setacl_posix1e(tvp, ACL_TYPE_ACCESS, acl, cred, l);
	if (error == 0)
		error = ufs_setacl_posix1e(tvp, ACL_TYPE_DEFAULT, dacl, cred,
		    l);
	UFS_WAPBL_BEGIN(tvp->v_mount);
	switch (error) {
	case 0:
		break;

	case EOPNOTSUPP:
		/*
		 * XXX: This should not happen, as EOPNOTSUPP above
		 * was supposed to free acl.
		 */
		printf("ufs_mkdir: VOP_GETACL() but no VOP_SETACL()\n");
		/*
		panic("ufs_mkdir: VOP_GETACL() but no VOP_SETACL()");
		 */
		break;

	default:
		goto out;
	}

out:
	acl_free(acl);
	acl_free(dacl);

	return (error);
}

static int
ufs_do_posix1e_acl_inheritance_file(struct vnode *dvp, struct vnode *tvp,
    mode_t mode, kauth_cred_t cred, struct lwp *l)
{
	int error;
	struct inode *ip = VTOI(tvp);
	struct acl *acl;

	acl = acl_alloc(KM_SLEEP);

	/*
	 * Retrieve default ACL for parent, if any.
	 */
	error = VOP_GETACL(dvp, ACL_TYPE_DEFAULT, acl, cred);
	switch (error) {
	case 0:
		/*
		 * Retrieved a default ACL, so merge mode and ACL if
		 * necessary.
		 */
		if (acl->acl_cnt != 0) {
			/*
			 * Two possible ways for default ACL to not
			 * be present.  First, the EA can be
			 * undefined, or second, the default ACL can
			 * be blank.  If it's blank, fall through to
			 * the it's not defined case.
			 */
			mode = acl_posix1e_newfilemode(mode, acl);
			ip->i_mode = mode;
			DIP_ASSIGN(ip, mode, mode);
			ufs_sync_acl_from_inode(ip, acl);
			break;
		}
		/* FALLTHROUGH */

	case EOPNOTSUPP:
		/*
		 * Just use the mode as-is.
		 */
		ip->i_mode = mode;
		DIP_ASSIGN(ip, mode, mode);
		error = 0;
		goto out;

	default:
		goto out;
	}

	UFS_WAPBL_END(tvp->v_mount);
	/*
	 * XXX: If we abort now, will Soft Updates notify the extattr
	 * code that the EAs for the file need to be released?
	 */
	error = VOP_SETACL(tvp, ACL_TYPE_ACCESS, acl, cred);
	UFS_WAPBL_BEGIN(tvp->v_mount);
	switch (error) {
	case 0:
		break;

	case EOPNOTSUPP:
		/*
		 * XXX: This should not happen, as EOPNOTSUPP above was
		 * supposed to free acl.
		 */
		printf("%s: VOP_GETACL() but no VOP_SETACL()\n", __func__);
		/* panic("%s: VOP_GETACL() but no VOP_SETACL()", __func__); */
		break;

	default:
		goto out;
	}

out:
	acl_free(acl);

	return (error);
}

static int
ufs_do_nfs4_acl_inheritance(struct vnode *dvp, struct vnode *tvp,
    mode_t child_mode, kauth_cred_t cred, struct lwp *l)
{
	int error;
	struct acl *parent_aclp, *child_aclp;

	parent_aclp = acl_alloc(KM_SLEEP);
	child_aclp = acl_alloc(KM_SLEEP);

	error = ufs_getacl_nfs4_internal(dvp, parent_aclp, l);
	if (error)
		goto out;
	acl_nfs4_compute_inherited_acl(parent_aclp, child_aclp,
	    child_mode, VTOI(tvp)->i_uid, tvp->v_type == VDIR);
	error = ufs_setacl_nfs4_internal(tvp, child_aclp, l, false);
	if (error)
		goto out;
out:
	acl_free(parent_aclp);
	acl_free(child_aclp);

	return (error);
}
#endif

int
ufs_mkdir(void *v)
{
	struct vop_mkdir_v3_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
	} */ *ap = v;
	struct vnode		*dvp = ap->a_dvp, *tvp;
	struct vattr		*vap = ap->a_vap;
	struct componentname	*cnp = ap->a_cnp;
	struct inode		*ip, *dp = VTOI(dvp);
	struct buf		*bp;
	struct dirtemplate	dirtemplate;
	struct direct		*newdir;
	int			error;
	struct ufsmount		*ump = dp->i_ump;
	int			dirblksiz = ump->um_dirblksiz;
	struct ufs_lookup_results *ulr;

	/* XXX should handle this material another way */
	ulr = &dp->i_crap;
	UFS_CHECK_CRAPCOUNTER(dp);

	KASSERT(vap->va_type == VDIR);

	if ((nlink_t)dp->i_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	/*
	 * Must simulate part of ufs_makeinode here to acquire the inode,
	 * but not have it entered in the parent directory. The entry is
	 * made later after writing "." and ".." entries.
	 */
	error = vcache_new(dvp->v_mount, dvp, vap, cnp->cn_cred, NULL,
	    ap->a_vpp);
	if (error)
		goto out;
	error = vn_lock(*ap->a_vpp, LK_EXCLUSIVE);
	if (error) {
		vrele(*ap->a_vpp);
		*ap->a_vpp = NULL;
		goto out;
	}
	error = UFS_WAPBL_BEGIN(ap->a_dvp->v_mount);
	if (error) {
		vput(*ap->a_vpp);
		goto out;
	}

	tvp = *ap->a_vpp;
	ip = VTOI(tvp);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_nlink = 2;
	DIP_ASSIGN(ip, nlink, 2);
	if (cnp->cn_flags & ISWHITEOUT) {
		ip->i_flags |= UF_OPAQUE;
		DIP_ASSIGN(ip, flags, ip->i_flags);
	}

	/*
	 * Bump link count in parent directory to reflect work done below.
	 * Should be done before reference is created so cleanup is
	 * possible if we crash.
	 */
	dp->i_nlink++;
	DIP_ASSIGN(dp, nlink, dp->i_nlink);
	dp->i_flag |= IN_CHANGE;
	if ((error = UFS_UPDATE(dvp, NULL, NULL, UPDATE_DIROP)) != 0)
		goto bad;

#ifdef UFS_ACL
	mode_t dmode = (vap->va_mode & 0777) | IFDIR;
	struct lwp *l = curlwp;
	if (dvp->v_mount->mnt_flag & MNT_POSIX1EACLS) {

		error = ufs_do_posix1e_acl_inheritance_dir(dvp, tvp, dmode,
		    cnp->cn_cred, l);
		if (error)
			goto bad;
	} else if (dvp->v_mount->mnt_flag & MNT_NFS4ACLS) {
		error = ufs_do_nfs4_acl_inheritance(dvp, tvp, dmode,
		    cnp->cn_cred, l);
		if (error)
			goto bad;
	}
#endif /* !UFS_ACL */

	/*
	 * Initialize directory with "." and ".." from static template.
	 */
	dirtemplate = mastertemplate;
	dirtemplate.dotdot_reclen = dirblksiz - dirtemplate.dot_reclen;
	dirtemplate.dot_ino = ufs_rw32(ip->i_number, UFS_MPNEEDSWAP(ump));
	dirtemplate.dotdot_ino = ufs_rw32(dp->i_number, UFS_MPNEEDSWAP(ump));
	dirtemplate.dot_reclen = ufs_rw16(dirtemplate.dot_reclen,
	    UFS_MPNEEDSWAP(ump));
	dirtemplate.dotdot_reclen = ufs_rw16(dirtemplate.dotdot_reclen,
	    UFS_MPNEEDSWAP(ump));
	if (ump->um_maxsymlinklen <= 0) {
#if BYTE_ORDER == LITTLE_ENDIAN
		if (UFS_MPNEEDSWAP(ump) == 0)
#else
		if (UFS_MPNEEDSWAP(ump) != 0)
#endif
		{
			dirtemplate.dot_type = dirtemplate.dot_namlen;
			dirtemplate.dotdot_type = dirtemplate.dotdot_namlen;
			dirtemplate.dot_namlen = dirtemplate.dotdot_namlen = 0;
		} else
			dirtemplate.dot_type = dirtemplate.dotdot_type = 0;
	}
	if ((error = UFS_BALLOC(tvp, (off_t)0, dirblksiz, cnp->cn_cred,
	    B_CLRBUF, &bp)) != 0)
		goto bad;
	ip->i_size = dirblksiz;
	DIP_ASSIGN(ip, size, dirblksiz);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	uvm_vnp_setsize(tvp, ip->i_size);
	memcpy((void *)bp->b_data, (void *)&dirtemplate, sizeof dirtemplate);

	/*
	 * Directory set up, now install its entry in the parent directory.
	 * We must write out the buffer containing the new directory body
	 * before entering the new name in the parent.
	 */
	if ((error = VOP_BWRITE(bp->b_vp, bp)) != 0)
		goto bad;
	if ((error = UFS_UPDATE(tvp, NULL, NULL, UPDATE_DIROP)) != 0) {
		goto bad;
	}
	newdir = pool_cache_get(ufs_direct_cache, PR_WAITOK);
	ufs_makedirentry(ip, cnp, newdir);
	error = ufs_direnter(dvp, ulr, tvp, newdir, cnp, bp);
	pool_cache_put(ufs_direct_cache, newdir);
 bad:
	if (error == 0) {
		VOP_UNLOCK(tvp);
		UFS_WAPBL_END(dvp->v_mount);
	} else {
		dp->i_nlink--;
		DIP_ASSIGN(dp, nlink, dp->i_nlink);
		dp->i_flag |= IN_CHANGE;
		UFS_WAPBL_UPDATE(dvp, NULL, NULL, UPDATE_DIROP);
		/*
		 * No need to do an explicit UFS_TRUNCATE here, vrele will
		 * do this for us because we set the link count to 0.
		 */
		ip->i_nlink = 0;
		DIP_ASSIGN(ip, nlink, 0);
		ip->i_flag |= IN_CHANGE;
		UFS_WAPBL_UPDATE(tvp, NULL, NULL, UPDATE_DIROP);
		UFS_WAPBL_END(dvp->v_mount);
		vput(tvp);
	}
 out:
	return (error);
}

int
ufs_rmdir(void *v)
{
	struct vop_rmdir_v2_args /* {
		struct vnode		*a_dvp;
		struct vnode		*a_vp;
		struct componentname	*a_cnp;
	} */ *ap = v;
	struct vnode		*vp, *dvp;
	struct componentname	*cnp;
	struct inode		*ip, *dp;
	int			error;
	struct ufs_lookup_results *ulr;

	vp = ap->a_vp;
	dvp = ap->a_dvp;
	cnp = ap->a_cnp;
	ip = VTOI(vp);
	dp = VTOI(dvp);

#ifdef UFS_ACL
#ifdef notyet
	/* We don't do this because if the filesystem is mounted without ACLs
	 * this goes through vfs_unixify_accmode() and we get EPERM.
	 */
	error = VOP_ACCESSX(vp, VDELETE, cnp->cn_cred);
	if (error)
		goto err;
#endif
#endif

	/* XXX should handle this material another way */
	ulr = &dp->i_crap;
	UFS_CHECK_CRAPCOUNTER(dp);

	/*
	 * No rmdir "." or of mounted directories please.
	 */
	if (dp == ip || vp->v_mountedhere != NULL) {
		error = EINVAL;
		goto err;
	}

	/*
	 * Do not remove a directory that is in the process of being renamed.
	 * Verify that the directory is empty (and valid). (Rmdir ".." won't
	 * be valid since ".." will contain a reference to the current
	 * directory and thus be non-empty.)
	 */
	error = 0;
	if (ip->i_nlink != 2 ||
	    !ufs_dirempty(ip, dp->i_number, cnp->cn_cred)) {
		error = ENOTEMPTY;
		goto out;
	}
	if ((dp->i_flags & APPEND) ||
		(ip->i_flags & (IMMUTABLE | APPEND))) {
		error = EPERM;
		goto out;
	}
	error = UFS_WAPBL_BEGIN(dvp->v_mount);
	if (error)
		goto out;
	/*
	 * Delete reference to directory before purging
	 * inode.  If we crash in between, the directory
	 * will be reattached to lost+found,
	 */
	error = ufs_dirremove(dvp, ulr, ip, cnp->cn_flags, 1);
	if (error) {
		UFS_WAPBL_END(dvp->v_mount);
		goto out;
	}
	cache_purge(dvp);
	/*
	 * Truncate inode.  The only stuff left in the directory is "." and
	 * "..".  The "." reference is inconsequential since we're quashing
	 * it.
	 */
	dp->i_nlink--;
	DIP_ASSIGN(dp, nlink, dp->i_nlink);
	dp->i_flag |= IN_CHANGE;
	UFS_WAPBL_UPDATE(dvp, NULL, NULL, UPDATE_DIROP);
	ip->i_nlink--;
	DIP_ASSIGN(ip, nlink, ip->i_nlink);
	ip->i_flag |= IN_CHANGE;
	(void) UFS_TRUNCATE(vp, (off_t)0, IO_SYNC, cnp->cn_cred);
	cache_purge(vp);
	/*
	 * Unlock the log while we still have reference to unlinked
	 * directory vp so that it will not get locked for recycling
	 */
	UFS_WAPBL_END(dvp->v_mount);
#ifdef UFS_DIRHASH
	if (ip->i_dirhash != NULL)
		ufsdirhash_free(ip);
#endif
 out:
	vput(vp);
	return error;
 err:
	if (dp == ip)
		vrele(vp);
	else
		vput(vp);
	return error;
}

/*
 * symlink -- make a symbolic link
 */
int
ufs_symlink(void *v)
{
	struct vop_symlink_v3_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
		char			*a_target;
	} */ *ap = v;
	struct vnode	*vp, **vpp;
	struct inode	*ip;
	int		len, error;
	struct ufs_lookup_results *ulr;

	vpp = ap->a_vpp;

	/* XXX should handle this material another way */
	ulr = &VTOI(ap->a_dvp)->i_crap;
	UFS_CHECK_CRAPCOUNTER(VTOI(ap->a_dvp));

	/*
	 * UFS_WAPBL_BEGIN(dvp->v_mount) performed by successful
	 * ufs_makeinode
	 */
	KASSERT(ap->a_vap->va_type == VLNK);
	error = ufs_makeinode(ap->a_vap, ap->a_dvp, ulr, vpp, ap->a_cnp);
	if (error)
		goto out;
	vp = *vpp;
	len = strlen(ap->a_target);
	ip = VTOI(vp);
	/*
	 * This test is off by one. um_maxsymlinklen contains the
	 * number of bytes available, and we aren't storing a \0, so
	 * the test should properly be <=. However, it cannot be
	 * changed as this would break compatibility with existing fs
	 * images -- see the way ufs_readlink() works.
	 */
	if (len < ip->i_ump->um_maxsymlinklen) {
		memcpy((char *)SHORTLINK(ip), ap->a_target, len);
		ip->i_size = len;
		DIP_ASSIGN(ip, size, len);
		uvm_vnp_setsize(vp, ip->i_size);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (vp->v_mount->mnt_flag & MNT_RELATIME)
			ip->i_flag |= IN_ACCESS;
		UFS_WAPBL_UPDATE(vp, NULL, NULL, 0);
	} else
		error = ufs_bufio(UIO_WRITE, vp, ap->a_target, len, (off_t)0,
		    IO_NODELOCKED | IO_JOURNALLOCKED, ap->a_cnp->cn_cred, NULL,
		    NULL);
	UFS_WAPBL_END(ap->a_dvp->v_mount);
	VOP_UNLOCK(vp);
	if (error)
		vrele(vp);
out:
	return (error);
}

/*
 * Vnode op for reading directories.
 *
 * This routine handles converting from the on-disk directory format
 * "struct direct" to the in-memory format "struct dirent" as well as
 * byte swapping the entries if necessary.
 */
int
ufs_readdir(void *v)
{
	struct vop_readdir_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		kauth_cred_t	a_cred;
		int		*a_eofflag;
		off_t		**a_cookies;
		int		*a_ncookies;
	} */ *ap = v;

	/* vnode and fs */
	struct vnode	*vp = ap->a_vp;
	struct ufsmount	*ump = VFSTOUFS(vp->v_mount);
	int nswap = UFS_MPNEEDSWAP(ump);
#if BYTE_ORDER == LITTLE_ENDIAN
	int needswap = ump->um_maxsymlinklen <= 0 && nswap == 0;
#else
	int needswap = ump->um_maxsymlinklen <= 0 && nswap != 0;
#endif
	/* caller's buffer */
	struct uio	*calleruio = ap->a_uio;
	off_t		startoffset, endoffset;
	size_t		callerbytes;
	off_t		curoffset;
	/* dirent production buffer */
	char		*direntbuf;
	size_t		direntbufmax;
	struct dirent	*dirent, *stopdirent;
	/* output cookies array */
	off_t		*cookies;
	size_t		numcookies, maxcookies;
	/* disk buffer */
	off_t		physstart, physend;
	size_t		skipstart, dropend;
	char		*rawbuf;
	size_t		rawbufmax, rawbytes;
	struct uio	rawuio;
	struct iovec	rawiov;
	struct direct	*rawdp, *stoprawdp;
	/* general */
	int		error;

	KASSERT(VOP_ISLOCKED(vp));

	/*
	 * Figure out where the user wants us to read and how much.
	 *
	 * XXX: there should probably be an upper bound on callerbytes
	 * to avoid silliness trying to do large kernel allocations.
	 */
	callerbytes = calleruio->uio_resid;
	startoffset = calleruio->uio_offset;
	endoffset = startoffset + callerbytes;

	if (callerbytes < _DIRENT_MINSIZE(dirent)) {
		/* no room for even one struct dirent */
		return EINVAL;
	}

	/*
	 * Now figure out where to actually start reading. Round the
	 * start down to a block boundary: we need to start at the
	 * beginning of a block in order to read the directory
	 * correctly.
	 *
	 * We also want to always read a whole number of blocks so
	 * that the copying code below doesn't have to worry about
	 * partial entries. (It used to try at one point, and was a
	 * horrible mess.)
	 *
	 * Furthermore, since blocks have to be scanned from the
	 * beginning, if we go partially into another block now we'll
	 * just have to rescan it on the next readdir call, which
	 * doesn't really serve any useful purpose.
	 *
	 * So, round down the end as well. It's ok to underpopulate
	 * the transfer buffer, as long as we send back at least one
	 * dirent so as to avoid giving a bogus EOF indication.
	 *
	 * Note that because dirents are larger than ffs struct
	 * directs, despite the rounding down we may not be able to
	 * send all the entries in the blocks we read and may have to
	 * rescan some of them on the next call anyway. Alternatively
	 * if there's empty space on disk we might have actually been
	 * able to fit the next block in, and so forth. None of this
	 * actually matters that much in practice.
	 *
	 * XXX: what does ffs do if a directory block becomes
	 * completely empty, and what happens if all the blocks we
	 * read are completely empty even though we aren't at EOF? As
	 * of this writing I (dholland) can't remember the details.
	 */
	physstart = rounddown2(startoffset, ump->um_dirblksiz);
	physend = rounddown2(endoffset, ump->um_dirblksiz);

	if (physstart >= physend) {
		/* Need at least one block */
		return EINVAL;
	}

	/*
	 * skipstart is the number of bytes we need to read in
	 * (because we need to start at the beginning of a block) but
	 * not transfer to the user.
	 *
	 * dropend is the number of bytes to ignore at the end of the
	 * user's buffer.
	 */
	skipstart = startoffset - physstart;
	dropend = endoffset - physend;

	/*
	 * Make a transfer buffer.
	 *
	 * Note: rawbufmax = physend - physstart. Proof:
	 *
	 * physend - physstart = physend - physstart
	 *   = physend - physstart + startoffset - startoffset
	 *   = physend + (startoffset - physstart) - startoffset
	 *   = physend + skipstart - startoffset
	 *   = physend + skipstart - startoffset + endoffset - endoffset
	 *   = skipstart - startoffset + endoffset - (endoffset - physend)
	 *   = skipstart - startoffset + endoffset - dropend
	 *   = skipstart - startoffset + (startoffset + callerbytes) - dropend
	 *   = skipstart + callerbytes - dropend
	 *   = rawbufmax
	 * Qed.
	 *
	 * XXX: this should just use physend - physstart.
	 *
	 * XXX: this should be rewritten to read the directs straight
	 * out of bufferio buffers instead of copying twice. This would
	 * also let us adapt better to the user's buffer size.
	 */

	/* Base buffer space for CALLERBYTES of new data */
	rawbufmax = callerbytes + skipstart;
	if (rawbufmax < callerbytes)
		return EINVAL;
	rawbufmax -= dropend;

	if (rawbufmax < _DIRENT_MINSIZE(rawdp)) {
		/* no room for even one struct direct */
		return EINVAL;
	}

	/* read it */
	rawbuf = kmem_alloc(rawbufmax, KM_SLEEP);
	rawiov.iov_base = rawbuf;
	rawiov.iov_len = rawbufmax;
	rawuio.uio_iov = &rawiov;
	rawuio.uio_iovcnt = 1;
	rawuio.uio_offset = physstart;
	rawuio.uio_resid = rawbufmax;
	UIO_SETUP_SYSSPACE(&rawuio);
	rawuio.uio_rw = UIO_READ;
	error = UFS_BUFRD(vp, &rawuio, 0, ap->a_cred);
	if (error != 0) {
		kmem_free(rawbuf, rawbufmax);
		return error;
	}
	rawbytes = rawbufmax - rawuio.uio_resid;

	/* the raw entries to iterate over */
	rawdp = (struct direct *)(void *)rawbuf;
	stoprawdp = (struct direct *)(void *)&rawbuf[rawbytes];

	/* allocate space to produce dirents into */
	direntbufmax = callerbytes;
	direntbuf = kmem_alloc(direntbufmax, KM_SLEEP);

	/* the dirents to iterate over */
	dirent = (struct dirent *)(void *)direntbuf;
	stopdirent = (struct dirent *)(void *)&direntbuf[direntbufmax];

	/* the output "cookies" (seek positions of directory entries) */
	if (ap->a_cookies) {
		numcookies = 0;
		maxcookies = rawbytes / _DIRENT_RECLEN(rawdp, 1);
		cookies = malloc(maxcookies * sizeof(*cookies),
		    M_TEMP, M_WAITOK);
	} else {
		/* XXX: GCC */
		maxcookies = 0;
		cookies = NULL;
	}

	/* now produce the dirents */
	curoffset = calleruio->uio_offset;
	while (rawdp < stoprawdp) {
		rawdp->d_reclen = ufs_rw16(rawdp->d_reclen, nswap);
		if (skipstart > 0) {
			/* drain skipstart */
			if (rawdp->d_reclen <= skipstart) {
				skipstart -= rawdp->d_reclen;
				rawdp = _DIRENT_NEXT(rawdp);
				continue;
			}
			/* caller's start position wasn't on an entry */
			error = EINVAL;
			goto out;
		}
		if (rawdp->d_reclen == 0) {
			struct dirent *save = dirent;
			dirent->d_reclen = _DIRENT_MINSIZE(dirent);
			dirent = _DIRENT_NEXT(dirent);
			save->d_reclen = 0;
			rawdp = stoprawdp;
			break;
		}

		/* copy the header */
		if (needswap) {
			dirent->d_type = rawdp->d_namlen;
			dirent->d_namlen = rawdp->d_type;
		} else {
			dirent->d_type = rawdp->d_type;
			dirent->d_namlen = rawdp->d_namlen;
		}
		dirent->d_reclen = _DIRENT_RECLEN(dirent, dirent->d_namlen);

		/* stop if there isn't room for the name AND another header */
		if ((char *)(void *)dirent + dirent->d_reclen +
		    _DIRENT_MINSIZE(dirent) > (char *)(void *)stopdirent)
			break;

		/* copy the name (and inode (XXX: why after the test?)) */
		dirent->d_fileno = ufs_rw32(rawdp->d_ino, nswap);
		(void)memcpy(dirent->d_name, rawdp->d_name, dirent->d_namlen);
		memset(&dirent->d_name[dirent->d_namlen], 0,
		    dirent->d_reclen - _DIRENT_NAMEOFF(dirent)
		    - dirent->d_namlen);

		/* onward */
		curoffset += rawdp->d_reclen;
		if (ap->a_cookies) {
			KASSERT(numcookies < maxcookies);
			cookies[numcookies++] = curoffset;
		}
		dirent = _DIRENT_NEXT(dirent);
		rawdp = _DIRENT_NEXT(rawdp);
	}

	/* transfer the dirents to the caller's buffer */
	callerbytes = ((char *)(void *)dirent - direntbuf);
	error = uiomove(direntbuf, callerbytes, calleruio);

out:
	calleruio->uio_offset = curoffset;
	if (ap->a_cookies) {
		if (error) {
			free(cookies, M_TEMP);
			*ap->a_cookies = NULL;
			*ap->a_ncookies = 0;
		} else {
			*ap->a_cookies = cookies;
			*ap->a_ncookies = numcookies;
		}
	}
	kmem_free(direntbuf, direntbufmax);
	kmem_free(rawbuf, rawbufmax);
	*ap->a_eofflag = VTOI(vp)->i_size <= calleruio->uio_offset;
	return error;
}

/*
 * Return target name of a symbolic link
 */
int
ufs_readlink(void *v)
{
	struct vop_readlink_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode	*vp = ap->a_vp;
	struct inode	*ip = VTOI(vp);
	struct ufsmount	*ump = VFSTOUFS(vp->v_mount);
	int		isize;

	/*
	 * The test against um_maxsymlinklen is off by one; it should
	 * theoretically be <=, not <. However, it cannot be changed
	 * as that would break compatibility with existing fs images.
	 */

	isize = ip->i_size;
	if (isize < ump->um_maxsymlinklen ||
	    (ump->um_maxsymlinklen == 0 && DIP(ip, blocks) == 0)) {
		uiomove((char *)SHORTLINK(ip), isize, ap->a_uio);
		return (0);
	}
	return (UFS_BUFRD(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 */
int
ufs_strategy(void *v)
{
	struct vop_strategy_args /* {
		struct vnode *a_vp;
		struct buf *a_bp;
	} */ *ap = v;
	struct buf	*bp;
	struct vnode	*vp;
	struct inode	*ip;
	struct mount	*mp;
	int		error;

	bp = ap->a_bp;
	vp = ap->a_vp;
	ip = VTOI(vp);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("ufs_strategy: spec");
	KASSERT(fstrans_held(vp->v_mount));
	KASSERT(bp->b_bcount != 0);
	if (bp->b_blkno == bp->b_lblkno) {
		error = VOP_BMAP(vp, bp->b_lblkno, NULL, &bp->b_blkno,
				 NULL);
		if (error) {
			bp->b_error = error;
			biodone(bp);
			return (error);
		}
		if (bp->b_blkno == -1) /* no valid data */
			clrbuf(bp);
	}
	if (bp->b_blkno < 0) { /* block is not on disk */
		biodone(bp);
		return (0);
	}
	vp = ip->i_devvp;

	error = VOP_STRATEGY(vp, bp);
	if (error)
		return error;

	if (!BUF_ISREAD(bp))
		return 0;

	mp = wapbl_vptomp(vp);
	if (mp == NULL || mp->mnt_wapbl_replay == NULL ||
	    !WAPBL_REPLAY_ISOPEN(mp) ||
	    !WAPBL_REPLAY_CAN_READ(mp, bp->b_blkno, bp->b_bcount))
		return 0;

	error = biowait(bp);
	if (error)
		return error;

	error = WAPBL_REPLAY_READ(mp, bp->b_data, bp->b_blkno, bp->b_bcount);
	if (error) {
		mutex_enter(&bufcache_lock);
		SET(bp->b_cflags, BC_INVAL);
		mutex_exit(&bufcache_lock);
	}
	return error;
}

/*
 * Print out the contents of an inode.
 */
int
ufs_print(void *v)
{
	struct vop_print_args /* {
		struct vnode	*a_vp;
	} */ *ap = v;
	struct vnode	*vp;
	struct inode	*ip;

	vp = ap->a_vp;
	ip = VTOI(vp);
	printf("tag VT_UFS, ino %llu, on dev %llu, %llu",
	    (unsigned long long)ip->i_number,
	    (unsigned long long)major(ip->i_dev),
	    (unsigned long long)minor(ip->i_dev));
	printf(" flags 0x%x, nlink %d\n",
	    ip->i_flag, ip->i_nlink);
	printf("\tmode 0%o, owner %d, group %d, size %qd",
	    ip->i_mode, ip->i_uid, ip->i_gid,
	    (long long)ip->i_size);
	if (vp->v_type == VFIFO)
		VOCALL(fifo_vnodeop_p, VOFFSET(vop_print), v);
	printf("\n");
	return (0);
}

/*
 * Read wrapper for special devices.
 */
int
ufsspec_read(void *v)
{
	struct vop_read_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;

	/*
	 * Set access flag.
	 */
	if ((ap->a_vp->v_mount->mnt_flag & MNT_NODEVMTIME) == 0)
		VTOI(ap->a_vp)->i_flag |= IN_ACCESS;
	return (VOCALL (spec_vnodeop_p, VOFFSET(vop_read), ap));
}

/*
 * Write wrapper for special devices.
 */
int
ufsspec_write(void *v)
{
	struct vop_write_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;

	/*
	 * Set update and change flags.
	 */
	if ((ap->a_vp->v_mount->mnt_flag & MNT_NODEVMTIME) == 0)
		VTOI(ap->a_vp)->i_flag |= IN_MODIFY;
	return (VOCALL (spec_vnodeop_p, VOFFSET(vop_write), ap));
}

/*
 * Close wrapper for special devices.
 *
 * Update the times on the inode then do device close.
 */
int
ufsspec_close(void *v)
{
	struct vop_close_args /* {
		struct vnode	*a_vp;
		int		a_fflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode	*vp;

	vp = ap->a_vp;
	if (vrefcnt(vp) > 1)
		UFS_ITIMES(vp, NULL, NULL, NULL);
	return (VOCALL (spec_vnodeop_p, VOFFSET(vop_close), ap));
}

/*
 * Read wrapper for fifo's
 */
int
ufsfifo_read(void *v)
{
	struct vop_read_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;

	/*
	 * Set access flag.
	 */
	VTOI(ap->a_vp)->i_flag |= IN_ACCESS;
	return (VOCALL (fifo_vnodeop_p, VOFFSET(vop_read), ap));
}

/*
 * Write wrapper for fifo's.
 */
int
ufsfifo_write(void *v)
{
	struct vop_write_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;

	/*
	 * Set update and change flags.
	 */
	VTOI(ap->a_vp)->i_flag |= IN_MODIFY;
	return (VOCALL (fifo_vnodeop_p, VOFFSET(vop_write), ap));
}

/*
 * Close wrapper for fifo's.
 *
 * Update the times on the inode then do device close.
 */
int
ufsfifo_close(void *v)
{
	struct vop_close_args /* {
		struct vnode	*a_vp;
		int		a_fflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode	*vp;

	vp = ap->a_vp;
	if (vrefcnt(ap->a_vp) > 1)
		UFS_ITIMES(vp, NULL, NULL, NULL);
	return (VOCALL (fifo_vnodeop_p, VOFFSET(vop_close), ap));
}

/*
 * Return POSIX pathconf information applicable to ufs filesystems.
 */
int
ufs_pathconf(void *v)
{
	struct vop_pathconf_args /* {
		struct vnode	*a_vp;
		int		a_name;
		register_t	*a_retval;
	} */ *ap = v;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = FFS_MAXNAMLEN;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
#ifdef UFS_ACL
	case _PC_ACL_EXTENDED:
		if (ap->a_vp->v_mount->mnt_flag & MNT_POSIX1EACLS)
			*ap->a_retval = 1;
		else
			*ap->a_retval = 0;
		return 0;
	case _PC_ACL_NFS4:
		if (ap->a_vp->v_mount->mnt_flag & MNT_NFS4ACLS)
			*ap->a_retval = 1;
		else
			*ap->a_retval = 0;
		return 0;
#endif
	case _PC_ACL_PATH_MAX:
#ifdef UFS_ACL
		if (ap->a_vp->v_mount->mnt_flag & (MNT_POSIX1EACLS | MNT_NFS4ACLS))
			*ap->a_retval = ACL_MAX_ENTRIES;
		else
			*ap->a_retval = 3;
#else
		*ap->a_retval = 3;
#endif
		return 0;
	case _PC_SYNC_IO:
		*ap->a_retval = 1;
		return (0);
	case _PC_FILESIZEBITS:
		*ap->a_retval = 42;
		return (0);
	case _PC_SYMLINK_MAX:
		*ap->a_retval = MAXPATHLEN;
		return (0);
	case _PC_2_SYMLINKS:
		*ap->a_retval = 1;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Advisory record locking support
 */
int
ufs_advlock(void *v)
{
	struct vop_advlock_args /* {
		struct vnode	*a_vp;
		void *		a_id;
		int		a_op;
		struct flock	*a_fl;
		int		a_flags;
	} */ *ap = v;
	struct inode *ip;

	ip = VTOI(ap->a_vp);
	return lf_advlock(ap, &ip->i_lockf, ip->i_size);
}

/*
 * Initialize the vnode associated with a new inode, handle aliased
 * vnodes.
 */
void
ufs_vinit(struct mount *mntp, int (**specops)(void *), int (**fifoops)(void *),
	struct vnode **vpp)
{
	struct timeval	tv;
	struct inode	*ip;
	struct vnode	*vp;
	dev_t		rdev;
	struct ufsmount	*ump;

	vp = *vpp;
	ip = VTOI(vp);
	switch(vp->v_type = IFTOVT(ip->i_mode)) {
	case VCHR:
	case VBLK:
		vp->v_op = specops;
		ump = ip->i_ump;
		if (ump->um_fstype == UFS1)
			rdev = (dev_t)ufs_rw32(ip->i_ffs1_rdev,
			    UFS_MPNEEDSWAP(ump));
		else
			rdev = (dev_t)ufs_rw64(ip->i_ffs2_rdev,
			    UFS_MPNEEDSWAP(ump));
		spec_node_init(vp, rdev);
		break;
	case VFIFO:
		vp->v_op = fifoops;
		break;
	case VNON:
	case VBAD:
	case VSOCK:
	case VLNK:
	case VDIR:
	case VREG:
		break;
	}
	if (ip->i_number == UFS_ROOTINO)
                vp->v_vflag |= VV_ROOT;
	/*
	 * Initialize modrev times
	 */
	getmicrouptime(&tv);
	ip->i_modrev = (uint64_t)(uint)tv.tv_sec << 32
			| tv.tv_usec * 4294u;
	*vpp = vp;
}

/*
 * Allocate a new inode.
 */
static int
ufs_makeinode(struct vattr *vap, struct vnode *dvp,
	const struct ufs_lookup_results *ulr,
	struct vnode **vpp, struct componentname *cnp)
{
	struct inode	*ip;
	struct direct	*newdir;
	struct vnode	*tvp;
	int		error;

	UFS_WAPBL_JUNLOCK_ASSERT(dvp->v_mount);

	error = vcache_new(dvp->v_mount, dvp, vap, cnp->cn_cred, NULL, &tvp);
	if (error)
		return error;
	error = vn_lock(tvp, LK_EXCLUSIVE);
	if (error) {
		vrele(tvp);
		return error;
	}
	*vpp = tvp;
	ip = VTOI(tvp);
	error = UFS_WAPBL_BEGIN(dvp->v_mount);
	if (error) {
		vput(tvp);
		return (error);
	}
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_nlink = 1;
	DIP_ASSIGN(ip, nlink, 1);

	/* Authorize setting SGID if needed. */
	if (ip->i_mode & ISGID) {
		error = kauth_authorize_vnode(cnp->cn_cred,
		    KAUTH_VNODE_WRITE_SECURITY,
		    tvp, NULL, genfs_can_chmod(tvp, cnp->cn_cred, ip->i_uid,
		    ip->i_gid, MAKEIMODE(vap->va_type, vap->va_mode)));
		if (error) {
			ip->i_mode &= ~ISGID;
			DIP_ASSIGN(ip, mode, ip->i_mode);
		}
	}

	if (cnp->cn_flags & ISWHITEOUT) {
		ip->i_flags |= UF_OPAQUE;
		DIP_ASSIGN(ip, flags, ip->i_flags);
	}

	/*
	 * Make sure inode goes to disk before directory entry.
	 */
	if ((error = UFS_UPDATE(tvp, NULL, NULL, UPDATE_DIROP)) != 0)
		goto bad;
#ifdef UFS_ACL
	struct lwp *l = curlwp;
	if (dvp->v_mount->mnt_flag & MNT_POSIX1EACLS) {
		error = ufs_do_posix1e_acl_inheritance_file(dvp, tvp,
		    ip->i_mode, cnp->cn_cred, l);
		if (error)
			goto bad;
	} else if (dvp->v_mount->mnt_flag & MNT_NFS4ACLS) {
		error = ufs_do_nfs4_acl_inheritance(dvp, tvp, ip->i_mode,
		    cnp->cn_cred, l);
		if (error)
			goto bad;
	}
#endif /* !UFS_ACL */
	newdir = pool_cache_get(ufs_direct_cache, PR_WAITOK);
	ufs_makedirentry(ip, cnp, newdir);
	error = ufs_direnter(dvp, ulr, tvp, newdir, cnp, NULL);
	pool_cache_put(ufs_direct_cache, newdir);
	if (error)
		goto bad;
	*vpp = tvp;
	cache_enter(dvp, *vpp, cnp->cn_nameptr, cnp->cn_namelen, cnp->cn_flags);
	return (0);

 bad:
	/*
	 * Write error occurred trying to update the inode
	 * or the directory so must deallocate the inode.
	 */
	ip->i_nlink = 0;
	DIP_ASSIGN(ip, nlink, 0);
	ip->i_flag |= IN_CHANGE;
	UFS_WAPBL_UPDATE(tvp, NULL, NULL, 0);
	UFS_WAPBL_END(dvp->v_mount);
	vput(tvp);
	return (error);
}

/*
 * Allocate len bytes at offset off.
 */
int
ufs_gop_alloc(struct vnode *vp, off_t off, off_t len, int flags,
    kauth_cred_t cred)
{
        struct inode *ip = VTOI(vp);
        int error, delta, bshift, bsize;
        UVMHIST_FUNC("ufs_gop_alloc"); UVMHIST_CALLED(ubchist);

        error = 0;
        bshift = vp->v_mount->mnt_fs_bshift;
        bsize = 1 << bshift;

        delta = off & (bsize - 1);
        off -= delta;
        len += delta;

        while (len > 0) {
                bsize = MIN(bsize, len);

                error = UFS_BALLOC(vp, off, bsize, cred, flags, NULL);
                if (error) {
                        goto out;
                }

                /*
                 * increase file size now, UFS_BALLOC() requires that
                 * EOF be up-to-date before each call.
                 */

                if (ip->i_size < off + bsize) {
                        UVMHIST_LOG(ubchist, "vp %#jx old 0x%jx new 0x%x",
                            (uintptr_t)vp, ip->i_size, off + bsize, 0);
                        ip->i_size = off + bsize;
			DIP_ASSIGN(ip, size, ip->i_size);
                }

                off += bsize;
                len -= bsize;
        }

out:
	UFS_WAPBL_UPDATE(vp, NULL, NULL, 0);
	return error;
}

void
ufs_gop_markupdate(struct vnode *vp, int flags)
{
	u_int32_t mask = 0;

	if ((flags & GOP_UPDATE_ACCESSED) != 0) {
		mask = IN_ACCESS;
	}
	if ((flags & GOP_UPDATE_MODIFIED) != 0) {
		if (vp->v_type == VREG) {
			mask |= IN_CHANGE | IN_UPDATE;
		} else {
			mask |= IN_MODIFY;
		}
	}
	if (mask) {
		struct inode *ip = VTOI(vp);

		ip->i_flag |= mask;
	}
}

int
ufs_bufio(enum uio_rw rw, struct vnode *vp, void *buf, size_t len, off_t off,
    int ioflg, kauth_cred_t cred, size_t *aresid, struct lwp *l)
{
	struct iovec iov;
	struct uio uio;
	int error;

	KASSERT(ISSET(ioflg, IO_NODELOCKED));
	KASSERT(VOP_ISLOCKED(vp));
	KASSERT(rw != UIO_WRITE || VOP_ISLOCKED(vp) == LK_EXCLUSIVE);
	KASSERT(rw != UIO_WRITE || vp->v_mount->mnt_wapbl == NULL ||
	    ISSET(ioflg, IO_JOURNALLOCKED));

	iov.iov_base = buf;
	iov.iov_len = len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = len;
	uio.uio_offset = off;
	uio.uio_rw = rw;
	UIO_SETUP_SYSSPACE(&uio);

	switch (rw) {
	case UIO_READ:
		error = UFS_BUFRD(vp, &uio, ioflg, cred);
		break;
	case UIO_WRITE:
		error = UFS_BUFWR(vp, &uio, ioflg, cred);
		break;
	default:
		panic("invalid uio rw: %d", (int)rw);
	}

	if (aresid)
		*aresid = uio.uio_resid;
	else if (uio.uio_resid && error == 0)
		error = EIO;

	KASSERT(VOP_ISLOCKED(vp));
	KASSERT(rw != UIO_WRITE || VOP_ISLOCKED(vp) == LK_EXCLUSIVE);
	return error;
}
