/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*-------------------------------------------------------------------------
 *
 * proclang.h
 *	  prototypes for proclang.c.
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PROCLANG_H
#define PROCLANG_H

#include "nodes/parsenodes.h"

PG_EXTERN void CreateProceduralLanguage(CreatePLangStmt *stmt);
PG_EXTERN void DropProceduralLanguage(DropPLangStmt *stmt);

#endif	 /* PROCLANG_H */
