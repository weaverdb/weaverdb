/*-------------------------------------------------------------------------
 *
 * spi.c
 *				Server Programming Interface private declarations
 *
 * $Header: /cvs/weaver/mtpgsql/src/include/executor/spi_priv.h,v 1.1.1.1 2006/08/12 00:22:18 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef SPI_PRIV_H
#define SPI_PRIV_H

#include "executor/spi.h"

typedef struct
{
	List	   *qtlist;
	uint32		processed;		/* by Executor */
	SPITupleTable *tuptable;
	MemoryContext procCxt;		/* procedure context */
	MemoryContext execCxt;		/* executor context */
	MemoryContext savedcxt;
	CommandId	savedId;
} _SPI_connection;

typedef struct
{
	MemoryContext plancxt;
	List	   *qtlist;
	List	   *ptlist;
	int			nargs;
	Oid		   *argtypes;
} _SPI_plan;

#define _SPI_CPLAN_CURCXT	0
#define _SPI_CPLAN_PROCXT	1
#define _SPI_CPLAN_TOPCXT	2

#endif   /* SPI_PRIV_H */
