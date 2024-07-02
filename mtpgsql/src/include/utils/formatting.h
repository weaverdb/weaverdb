
/* -----------------------------------------------------------------------
 * formatting.h
 *
 *
 *
 *
 *	 Portions Copyright (c) 1999-2000, PostgreSQL, Inc
 *
 *	 The PostgreSQL routines for a DateTime/int/float/numeric formatting,
 *	 inspire with Oracle TO_CHAR() / TO_DATE() / TO_NUMBER() routines.
 *
 *	 Karel Zak - Zakkr
 *
 * -----------------------------------------------------------------------
 */

#ifndef _FORMATTING_H_
#define _FORMATTING_H_

PG_EXTERN text *timestamp_to_char(Timestamp *dt, text *fmt);
PG_EXTERN Timestamp *to_timestamp(text *date_str, text *fmt);
PG_EXTERN DateADT to_date(text *date_str, text *fmt);
PG_EXTERN Numeric numeric_to_number(text *value, text *fmt);
PG_EXTERN text *numeric_to_char(Numeric value, text *fmt);
PG_EXTERN text *int4_to_char(int32 value, text *fmt);
PG_EXTERN text *int8_to_char(int64 *value, text *fmt);
PG_EXTERN text *float4_to_char(float32 value, text *fmt);
PG_EXTERN text *float8_to_char(float64 value, text *fmt);

#endif
