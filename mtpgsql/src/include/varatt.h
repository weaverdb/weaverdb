/* Varlena attribute macros for WeaverDB (PG7 fork + pgvector PG13-era sources).
 *
 * postgres.h provides VARSIZE / SETVARSIZE / VARDATA.
 * pgvector uses SET_VARSIZE, VARSIZE_ANY, VARATT_IS_* etc.
 *
 * Large values use blobstorage (ISINDIRECT blob headers), not PostgreSQL TOAST.
 */
#ifndef VARATT_H
#define VARATT_H

#include "postgres.h"
#include "access/blobstorage.h"

#define SET_VARSIZE(PTR, len)   SETVARSIZE((PTR), (len))
#define VARSIZE_ANY(PTR)        VARSIZE(PTR)
#define VARSIZE_ANY_EXHDR(PTR)  (VARSIZE(PTR) - VARHDRSZ)

#define VARDATA_ANY(PTR)        VARDATA(PTR)

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
#define VARATT_SHORT_MAX          0x7F

#define PG_DETOAST_DATUM(datum)        materialize_blob_datum(datum)
#define PG_DETOAST_DATUM_COPY(datum)   materialize_blob_datum(datum)
#define PG_DETOAST_DATUM_PACKED(datum) materialize_blob_datum(datum)

#define VARTAG_1B_E(PTR)          (0)
#define VARTAG_INDIRECT(PTR)      (0)
#define VARTAG_ONDISK(PTR)        (0)
#define VARTAG_EXPANDED(PTR)      (0)

#endif /* VARATT_H */
