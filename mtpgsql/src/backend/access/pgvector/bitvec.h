#ifndef BITVEC_H
#define BITVEC_H

/* Stubbed for WeaverDB integration - VarBit provided by project's utils/varbit.h + compat typedef */

#ifndef VarBit
typedef struct varbita VarBit;
#endif

VarBit *InitBitVector(int dim);

#endif /* BITVEC_H */
