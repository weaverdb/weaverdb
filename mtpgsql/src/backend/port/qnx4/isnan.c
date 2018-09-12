/*-------------------------------------------------------------------------
 *
 * isnan.c
 *	  isnan() implementation
 *
 * Copyright (c) 1999, repas AEG Automation GmbH
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/port/qnx4/isnan.c,v 1.1.1.1 2006/08/12 00:21:15 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "os.h"

unsigned char __nan[8] = __nan_bytes;

int
isnan(double dsrc)
{
	return !memcmp(&dsrc, &NAN, sizeof(double));
}
