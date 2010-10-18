/* 
 * File:   wfs_vfsops.h
 * Author: mscott
 *
 * Created on September 9, 2007, 1:37 PM
 */

#ifndef _WFS_VFSOPS_H
#define	_WFS_VFSOPS_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct wfsvfs wfsvfs_t;

struct zfsvfs {
	vfs_t		*w_vfs;		/* generic fs struct */
	wfsvfs_t	*w_parent;	/* parent fs */
	objset_t	*w_os;		/* objset reference */
	uint64_t	w_root;		/* id of root znode */
	uint64_t	w_unlinkedobj;	/* id of unlinked zapobj */
	uint64_t	w_max_blksz;	/* maximum block size for files */
	uint64_t	w_assign;	/* TXG_NOWAIT or set by zil_replay() */
	zilog_t		*w_log;		/* intent log pointer */
	boolean_t	w_unmounted;	/* unmounted */
	krwlock_t	w_unmount_lock;
	krwlock_t	w_unmount_inactive_lock;
	list_t		w_all_znodes;	/* all vnodes in the fs */
	kmutex_t	w_znodes_lock;	/* lock for z_all_znodes */
	vnode_t		*w_ctldir;	/* .zfs directory pointer */
	boolean_t	w_show_ctldir;	/* expose .zfs in the root dir */
	boolean_t	w_issnap;	/* true if this is a snapshot */
	uint64_t	w_version;
#define	ZFS_OBJ_MTX_SZ	64
	kmutex_t	w_hold_mtx[ZFS_OBJ_MTX_SZ];	/* znode hold locks */
};

/*
 * Normal filesystems (those not under .zfs/snapshot) have a total
 * file ID size limited to 12 bytes (including the length field) due to
 * NFSv2 protocol's limitation of 32 bytes for a filehandle.  For historical
 * reasons, this same limit is being imposed by the Solaris NFSv3 implementation
 * (although the NFSv3 protocol actually permits a maximum of 64 bytes).  It
 * is not possible to expand beyond 12 bytes without abandoning support
 * of NFSv2.
 *
 * For normal filesystems, we partition up the available space as follows:
 *	2 bytes		fid length (required)
 *	6 bytes		object number (48 bits)
 *	4 bytes		generation number (32 bits)
 *
 * We reserve only 48 bits for the object number, as this is the limit
 * currently defined and imposed by the DMU.
 */
typedef struct wfid_short {
	uint16_t	wf_len;
	uint8_t		wf_object[6];		/* obj[i] = obj >> (8 * i) */
	uint8_t		wf_gen[4];		/* gen[i] = gen >> (8 * i) */
} wfid_short_t;

/*
 * Filesystems under .zfs/snapshot have a total file ID size of 22 bytes
 * (including the length field).  This makes files under .zfs/snapshot
 * accessible by NFSv3 and NFSv4, but not NFSv2.
 *
 * For files under .zfs/snapshot, we partition up the available space
 * as follows:
 *	2 bytes		fid length (required)
 *	6 bytes		object number (48 bits)
 *	4 bytes		generation number (32 bits)
 *	6 bytes		objset id (48 bits)
 *	4 bytes		currently just zero (32 bits)
 *
 * We reserve only 48 bits for the object number and objset id, as these are
 * the limits currently defined and imposed by the DMU.
 */
typedef struct wfid_long {
	wfid_short_t	w_fid;
	uint8_t		wf_setid[6];		/* obj[i] = obj >> (8 * i) */
	uint8_t		wf_setgen[4];		/* gen[i] = gen >> (8 * i) */
} wfid_long_t;

#define	SHORT_FID_LEN	(sizeof (wfid_short_t) - sizeof (uint16_t))
#define	LONG_FID_LEN	(sizeof (wfid_long_t) - sizeof (uint16_t))

extern uint_t wfs_fsyncer_key;


#ifdef	__cplusplus
}
#endif

#endif	/* _WFS_VFSOPS_H */

