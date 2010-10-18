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
