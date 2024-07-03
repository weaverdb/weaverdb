/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */








/* ----------------
 *		HeapKeyTest
 *
 *		Test a heap tuple with respect to a scan key.
 * ----------------
 */

bool HeapKeyTest(HeapTuple tuple, 
					TupleDescr tupdesc, 
					int nkeys, 
					ScanKey keys) 
{ 
/* We use underscores to protect the variable passed in as parameters */ 
/* We use two underscore here because this macro is included in the 
   macro below */ 
	bool		isnull; 
	Datum		atp; 
	int			test; 
	int			cur_nkeys = (nkeys); 
	ScanKey		cur_keys = (keys); 


	for (; cur_nkeys--; cur_keys++) 
	{ 
		atp = heap_getattr((tuple), 
						   cur_keys->sk_attno, 
						   (tupdesc), 
						   &isnull); 
 
		if (isnull) 
		{ 
			/* XXX eventually should check if SK_ISNULL */ 
			return false; 
		} 
		if (cur_keys->sk_flags & SK_ISNULL) 
		{ 
			return false; 
		} 

		if (cur_keys->sk_func.fn_addr == (func_ptr) oideq)	/* optimization */ 
			test = (cur_keys->sk_argument == atp); 
		else if (cur_keys->sk_flags & SK_COMMUTE) 
			test = (Datum) FMGR_PTR2(&cur_keys->sk_func,cur_keys->sk_argument, atp); 
		else 
			test = (Datum) FMGR_PTR2(&cur_keys->sk_func, 
									atp, cur_keys->sk_argument); 
		if (!test == !(cur_keys->sk_flags & SK_NEGATE)) 
		{ 
			/* XXX eventually should check if SK_ISNULL */ 
			return false;
		} 
	} 
	return true;
}





/* ----------------
 *		HeapTupleSatisfies
 *
 *	Returns a valid HeapTuple if it satisfies the timequal and keytest.
 *	Returns NULL otherwise.  Used to be heap_satisifies (sic) which
 *	returned a boolean.  It now returns a tuple so that we can avoid doing two
 *	PageGetItem's per tuple.
 *
 *		Complete check of validity including LP_CTUP and keytest.
 *		This should perhaps be combined with valid somehow in the
 *		future.  (Also, additional rule tests/time range tests.)
 *
 *	on 8/21/92 mao says:  i rearranged the tests here to do keytest before
 *	SatisfiesTimeQual.	profiling indicated that even for vacuumed relations,
 *	time qual checking was more expensive than key testing.  time qual is
 *	least likely to fail, too.	we should really add the time qual test to
 *	the restriction and optimize it in the normal way.	this has interactions
 *	with joey's expensive function work.
 * ----------------
 */
bool HeapTupleSatisfies(Env env,HeapTuple tuple,Relation relation, Buffer buffer, 
		PageHeader disk_page, Snapshot seeself, int nKeys, ScanKey key) 
{ 
/* We use underscores to protect the variable passed in as parameters */
	bool		res; 

	if ((key) != NULL) 
		res = HeapKeyTest(tuple, RelationGetDescr(relation), (nKeys), (key)); 
	else 
		res = TRUE; 
 
	if (res) 
	{ 
		if ((relation)->rd_rel->relkind != RELKIND_UNCATALOGED) 
		{ 
			uint16	_infomask = (tuple)->t_data->t_infomask;
			res = HeapTupleSatisfiesVisibility(env,(tuple), (seeself)); 
			if ((tuple)->t_data->t_infomask != _infomask) 
				SetBufferCommitInfoNeedsSave(buffer); 
			if (!res) 
				(tuple)->t_data = NULL; 
		} 
	} 
	else 
	{ 
		(tuple)->t_data = NULL; 
	} 
	return res;
}
