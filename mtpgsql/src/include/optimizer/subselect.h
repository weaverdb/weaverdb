/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*-------------------------------------------------------------------------
 *
 * subselect.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SUBSELECT_H
#define SUBSELECT_H

#include "nodes/plannodes.h"

PG_EXTERN List *SS_finalize_plan(Plan *plan);
PG_EXTERN Node *SS_replace_correlation_vars(Node *expr);
PG_EXTERN Node *SS_process_sublinks(Node *expr);

#endif	 /* SUBSELECT_H */
