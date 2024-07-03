/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/* This is the prototype for the strdup() function which is distributed
   with Postgres.  That strdup() is only needed on those systems that
   don't already have strdup() in their system libraries.

   The Postgres strdup() is in src/utils/strdup.c.
*/

extern char *strdup(char const *);
