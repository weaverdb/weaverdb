/*-------------------------------------------------------------------------
 *
 * heaptuple.c
 *	  This file contains heap tuple accessor and mutator routines, as well
 *	  as a few various tuple utilities.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/common/heaptuple.c,v 1.1.1.1 2006/08/12 00:19:55 synmscott Exp $
 *
 * NOTES
 *	  The old interface functions have been converted to macros
 *	  and moved to heapam.h
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"


#include "utils/tqual.h"
#include "access/heapam.h"
#include "catalog/pg_type.h"
#include "access/tupmacs.h"

/* Used by HeapGetAttr() macro, for speed */
long            heap_sysoffset[] = {
	/*
	 * Only the first one is pass-by-ref, and is handled specially in the
	 * macro
	 */
	offsetof(HeapTupleHeaderData, t_ctid),
	offsetof(HeapTupleHeaderData, t_oid),
	offsetof(HeapTupleHeaderData, t_xmin),
	offsetof(HeapTupleHeaderData, progress.cmd.t_cmin),
	offsetof(HeapTupleHeaderData, t_xmax),
	offsetof(HeapTupleHeaderData, progress.cmd.t_cmax),
	offsetof(HeapTupleHeaderData, progress.t_vtran)
};

/*
 * ---------------------------------------------------------------- misc
 * support routines
 * ----------------------------------------------------------------
 */

/*
 * ---------------- ComputeDataSize ----------------
 */
int
ComputeDataSize(TupleDesc tupleDesc,
		Datum * value,
		char *nulls)
{
	int             data_length;
	int             i;
	int             numberOfAttributes = tupleDesc->natts;
	Form_pg_attribute *att = tupleDesc->attrs;

	for (data_length = 0, i = 0; i < numberOfAttributes; i++) {
		if (nulls[i] == 'n')
			continue;

		data_length = att_align(data_length, att[i]->attlen, att[i]->attalign);
		data_length = att_addlength(data_length, att[i]->attlen, value[i]);
	}

	return data_length;
}

/*
 * ---------------- DataFill ----------------
 */
void
DataFill(char *data,
	 TupleDesc tupleDesc,
	 Datum * value,
	 char *nulls,
	 uint16 * infomask,
	 bits8 * bit)
{
	bits8          *bitP = 0;
	int             bitmask = 0;
	uint32          data_length;
	int             i;
	int             numberOfAttributes = tupleDesc->natts;
	Form_pg_attribute *att = tupleDesc->attrs;

	if (bit != NULL) {
		bitP = &bit[-1];
		bitmask = CSIGNBIT;
	}
	*infomask = 0;

	for (i = 0; i < numberOfAttributes; i++) {
		if (bit != NULL) {
			if (bitmask != CSIGNBIT) {
				bitmask <<= 1;
                        } else {
				bitP += 1;
				*bitP = 0x0;
				bitmask = 1;
			}

			if (nulls[i] == 'n') {
				*infomask |= HEAP_HASNULL;
				continue;
			}
			*bitP |= bitmask;
		}
		data = (char *) att_align((char*) data, att[i]->attlen, att[i]->attalign);
		switch (att[i]->attlen) {
		case -1:
			*infomask |= HEAP_HASVARLENA;
                        data_length = VARSIZE(DatumGetPointer(value[i]));
                        memmove(data, DatumGetPointer(value[i]), data_length);
			break;
		case sizeof(char):
			*data = att[i]->attbyval ?
				DatumGetChar(value[i]) : *((char *) value[i]);
			break;
		case sizeof(int16):
			*((short *) data) = (att[i]->attbyval ?
					   DatumGetInt16(value[i]) :
					   *((int16*) value[i]));
			break;
		case sizeof(int32):
			*((int32 *) data) = (att[i]->attbyval ?
					   DatumGetInt32(value[i]) :
					   *((int32 *) value[i]));
			break;
#ifdef _LP64
		case sizeof(int64):
			*((int64 *) data) = (att[i]->attbyval ?
					   DatumGetInt64(value[i]) :
					   *((int64 *) value[i]));
                        break;
#endif
		default:
			Assert(att[i]->attlen >= 0);
			Assert(!att[i]->attbyval);
			memmove(data, DatumGetPointer(value[i]),
				(int4) (att[i]->attlen));
			break;
		}
		data = (char *) att_addlength((char*) data, att[i]->attlen, value[i]);
	}
}

/*
 * ---------------------------------------------------------------- heap
 * tuple interface
 * ----------------------------------------------------------------
 */

/*
 * ---------------- heap_attisnull	- returns 1 iff tuple attribute is
 * not present ----------------
 */
int
heap_attisnull(HeapTuple tup, int attnum)
{
	if (attnum > (int) tup->t_data->t_natts)
		return 1;

	if (HeapTupleNoNulls(tup))
		return 0;

	if (attnum > 0)
		return att_isnull(attnum - 1, tup->t_data->t_bits);
	else
		switch (attnum) {
		case SelfItemPointerAttributeNumber:
		case ObjectIdAttributeNumber:
		case MinTransactionIdAttributeNumber:
		case MinCommandIdAttributeNumber:
		case MaxTransactionIdAttributeNumber:
		case MaxCommandIdAttributeNumber:
			break;
		case MoveTransactionIdAttributeNumber:
			if (!(tup->t_data->t_infomask & HEAP_MOVED_IN)) {
				return 1;
			}
			break;
		case 0:
			elog(ERROR, "heap_attisnull: zero attnum disallowed");

		default:
			elog(ERROR, "heap_attisnull: undefined negative attnum");
		}

	return 0;
}

/*
 * ---------------------------------------------------------------- system
 * attribute heap tuple support
 * ----------------------------------------------------------------
 */

/*
 * ---------------- heap_sysattrlen
 * 
 * This routine returns the length of a system attribute. ----------------
 */
int
heap_sysattrlen(AttrNumber attno)
{
	HeapTupleHeader f = NULL;

	switch (attno) {
	case SelfItemPointerAttributeNumber:
		return sizeof f->t_ctid;
	case ObjectIdAttributeNumber:
		return sizeof f->t_oid;
	case MinTransactionIdAttributeNumber:
		return sizeof f->t_xmin;
	case MinCommandIdAttributeNumber:
		return sizeof f->progress.cmd.t_cmin;
	case MaxTransactionIdAttributeNumber:
		return sizeof f->t_xmax;
	case MaxCommandIdAttributeNumber:
		return sizeof f->progress.cmd.t_cmax;
	case MoveTransactionIdAttributeNumber:
		return sizeof f->progress.t_vtran;
	default:
		elog(ERROR, "sysattrlen: System attribute number %d unknown.", attno);
		return 0;
	}
}

/*
 * ---------------- heap_sysattrbyval
 * 
 * This routine returns the "by-value" property of a system attribute.
 * ----------------
 */
bool
heap_sysattrbyval(AttrNumber attno)
{
	bool            byval;

	switch (attno) {
	case SelfItemPointerAttributeNumber:
		byval = false;
		break;
	case ObjectIdAttributeNumber:
		byval = true;
		break;
	case MinTransactionIdAttributeNumber:
		byval = false;
		break;
	case MinCommandIdAttributeNumber:
		byval = true;
		break;
	case MaxTransactionIdAttributeNumber:
		byval = false;
		break;
	case MaxCommandIdAttributeNumber:
		byval = true;
		break;
	case MoveTransactionIdAttributeNumber:
		byval = false;
		break;
	default:
		byval = true;
		elog(ERROR, "sysattrbyval: System attribute number %d unknown.",
		     attno);
		break;
	}

	return byval;
}

/*
 * ---------------- nocachegetattr
 * 
 * This only gets called from fastgetattr() macro, in cases where we can't use a
 * cacheoffset and the value is not null.
 * 
 * This caches attribute offsets in the attribute descriptor.
 * 
 * An alternate way to speed things up would be to cache offsets with the tuple,
 * but that seems more difficult unless you take the storage hit of actually
 * putting those offsets into the tuple you send to disk.  Yuck.
 * 
 * This scheme will be slightly slower than that, but should perform well for
 * queries which hit large #'s of tuples.  After you cache the offsets once,
 * examining all the other tuples using the same attribute descriptor will go
 * much quicker. -cim 5/4/91 ----------------
 */
Datum
nocachegetattr(HeapTuple tuple,
	       int attnum,
	       TupleDesc tupleDesc,
	       bool * isnull)
{
	char           *tp;	/* ptr to att in tuple */
	HeapTupleHeader tup = tuple->t_data;
	bits8          *bp = tup->t_bits;	/* ptr to att in tuple */
	Form_pg_attribute *att = tupleDesc->attrs;
	int             slow = 0;	/* do we have to walk nulls? */

	(void) isnull;		/* not used */
#ifdef IN_MACRO
	/* This is handled in the macro */
	Assert(attnum > 0);

	if (isnull)
		*isnull = false;
#endif

	attnum--;

	/*
	 * ---------------- Three cases:
	 * 
	 * 1: No nulls and no variable length attributes. 2: Has a null or a
	 * varlena AFTER att. 3: Has nulls or varlenas BEFORE att.
	 * ----------------
	 */

	if (HeapTupleNoNulls(tuple)) {
#ifdef IN_MACRO
		/* This is handled in the macro */
		if (att[attnum]->attcacheoff != -1) {
			return (Datum)
				HeapFetchAtt(&(att[attnum]),
					     (char *) tup + tup->t_hoff + att[attnum]->attcacheoff);
		} else if (attnum == 0) {

			/*
			 * first attribute is always at position zero
			 */
			return (Datum) HeapFetchAtt(&(att[0]), (char *) tup + tup->t_hoff);
		}
#endif
	} else {

		/*
		 * there's a null somewhere in the tuple
		 */

		/*
		 * ---------------- check to see if desired att is null
		 * ----------------
		 */

#ifdef IN_MACRO
		/* This is handled in the macro */
		if (att_isnull(attnum, bp)) {
			if (isnull)
				*isnull = true;
			return (Datum) NULL;
		}
#endif

		/*
		 * ---------------- Now check to see if any preceding bits
		 * are null... ----------------
		 */
		{
			int             byte = attnum >> 3;
			int             finalbit = attnum & 0x07;

			/* check for nulls "before" final bit of last byte */
			if ((~bp[byte]) & ((1 << finalbit) - 1))
				slow = 1;
			else {
				/* check for nulls in any "earlier" bytes */
				int             i;

				for (i = 0; i < byte; i++) {
					if (bp[i] != 0xFF) {
						slow = 1;
						break;
					}
				}
			}
		}
	}

	tp = (char *) tup + tup->t_hoff;

	/*
	 * now check for any non-fixed length attrs before our attribute
	 */
	if (!slow) {
		if (att[attnum]->attcacheoff != -1) {
			return (Datum) HeapFetchAtt(&(att[attnum]),
					     tp + att[attnum]->attcacheoff);
		} else if (attnum == 0)
			return (Datum) HeapFetchAtt(&(att[0]), tp);
		else if (!HeapTupleAllFixed(tuple)) {
			int             j;

			/*
			 * In for(), we make this <= and not < because we
			 * want to test if we can go past it in initializing
			 * offsets.
			 */
			for (j = 0; j <= attnum; j++) {
				if (att[j]->attlen < 1 && !VARLENA_FIXED_SIZE(att[j])) {
					slow = 1;
					break;
				}
			}
		}
	}
	/*
	 * If slow is zero, and we got here, we know that we have a tuple
	 * with no nulls or varlenas before the target attribute. If
	 * possible, we also want to initialize the remainder of the
	 * attribute cached offset values.
	 */
	if (!slow) {
		int             j = 1;
		Offset          off;

		/*
		 * need to set cache for some atts
		 */

		att[0]->attcacheoff = 0;

		while (att[j]->attcacheoff > 0)
			j++;

		if (!VARLENA_FIXED_SIZE(att[j - 1]))
			off = att[j - 1]->attcacheoff + att[j - 1]->attlen;
		else
			off = att[j - 1]->attcacheoff + att[j - 1]->atttypmod;

		while (j <= attnum) {
			off = att_align(off, att[j]->attlen, att[j]->attalign);
			att[j]->attcacheoff = (int4) off;
			off = att_addlength(off, att[j]->attlen, tp + off);
			j++;
		}

		return (Datum) HeapFetchAtt(&(att[attnum]), tp + att[attnum]->attcacheoff);
	} else {
		bool            usecache = true;
		Offset          off = 0;
		int             i;

		/*
		 * Now we know that we have to walk the tuple CAREFULLY.
		 * 
		 * Note - This loop is a little tricky.  On iteration i we first
		 * set the offset for attribute i and figure out how much the
		 * offset should be incremented.  Finally, we need to align
		 * the offset based on the size of attribute i+1 (for which
		 * the offset has been computed). -mer 12 Dec 1991
		 */

		for (i = 0; i < attnum; i++) {
			if (!HeapTupleNoNulls(tuple)) {
				if (att_isnull(i, bp)) {
					usecache = false;
					continue;
				}
			}
			/* If we know the next offset, we can skip the rest */
			if (usecache && att[i]->attcacheoff != -1)
				off = att[i]->attcacheoff;
			else {
				/* LINTED */
				off = att_align(off, att[i]->attlen, att[i]->attalign);

				if (usecache)
					att[i]->attcacheoff = off;
			}

			off = att_addlength(off, att[i]->attlen, tp + off);

			if (usecache &&
			att[i]->attlen == -1 && !VARLENA_FIXED_SIZE(att[i]))
				usecache = false;
		}
		/* LINTED */
		off = att_align(off, att[attnum]->attlen, att[attnum]->attalign);

		return (Datum) HeapFetchAtt(&(att[attnum]), tp + off);
	}
}

/*
 * ---------------- heap_copytuple
 * 
 * returns a copy of an entire tuple ----------------
 */
HeapTuple
heap_copytuple(HeapTuple tuple)
{
	HeapTuple       newTuple;

	if (!HeapTupleIsValid(tuple) || tuple->t_data == NULL)
		return NULL;

	newTuple = (HeapTuple) palloc(HEAPTUPLESIZE + tuple->t_len);
	newTuple->t_len = tuple->t_len;
	newTuple->t_self = tuple->t_self;
	newTuple->t_datamcxt = MemoryContextGetCurrentContext();
	newTuple->t_datasrc = NULL;
	newTuple->t_info = tuple->t_info;
	newTuple->t_data = (HeapTupleHeader) ((char *) newTuple + HEAPTUPLESIZE);
	memmove((char *) newTuple->t_data,
		(char *) tuple->t_data, tuple->t_len);
	return newTuple;
}

/*
 * ---------------- heap_formtuple
 * 
 * constructs a tuple from the given *value and *null arrays
 * 
 * old comments Handles alignment by aligning 2 byte attributes on short
 * boundries and 3 or 4 byte attributes on long word boundries on a vax; and
 * aligning non-byte attributes on short boundries on a sun.  Does not
 * properly align fixed length arrays of 1 or 2 byte types (yet).
 * 
 * Null attributes are indicated by a 'n' in the appropriate byte of the *null.
 * Non-null attributes are indicated by a ' ' (space).
 * 
 * Fix me.  (Figure that must keep context if debug--allow give oid.) Assumes in
 * order. ----------------
 */
HeapTuple
heap_formtuple(TupleDesc tupleDescriptor,
	       Datum * value,
	       char *nulls)
{
	HeapTuple       tuple;	
	HeapTupleHeader td;	
	int             bitmaplen;
	int             len;
	int             hoff;
	bool            hasnull = false;
	bool            hasindirect = false;
	bool            hasbuffered = false;
        int             i;
	int             numberOfAttributes = tupleDescriptor->natts;

	len = offsetof(HeapTupleHeaderData, t_bits);

	for (i = 0; i < numberOfAttributes; i++) {
            if (nulls[i] == 'n') {
                    hasnull = true;
            } else if (tupleDescriptor->attrs[i]->attstorage == 'e' ) {
                if (ISBUFFERED(DatumGetPointer(value[i]))) {
                    hasbuffered = true;
                } if (ISINDIRECT(DatumGetPointer(value[i]))) {
                    hasindirect = true;
                }
            }
	}

	if (numberOfAttributes > MaxHeapAttributeNumber)
		elog(ERROR, "heap_formtuple: numberOfAttributes of %d > %d",
		     numberOfAttributes, MaxHeapAttributeNumber);

	if (hasnull) {
		bitmaplen = BITMAPLEN(numberOfAttributes);
		len += bitmaplen;
	}
	len = MAXALIGN(len);	/* be conservative here */
	hoff = len;

	len += ComputeDataSize(tupleDescriptor, value, nulls);

	tuple = (HeapTuple) palloc(HEAPTUPLESIZE + len);
	tuple->t_datamcxt = MemoryContextGetCurrentContext();
	tuple->t_datasrc = NULL;
	tuple->t_info = 0;
	if ( hasindirect ) tuple->t_info |= TUPLE_HASINDIRECT;
	if ( hasbuffered ) tuple->t_info |= TUPLE_HASBUFFERED;
        td = tuple->t_data = (HeapTupleHeader) ((char *) tuple + HEAPTUPLESIZE);

	MemSet((char *) td, 0, len);

	tuple->t_len = len;
	ItemPointerSetInvalid(&(tuple->t_self));
	td->t_natts = numberOfAttributes;
	td->t_hoff = (uint8) hoff;

	DataFill((char *) td + td->t_hoff,
		 tupleDescriptor,
		 value,
		 nulls,
		 &td->t_infomask,
		 (hasnull ? td->t_bits : NULL));

	td->t_infomask |= HEAP_XMAX_INVALID;

	return tuple;
}

/*
 * ---------------- heap_modifytuple
 * 
 * forms a new tuple from an old tuple and a set of replacement values. returns
 * a new palloc'ed tuple. ----------------
 */
HeapTuple
heap_modifytuple(HeapTuple tuple,
		 Relation relation,
		 Datum * replValue,
		 char *replNull,
		 char *repl)
{
	int             attoff;
	int             numberOfAttributes;
	Datum          *value;
	char           *nulls;
	bool            isNull;
	HeapTuple       newTuple;
	uint16           infomask;

	/*
	 * ---------------- sanity checks ----------------
	 */
	Assert(HeapTupleIsValid(tuple));
	Assert(RelationIsValid(relation));
	Assert(PointerIsValid(replValue));
	Assert(PointerIsValid(replNull));
	Assert(PointerIsValid(repl));

	numberOfAttributes = RelationGetForm(relation)->relnatts;

	/*
	 * ---------------- allocate and fill *value and *nulls arrays from
	 * either the tuple or the repl information, as appropriate.
	 * ----------------
	 */
	value = (Datum *) palloc(numberOfAttributes * sizeof(Datum));
	nulls = (char *) palloc(numberOfAttributes * sizeof(char));

	for (attoff = 0;
	     attoff < numberOfAttributes;
	     attoff += 1) {

		if (repl[attoff] == ' ') {
			value[attoff] = HeapGetAttr(tuple,
					    AttrOffsetGetAttrNumber(attoff),
						 RelationGetDescr(relation),
						    &isNull);
			nulls[attoff] = (isNull) ? 'n' : ' ';

		} else if (repl[attoff] != 'r')
			elog(ERROR, "heap_modifytuple: repl is \\%3d", repl[attoff]);
		else {		/* == 'r' */
			value[attoff] = replValue[attoff];
			nulls[attoff] = replNull[attoff];
		}
	}

	/*
	 * ---------------- create a new tuple from the *values and *nulls
	 * arrays ----------------
	 */
	newTuple = heap_formtuple(RelationGetDescr(relation),
				  value,
				  nulls);

	/*
	 * ---------------- copy the header except for t_len, t_natts,
	 * t_hoff, t_bits, t_infomask ----------------
	 */
	infomask = newTuple->t_data->t_infomask;
	memmove((char *) &newTuple->t_data->t_oid,	/* XXX */
		(char *) &tuple->t_data->t_oid,
		((char *) &tuple->t_data->t_hoff -
		 (char *) &tuple->t_data->t_oid));	/* XXX */
	newTuple->t_data->t_infomask = infomask;
	newTuple->t_data->t_natts = numberOfAttributes;
	newTuple->t_self = tuple->t_self;
        
	pfree(value);
	pfree(nulls);

	return newTuple;
}


/*
 * ---------------- heap_freetuple ----------------
 */
void
heap_freetuple(HeapTuple htup)
{
	if (htup->t_data != NULL)
		if (htup->t_datamcxt != NULL && (char *) (htup->t_data) !=
		 ((char *) htup + HEAPTUPLESIZE) && htup->t_datasrc == NULL)
			elog(NOTICE, "TELL Jan Wieck: heap_freetuple() found separate t_data");

	if (htup->t_datasrc != NULL)
		pfree(htup->t_datasrc);
	pfree(htup);
}


/*
 * ---------------------------------------------------------------- other
 * misc functions
 * ----------------------------------------------------------------
 */

HeapTuple
heap_addheader(uint32 natts,		/* max domain index */
	       int structlen,	/* its length */
	       char *structure)
{				/* pointer to the struct */
	HeapTuple       tuple;
	HeapTupleHeader td;	/* tuple data */
	unsigned long   len;
	int             hoff;

	AssertArg(natts > 0);

	len = offsetof(HeapTupleHeaderData, t_bits);

	hoff = len = MAXALIGN(len);	/* be conservative */
	len += structlen;
	tuple = (HeapTuple) palloc(HEAPTUPLESIZE + len);
	tuple->t_datamcxt = MemoryContextGetCurrentContext();
	tuple->t_datasrc = NULL;
	tuple->t_info = 0;
	td = tuple->t_data = (HeapTupleHeader) ((char *) tuple + HEAPTUPLESIZE);

	MemSet((char *) td, 0, len);

	tuple->t_len = len;
	ItemPointerSetInvalid(&(tuple->t_self));
	td->t_hoff = hoff;
	td->t_natts = natts;
	td->t_infomask = 0;
	td->t_infomask |= HEAP_XMAX_INVALID;


	if (structlen > 0)
		memmove((char *) td + hoff, structure, (size_t) structlen);

	return tuple;
}


bool
HeapKeyTest(HeapTuple tuple,
	    TupleDesc tupdesc,
	    int nkeys,
	    ScanKey keys)
{
	/* We use underscores to protect the variable passed in as parameters */
	/*
	 * We use two underscore here because this macro is included in the \
	 * macro below
	 */
	bool            isnull;
	Datum           atp;
	int            test;
	int             cur_nkeys = (nkeys);
	ScanKey         cur_keys = (keys);

	for (; cur_nkeys--; cur_keys++) {
		atp = HeapGetAttr((tuple),
				  cur_keys->sk_attno,
				  (tupdesc),
				  &isnull);

		if (isnull) {
			/* XXX eventually should check if SK_ISNULL */
			return false;
		}
		if (cur_keys->sk_flags & SK_ISNULL) {
			return false;
		}

		if (cur_keys->sk_flags & SK_COMMUTE) {
 			test = DatumGetChar(FMGR_PTR2(&cur_keys->sk_func,cur_keys->sk_argument, atp));
		} else {
			test = DatumGetChar(FMGR_PTR2(&cur_keys->sk_func, atp, cur_keys->sk_argument));
		}
		if (!test == !(cur_keys->sk_flags & SK_NEGATE)) {
			/* XXX eventually should check if SK_ISNULL */
			return false;
		}
	}
	return true;
}

bool
HeapTupleSatisfies(Relation relation,Buffer buffer,HeapTuple tuple,
		   Snapshot seeself,
		   int nKeys,
		   ScanKey key)
{
	bool            res = TRUE;

        SnapshotHolder* env = RelationGetSnapshotCxt(relation);

	if ((key) != NULL)
		res = HeapKeyTest(tuple, RelationGetDescr(relation),
				  (nKeys), (key));

	if (res) {
            if ((relation)->rd_rel->relkind != RELKIND_UNCATALOGED) {
                uint16          infomask = (tuple)->t_data->t_infomask;

                res = HeapTupleSatisfiesVisibility(env, (tuple), (seeself));
                if ((tuple)->t_data->t_infomask != infomask)
                        SetBufferCommitInfoNeedsSave(buffer);
                if (!res)
                        (tuple)->t_data = NULL;
            }
	} else {
		(tuple)->t_data = NULL;
	}
	return res;
}


static TransactionId dummy_move_id = InvalidTransactionId;

Datum
HeapGetAttr(HeapTuple tup, int attnum, TupleDesc tupleDesc, bool * isnull)
{
	if (isnull) *isnull = false;
	if ((tup) == NULL ||
	    (attnum) < FirstLowInvalidHeapAttributeNumber ||
	    (attnum) == 0) {
		if (isnull) *(isnull) = true;
		return (Datum) NULL;
	}
	if ((attnum) > (int) (tup)->t_data->t_natts) {
            if (isnull) *(isnull) = true;
            return (Datum) NULL;
	}
	if ((attnum) > 0) {
		if (HeapTupleNoNulls(tup)) {
                    if ((tupleDesc)->attrs[(attnum) - 1]->attcacheoff != -1 || (attnum) == 1) {
                        Datum  value = HeapFetchAtt(&((tupleDesc)->attrs[(attnum) - 1]),
                                 (char *) (tup)->t_data + (tup)->t_data->t_hoff +
                                 (((attnum) != 1) ? (tupleDesc)->attrs[(attnum) - 1]->attcacheoff : 0));
                        return value;
                    }
                    return nocachegetattr((tup), (attnum), (tupleDesc), (isnull));
		}
		if (att_isnull((attnum) - 1, (tup)->t_data->t_bits)) {
			if (isnull) *(isnull) = true;
			return (Datum) NULL;
		} else {
			return nocachegetattr((tup), (attnum), (tupleDesc), (isnull));
		}
	} else {
		if ((attnum) == SelfItemPointerAttributeNumber) {
			return (Datum) ((char *) &((tup)->t_self));
		} else if ((attnum) == MinTransactionIdAttributeNumber) {
			if ((tup)->t_data->t_infomask & HEAP_MOVED_IN)
				return (Datum) ((char *) (tup)->t_data + heap_sysoffset[-(MoveTransactionIdAttributeNumber) - 1]);
			else
				return (Datum) ((char *) (tup)->t_data + heap_sysoffset[-(MinTransactionIdAttributeNumber) - 1]);
		} else if ((attnum) == MaxTransactionIdAttributeNumber) {
			return (Datum) ((char *) (tup)->t_data + heap_sysoffset[-(MaxTransactionIdAttributeNumber) - 1]);
		} else if ((attnum) == MoveTransactionIdAttributeNumber) {
			if ((tup)->t_data->t_infomask & (HEAP_MOVED_IN))
				return (Datum) ((char *) (tup)->t_data + heap_sysoffset[-(MinTransactionIdAttributeNumber) - 1]);
			else
				return (Datum) & dummy_move_id;
		} else if ((attnum) == MinCommandIdAttributeNumber ||
			   (attnum) == MaxCommandIdAttributeNumber) {
			if ((tup)->t_data->t_infomask & (HEAP_MOVED_IN)) {
				return (Datum) FirstCommandId;
			} else {
				return (Datum) * (CommandId *) ((char *) (tup)->t_data + heap_sysoffset[-(attnum) - 1]);
			}
		} else {
			return (Datum) * (Oid *) ((char *) (tup)->t_data + heap_sysoffset[-(attnum) - 1]);
		}
	}
}


Datum
HeapFetchAtt(Form_pg_attribute * ap, void *tupledata)
{
	Form_pg_attribute a = *ap;
	Datum           returnval = 0x0;


	if (a->attbyval && a->attlen != -1) {
		switch (a->attlen) {
		case sizeof(int8):
			returnval = (Datum) * (int8 *) tupledata;
			break;
		case sizeof(int16):
			returnval = (Datum) * (int16 *) tupledata;
			break;
		case sizeof(long):
			returnval = (Datum) *(long *) tupledata;
			break;
		default:
			returnval = (Datum) * (int32 *) tupledata;
		}
	} else {
		returnval = PointerGetDatum(tupledata);
	}
	return returnval;
}
