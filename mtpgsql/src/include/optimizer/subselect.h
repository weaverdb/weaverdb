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
