/* Varlena attribute macros stub for pgvector on WeaverDB's older fork.
 *
 * The fork's postgres.h already provides basic VARSIZE / SETVARSIZE / VARDATA.
 * pgvector code (PG13-era) uses SET_VARSIZE, VARSIZE_ANY, VARATT_IS_* etc.
 */
#ifndef VARATT_H
#define VARATT_H

#include "postgres.h"

/* Core aliases used everywhere in pgvector */
#define SET_VARSIZE(PTR, len)   SETVARSIZE((PTR), (len))
#define VARSIZE_ANY(PTR)        VARSIZE(PTR)
#define VARSIZE_ANY_EXHDR(PTR)  (VARSIZE(PTR) - VARHDRSZ)

/* Modern names for data access */
#define VARDATA_ANY(PTR)        VARDATA(PTR)

/* Extended / compressed / toast checks (we have no TOAST in this context usually) */
#define VARATT_IS_4B(PTR)       (1)
#define VARATT_IS_4B_U(PTR)     (0)
#define VARATT_IS_1B(PTR)       (0)
#define VARATT_IS_1B_E(PTR)     (0)
#define VARATT_IS_COMPRESSED(PTR) (0)
#define VARATT_IS_EXTERNAL(PTR)   (0)
#define VARATT_IS_EXTERNAL_ONDISK(PTR) (0)
#define VARATT_IS_EXTERNAL_INDIRECT(PTR) (0)
#define VARATT_IS_EXTERNAL_EXPANDED(PTR) (0)
#define VARATT_IS_EXTENDED(PTR)   (0)
#define VARATT_IS_SHORT(PTR)      (0)
#define VARATT_IS_NOT_PADDED(PTR) (0)

#define VARATT_SHORT_PAD_BYTE     0

/* For code that does raw size math */
#define VARATT_SHORT_MAX          0x7F

/* Detoast is mostly a no-op here (no external toast in this integration yet) */
#define PG_DETOAST_DATUM(datum)   (datum)
#define PG_DETOAST_DATUM_COPY(datum) (datum)
#define PG_DETOAST_DATUM_PACKED(datum) (datum)

/* Some code uses these for header inspection */
#define VARTAG_1B_E(PTR)          (0)
#define VARTAG_INDIRECT(PTR)      (0)
#define VARTAG_ONDISK(PTR)        (0)
#define VARTAG_EXPANDED(PTR)      (0)

#endif /* VARATT_H */
