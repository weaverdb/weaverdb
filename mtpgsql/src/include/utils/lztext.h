/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/* ----------
 * lztext.h
 *
 *
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
