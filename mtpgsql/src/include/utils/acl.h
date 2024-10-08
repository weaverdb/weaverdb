/*-------------------------------------------------------------------------
 *
 * acl.h
 *	  Definition of (and support for) access control list data structures.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTES
 *	  For backward-compatability purposes we have to allow there
 *	  to be a null ACL in a pg_class tuple.  This will be defined as
 *	  meaning "no protection" (i.e., old catalogs get old semantics).
 *
 *	  The AclItems in an ACL array are currently kept in sorted order.
 *	  Things will break hard if you change that without changing the
 *	  code wherever this is included.
 *
 *-------------------------------------------------------------------------
 */
#ifndef ACL_H
#define ACL_H

#include "nodes/parsenodes.h"
#include "utils/array.h"
#include "utils/memutils.h"

/*
 * AclId		system identifier for the user, group, etc.
 *				XXX currently UNIX uid for users...
 */
typedef uint32 AclId;

#define ACL_ID_WORLD	0		/* XXX only idtype should be checked */

/*
 * AclIdType	tag that describes if the AclId is a user, group, etc.
 */
typedef uint8 AclIdType;

#define ACL_IDTYPE_WORLD		0x00
#define ACL_IDTYPE_UID			0x01	/* user id - from pg_shadow */
#define ACL_IDTYPE_GID			0x02	/* group id - from pg_group */

/*
 * AclMode		the actual permissions
 *				XXX should probably use bit.h routines.
 *				XXX should probably also stuff the modechg cruft in the
 *					high bits, too.
 */
typedef uint8 AclMode;

#define ACL_NO			0		/* no permissions */
#define ACL_AP			(1<<0)	/* append */
#define ACL_RD			(1<<1)	/* read */
#define ACL_WR			(1<<2)	/* write (append/delete/replace) */
#define ACL_RU			(1<<3)	/* place rules */
#define N_ACL_MODES		4

#define ACL_MODECHG_ADD			1
#define ACL_MODECHG_DEL			2
#define ACL_MODECHG_EQL			3

/* change this line if you want to set the default acl permission  */
/*#define ACL_WORLD_DEFAULT		(ACL_NO) */
#define		ACL_WORLD_DEFAULT		(ACL_RD|ACL_WR|ACL_AP|ACL_RU) 
#define 	ACL_OWNER_DEFAULT		(ACL_RD|ACL_WR|ACL_AP|ACL_RU)

/*
 * AclItem
 */
typedef struct AclItem
{
	AclId		ai_id;
	AclIdType	ai_idtype;
	AclMode		ai_mode;

	/*
	 * This is actually type 'aclitem', and we want a fixed size for for
	 * all platforms, so we pad this with dummies.
	 */
	char		dummy1,
				dummy2;
} AclItem;

/* Note: if the size of AclItem changes,
   change the aclitem typlen in pg_type.h */

/*
 * The value of the first dimension-array element.	Since these arrays
 * always have a lower-bound of 0, this is the same as the number of
 * elements in the array.
 */
#define ARR_DIM0(a) (((unsigned *) (((char *) a) + sizeof(ArrayType)))[0])

/*
 * Acl			a one-dimensional POSTGRES array of AclItem
 */
typedef ArrayType Acl;

#define ACL_NUM(ACL)			ARR_DIM0(ACL)
#define ACL_DAT(ACL)			((AclItem *) ARR_DATA_PTR(ACL))
#define ACL_N_SIZE(N) \
		((unsigned) (ARR_OVERHEAD(1) + ((N) * sizeof(AclItem))))
#define ACL_SIZE(ACL)			ARR_SIZE(ACL)

/*
 * IdList		a one-dimensional POSTGRES array of AclId
 */
typedef ArrayType IdList;

#define IDLIST_NUM(IDL)			ARR_DIM0(IDL)
#define IDLIST_DAT(IDL)			((AclId *) ARR_DATA_PTR(IDL))
#define IDLIST_N_SIZE(N) \
		((unsigned) (ARR_OVERHEAD(1) + ((N) * sizeof(AclId))))
#define IDLIST_SIZE(IDL)		ARR_SIZE(IDL)

#define ACL_MODECHG_STR			"+-="	/* list of valid characters */
#define ACL_MODECHG_ADD_CHR		'+'
#define ACL_MODECHG_DEL_CHR		'-'
#define ACL_MODECHG_EQL_CHR		'='
#define ACL_MODE_STR			"arwR"	/* list of valid characters */
#define ACL_MODE_AP_CHR			'a'
#define ACL_MODE_RD_CHR			'r'
#define ACL_MODE_WR_CHR			'w'
#define ACL_MODE_RU_CHR			'R'

/* result codes for pg_aclcheck */
#define ACLCHECK_OK				  0
#define ACLCHECK_NO_PRIV		  1
#define ACLCHECK_NO_CLASS		  2
#define ACLCHECK_NOT_OWNER		  3

/* warning messages.  set these in aclchk.c. */
extern char *aclcheck_error_strings[];

/*
 * Enable ACL execution tracing and table dumps
 */
/*#define ACLDEBUG_TRACE*/

/*
 * routines used internally (parser, etc.)
 */
PG_EXTERN Acl *aclownerdefault(char *relname, AclId ownerid);
PG_EXTERN Acl *acldefault(char *relname);
PG_EXTERN Acl *aclinsert3(Acl *old_acl, AclItem *mod_aip, unsigned modechg);

PG_EXTERN char *aclmakepriv(char *old_privlist, char new_priv);
PG_EXTERN char *aclmakeuser(char *user_type, char *user);
PG_EXTERN ChangeACLStmt *makeAclStmt(char *privs, List *rel_list, char *grantee,
			char grant_or_revoke);

/*
 * exported routines (from acl.c)
 */
PG_EXTERN Acl *makeacl(int n);
PG_EXTERN AclItem *aclitemin(char *s);
PG_EXTERN char *aclitemout(AclItem *aip);
PG_EXTERN Acl *aclinsert(Acl *old_acl, AclItem *mod_aip);
PG_EXTERN Acl *aclremove(Acl *old_acl, AclItem *mod_aip);
PG_EXTERN int32 aclcontains(Acl *acl, AclItem *aip);

/*
 * prototypes for functions in aclchk.c
 */
PG_EXTERN void ChangeAcl(char *relname, AclItem *mod_aip, unsigned modechg);
PG_EXTERN AclId get_grosysid(char *groname);
PG_EXTERN char *get_groname(AclId grosysid);

PG_EXTERN int32 pg_aclcheck(char *relname, char *usename, AclMode mode);
PG_EXTERN int32 pg_ownercheck(const char *usename, const char *value, int cacheid);
PG_EXTERN int32 pg_func_ownercheck(char *usename, char *funcname,
				   int nargs, Oid *arglist);
PG_EXTERN int32 pg_aggr_ownercheck(char *usename, char *aggname,
				   Oid basetypeID);

#endif	 /* ACL_H */
