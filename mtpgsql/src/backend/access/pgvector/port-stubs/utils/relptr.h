/* Stub relptr.h for pgvector */

#ifndef UTILS_RELPTR_H
#define UTILS_RELPTR_H

/* Minimal to satisfy HnswPtrDeclare */
#define relptr_declare(type, relptrtype) typedef uintptr_t relptrtype

typedef uintptr_t Relptr;

/* Accessor macro used by HnswPtrAccess / HnswPtrStore in hnsw code */
#define relptr_access(base, rp)   ((void*)((char*)(base) + (rp)))

#endif /* UTILS_RELPTR_H */
