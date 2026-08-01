#include "postgres.h"

#include "bitutils.h"
#include "bitvec.h"
#include "fmgr.h"
#include "utils/varbit.h"
#include "vector.h"

#include "varatt.h"

/*
 * Allocate and initialize a new bit vector
 */
VarBit *
InitBitVector(int dim)
{
	VarBit	   *result;
	int			size;

	size = VARBITTOTALLEN(dim);
	result = (VarBit *) palloc0(size);
	SET_VARSIZE(result, size);
	VARBITLEN(result) = dim;

	return result;
}

/*
 * Ensure same dimensions
 */
static inline void
CheckDims(VarBit *a, VarBit *b)
{
	if (VARBITLEN(a) != VARBITLEN(b))
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("different bit lengths %u and %u", VARBITLEN(a), VARBITLEN(b))));
}

/*
 * Get the Hamming distance between two bit vectors
 */
FUNCTION_PREFIX PG_FUNCTION_INFO_V1(hamming_distance);
Datum
hamming_distance(PG_FUNCTION_ARGS)
{
	VarBit	   *a = PG_GETARG_VARBIT_P(0);
	VarBit	   *b = PG_GETARG_VARBIT_P(1);

	CheckDims(a, b);

	PG_RETURN_FLOAT8((double) BitHammingDistance(VARBITBYTES(a), VARBITS(a), VARBITS(b), 0));
}

/*
 * Get the Jaccard distance between two bit vectors
 */
FUNCTION_PREFIX PG_FUNCTION_INFO_V1(jaccard_distance);
Datum
jaccard_distance(PG_FUNCTION_ARGS)
{
	VarBit	   *a = PG_GETARG_VARBIT_P(0);
	VarBit	   *b = PG_GETARG_VARBIT_P(1);

	CheckDims(a, b);

	PG_RETURN_FLOAT8(BitJaccardDistance(VARBITBYTES(a), VARBITS(a), VARBITS(b), 0, 0, 0));
}

/*
 * Convert packed bit blob to varbit.
 *
 * Layout (native endian): int32 bit_length | MSB-first packed bits
 * (same byte packing as PostgreSQL varbit / InitBitVector).
 */
FUNCTION_PREFIX PG_FUNCTION_INFO_V1(bytea_to_bit);
Datum
bytea_to_bit(PG_FUNCTION_ARGS)
{
	bytea	   *raw = PG_GETARG_BYTEA_P(0);
	Size		nbytes;
	char	   *ptr;
	int32		bitlen;
	int			bytes;
	VarBit	   *result;

	nbytes = VARSIZE(raw) - VARHDRSZ;
	if (nbytes < sizeof(int32))
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("bytea too short for bit length header")));

	ptr = VARDATA(raw);
	memcpy(&bitlen, ptr, sizeof(int32));
	ptr += sizeof(int32);

	if (bitlen < 1)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("bit length must be at least 1")));

	bytes = (bitlen + 7) / 8;
	if (nbytes != sizeof(int32) + (Size) bytes)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("bytea length does not match bit length %d", bitlen)));

	result = InitBitVector(bitlen);
	memcpy(VARBITS(result), ptr, bytes);

	PG_RETURN_POINTER(result);
}

/*
 * Convert varbit/bit to packed bit blob (see bytea_to_bit).
 */
FUNCTION_PREFIX PG_FUNCTION_INFO_V1(bit_to_bytea);
Datum
bit_to_bytea(PG_FUNCTION_ARGS)
{
	VarBit	   *bits = PG_GETARG_VARBIT_P(0);
	int32		bitlen = VARBITLEN(bits);
	int			bytes = VARBITBYTES(bits);
	Size		nbytes;
	bytea	   *result;
	char	   *ptr;

	nbytes = sizeof(int32) + (Size) bytes;
	result = (bytea *) palloc(VARHDRSZ + nbytes);
	SET_VARSIZE(result, VARHDRSZ + nbytes);
	ptr = VARDATA(result);

	memcpy(ptr, &bitlen, sizeof(int32));
	ptr += sizeof(int32);
	memcpy(ptr, VARBITS(bits), bytes);

	PG_RETURN_BYTEA_P(result);
}
