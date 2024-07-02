/*-------------------------------------------------------------------------
 *
 * nodeMaterial.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEMATERIAL_H
#define NODEMATERIAL_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecMaterial(Material *node);
PG_EXTERN bool ExecInitMaterial(Material *node, EState *estate);
PG_EXTERN int	ExecCountSlotsMaterial(Material *node);
PG_EXTERN void ExecEndMaterial(Material *node);
PG_EXTERN void ExecMaterialReScan(Material *node, ExprContext *exprCtxt);

#ifdef NOT_USED
PG_EXTERN List ExecMaterialMarkPos(Material *node);
PG_EXTERN void ExecMaterialRestrPos(Material *node);

#endif
#endif	 /* NODEMATERIAL_H */
