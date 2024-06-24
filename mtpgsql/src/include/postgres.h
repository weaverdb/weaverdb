/*-------------------------------------------------------------------------
 *
 * postgres.h
 *	  definition of (and support for) postgres system types.
 * this file is included by almost every .c in the system
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1995, Regents of the University of California
 *
 * $Id: postgres.h,v 1.1.1.1 2006/08/12 00:22:07 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 *	 NOTES
 *		this file will eventually contain the definitions for the
 *		following (and perhaps other) system types:
 *
 *				int2	   int4		  float4	   float8
 *				Oid		   regproc	  RegProcedure
 *				aclitem
 *				struct varlena
 *				int2vector	  oidvector
 *				bytea	   text
 *				NameData   Name
 *
 *	 TABLE OF CONTENTS
 *		1)		simple type definitions
 *		2)		varlena and array types
 *		3)		TransactionId and CommandId
 *		4)		genbki macros used by catalog/pg_xxx.h files
 *		5)		random stuff
 *
 * ----------------------------------------------------------------
 */
#ifndef POSTGRES_H
#define POSTGRES_H

#include "c.h"

#include "postgres_ext.h"

#include "utils/elog.h"
#include "utils/palloc.h"

/* ----------------------------------------------------------------
 *				Section 1:	simple type definitions
 * ----------------------------------------------------------------
 */

typedef int16 int2;
typedef int32 int4;
typedef float float4;
typedef double float8;

typedef int4 aclitem;

#define InvalidOid		0
#define OidIsValid(objectId)  ((bool) ((objectId) != InvalidOid))

/* unfortunately, both regproc and RegProcedure are used */
typedef Oid regproc;
typedef Oid RegProcedure;

/* ptr to func returning (char *) */
#if defined(__mc68000__) && defined(__ELF__)
/* The m68k SVR4 ABI defines that pointers are returned in %a0 instead of
 * %d0. So if a function pointer is declared to return a pointer, the
 * compiler may look only into %a0, but if the called function was declared
 * to return return an integer type, it puts its value only into %d0. So the
 * caller doesn't pink up the correct return value. The solution is to
 * declare the function pointer to return int, so the compiler picks up the
 * return value from %d0. (Functions returning pointers put their value
 * *additionally* into %d0 for compability.) The price is that there are
 * some warnings about int->pointer conversions...
 */
typedef int32 ((*func_ptr) ());

#else

typedef char* ((*func_ptr) (void));
typedef char* ((*func_ptr_1) (long));
typedef char* ((*func_ptr_2) (long,long));
typedef char* ((*func_ptr_3) (long,long,long));
typedef char* ((*func_ptr_4) (long,long,long,long));
typedef char* ((*func_ptr_5) (long,long,long,long,long));
typedef char* ((*func_ptr_6) (long,long,long,long,long,long));
typedef char* ((*func_ptr_7) (long,long,long,long,long,long,long));
typedef char* ((*func_ptr_8) (long,long,long,long,long,long,long,long));
typedef char* ((*func_ptr_9) (long,long,long,long,long,long,long,long,long));

#endif


#define RegProcedureIsValid(p)	OidIsValid(p)

/* ----------------------------------------------------------------
 *				Section 2:	variable length and array types
 * ----------------------------------------------------------------
 */
/* ----------------
 *		struct varlena
 * ----------------
 */
struct varlena
{
	int32		vl_len;
	char		vl_dat[1];
};

#define VARSIZE(PTR)	(((struct varlena *)(PTR))->vl_len & 0x3fffffff)
#define SETVARSIZE(PTR,SIZE)	(((struct varlena *)(PTR))->vl_len = SIZE)
#define SETINDIRECT(PTR)	(((struct varlena *)(PTR))->vl_len |= 0x80000000)
#define ISINDIRECT(PTR)	(((struct varlena *)(PTR))->vl_len & 0x80000000)
#define SETBUFFERED(PTR)	(((struct varlena *)(PTR))->vl_len |= 0x40000000)
#define ISBUFFERED(PTR)	(((struct varlena *)(PTR))->vl_len & 0x40000000)
#define VARDATA(PTR)	(((struct varlena *)(PTR))->vl_dat)
#define VARHDRSZ		((int32) sizeof(int32))

typedef struct varlena bytea;
typedef struct varlena text;
typedef struct varlena wrapped;

typedef int2 int2vector[INDEX_MAX_KEYS];
typedef Oid oidvector[INDEX_MAX_KEYS];

#define OIDARRAYSIZE	sizeof(Oid)*INDEX_MAX_KEYS

#define VARATT_SIZE(__PTR) VARSIZE(__PTR)
#define VARATT_SIZEP(__PTR) VARSIZE(__PTR)


/* We want NameData to have length NAMEDATALEN and int alignment,
 * because that's how the data type 'name' is defined in pg_type.
 * Use a union to make sure the compiler agrees.
 */
typedef union nameData
{
	char		data[NAMEDATALEN];
	int			alignmentDummy;
} NameData;
typedef NameData *Name;

#define NameStr(name)	((name).data)

/* ----------------------------------------------------------------
 *				Section 3: TransactionId and CommandId
 * ----------------------------------------------------------------
 */
typedef uint64_t TransactionId;


#define InvalidTransactionId	0ULL
typedef uint32_t CommandId;

#define FirstCommandId	0ULL

/* ----------------------------------------------------------------
 *				Section 4: genbki macros used by the
 *						   catalog/pg_xxx.h files
 * ----------------------------------------------------------------
 */
#define CATALOG(x) \
	typedef struct CppConcat(FormData_,x)

/* Huh? */
#define DATA(x) extern bool multiuser
#define DESCR(x) extern bool multiuser
#define DECLARE_INDEX(x) extern bool multiuser
#define DECLARE_UNIQUE_INDEX(x) extern bool multiuser

#define BUILD_INDICES
#define BOOTSTRAP

#define BKI_BEGIN
#define BKI_END

/* ----------------------------------------------------------------
 *				Section 5:	random stuff
 *							CSIGNBIT, STATUS...
 * ----------------------------------------------------------------
 */

/* msb for int/unsigned */
#define ISIGNBIT (0x80000000)
#define WSIGNBIT (0x8000)

/* msb for char */
#define CSIGNBIT (0x80)

#define STATUS_OK				(0)
#define STATUS_ERROR			(-1)
#define STATUS_NOT_FOUND		(-2)
#define STATUS_INVALID			(-3)
#define STATUS_UNCATALOGUED		(-4)
#define STATUS_REPLACED			(-5)
#define STATUS_NOT_DONE			(-6)
#define STATUS_BAD_PACKET		(-7)
#define STATUS_FOUND			(1)

#endif	 /* POSTGRES_H */
