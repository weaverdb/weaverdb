/* ----------
 * lztext.h
 *
 * $Header: /cvs/weaver/mtpgsql/src/include/utils/lztext.h,v 1.1.1.1 2006/08/12 00:22:27 synmscott Exp $
 *
 *	Definitions for the lztext compressed data type
 * ----------
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
