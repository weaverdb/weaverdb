/*-------------------------------------------------------------------------
 *
 * nodes.c
 *	  support code for nodes (now that we get rid of the home-brew
 *	  inheritance system, our support code for nodes get much simpler)
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/nodes/nodes.c,v 1.1.1.1 2006/08/12 00:20:42 synmscott Exp $
 *
 * HISTORY
 *	  Andrew Yu			Oct 20, 1994	file creation
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "nodes/nodes.h"
#include "env/env.h"
/*
 * newNode -
 *	  create a new node of the specified size and tag the node with the
 *	  specified tag.
 *
 * !WARNING!: Avoid using newNode directly. You should be using the
 *	  macro makeNode. eg. to create a Resdom node, use makeNode(Resdom)
 *
 */
Node *
newNode(Size size, NodeTag tag)
{
	Node	   *newNode;

	Assert(size >= sizeof(Node));		/* need the tag, at least */

	newNode = (Node *) palloc(size);
	MemSet((char *) newNode, 0, size);
	newNode->type = tag;
	return newNode;
}
