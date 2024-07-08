/*-------------------------------------------------------------------------
 *
 * lztext.h
 *	  Convenience routines for common queries in the system catalog cache.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#ifndef _LZTEXT_H_
#define _LZTEXT_H_

#include "utils/pg_lzcompress.h"


/* ----------
 * The internal storage format of an LZ compressed text field
 * ----------
 */
typedef PGLZ_Header lztext;

#endif	 /* _LZTEXT_H_ */
