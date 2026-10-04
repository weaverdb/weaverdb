#ifndef BITVEC_H
#define BITVEC_H

/* Stubbed for WeaverDB integration. pgvector_compat.h typedefs VarBit
 * before this header; a second typedef warns under -std=gnu99. */
#ifndef PGVECTOR_HAVE_VARBIT_TYPEDEF
#define PGVECTOR_HAVE_VARBIT_TYPEDEF
typedef struct varbita VarBit;
#endif

VarBit *InitBitVector(int dim);

#endif /* BITVEC_H */
