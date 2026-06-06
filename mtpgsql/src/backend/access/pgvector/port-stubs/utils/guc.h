/* Stub utils/guc.h for pgvector */
#ifndef UTILS_GUC_H
#define UTILS_GUC_H

/* GUC not heavily used; provide minimal */
typedef enum { PGC_SIGHUP, PGC_POSTMASTER } GucContext;
/* GucSource provided by compat.h to avoid redef */

#endif /* UTILS_GUC_H */