/*-------------------------------------------------------------------------
 *
 * slotvar.h
 *	  slot data specific to axonenv
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef SLOTVAR_H
#define SLOTVAR_H

#define SYNSTRING  8
#define SYNBLOB  12
#define SYNBOOLEAN 13
#define SYNINTEGER 14
#define SYNCHARACTER 1
#define SYNNIL 10
#define SYNBINARYOBJECT 3
#define SYNARRAY 4
#define SYNPATTERN 6

PG_EXTERN void* slotvar_in(char *str);
PG_EXTERN void* int4toslot(int var);
PG_EXTERN void* varchartoslot(struct varlena* var);
PG_EXTERN void* byteatoslot(struct varlena* var);
PG_EXTERN void* texttoslot(struct varlena* var);
PG_EXTERN void* booltoslot(bool var);
PG_EXTERN void* arraytoslot(void* var);
PG_EXTERN void* patterntoslot(void* var);
PG_EXTERN char* slotvar_out(void* val);

PG_EXTERN bool slotvareq(void *val1, void *val2);
PG_EXTERN bool slotvarneq(void *val1, void *val2);

PG_EXTERN bool slotvarlike(void *val1, void *val2);
PG_EXTERN bool slotvarnlike(void *val1, void *val2);

PG_EXTERN bool vctosloteq(void *val1, struct varlena* val2);
PG_EXTERN bool vctoslotneq(void *val1, struct varlena* val2);
PG_EXTERN bool vctoslotlike(void *val1, struct varlena* val2);
PG_EXTERN bool vctoslotnlike(void *val1, struct varlena* val2);

PG_EXTERN bool inttosloteq(void *val1, int val2);
PG_EXTERN bool inttoslotneq(void *val1, int val2);
PG_EXTERN bool inttoslotgt(void *val1, int val2);
PG_EXTERN bool inttoslotlt(void *val1, int val2);

PG_EXTERN bool booltosloteq(void *val1, bool val2);
PG_EXTERN bool booltosloteq(void *val1, bool val2);

#endif	 /* INT8_H */
