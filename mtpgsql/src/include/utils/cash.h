/*
 * cash.h
 * Written by D'Arcy J.M. Cain
 *
 * Functions to allow input and output of money normally but store
 *	and handle it as int4.
 */

#ifndef CASH_H
#define CASH_H

/* if we store this as 4 bytes, we better make it int, not long, bjm */
typedef signed int Cash;

PG_EXTERN const char *cash_out(Cash *value);
PG_EXTERN Cash *cash_in(const char *str);

PG_EXTERN bool cash_eq(Cash *c1, Cash *c2);
PG_EXTERN bool cash_ne(Cash *c1, Cash *c2);
PG_EXTERN bool cash_lt(Cash *c1, Cash *c2);
PG_EXTERN bool cash_le(Cash *c1, Cash *c2);
PG_EXTERN bool cash_gt(Cash *c1, Cash *c2);
PG_EXTERN bool cash_ge(Cash *c1, Cash *c2);

PG_EXTERN Cash *cash_pl(Cash *c1, Cash *c2);
PG_EXTERN Cash *cash_mi(Cash *c1, Cash *c2);

PG_EXTERN Cash *cash_mul_flt8(Cash *c, float8 *f);
PG_EXTERN Cash *cash_div_flt8(Cash *c, float8 *f);
PG_EXTERN Cash *flt8_mul_cash(float8 *f, Cash *c);

PG_EXTERN Cash *cash_mul_flt4(Cash *c, float4 *f);
PG_EXTERN Cash *cash_div_flt4(Cash *c, float4 *f);
PG_EXTERN Cash *flt4_mul_cash(float4 *f, Cash *c);

PG_EXTERN Cash *cash_mul_int4(Cash *c, int4 i);
PG_EXTERN Cash *cash_div_int4(Cash *c, int4 i);
PG_EXTERN Cash *int4_mul_cash(int4 i, Cash *c);

PG_EXTERN Cash *cash_mul_int2(Cash *c, int2 s);
PG_EXTERN Cash *cash_div_int2(Cash *c, int2 s);
PG_EXTERN Cash *int2_mul_cash(int2 s, Cash *c);

PG_EXTERN Cash *cashlarger(Cash *c1, Cash *c2);
PG_EXTERN Cash *cashsmaller(Cash *c1, Cash *c2);

PG_EXTERN text *cash_words_out(Cash *value);

#endif	 /* CASH_H */
