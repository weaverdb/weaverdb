/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from FreeBSD 2.2.1-RELEASE software.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *	  must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *	  may be used to endorse or promote products derived from this software
 *	  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.	IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "mb/pg_wchar.h"

int
pg_wchar_strncmp(s1, s2, n)
register const pg_wchar *s1,
		   *s2;
register size_t n;
{

	if (n == 0)
		return 0;
	do
	{
		if (*s1 != *s2++)
			return (*(const pg_wchar *) s1 -
					*(const pg_wchar *) (s2 - 1));
		if (*s1++ == 0)
			break;
	} while (--n != 0);
	return 0;
}

int
pg_char_and_wchar_strncmp(s1, s2, n)
register const char *s1;
register const pg_wchar *s2;
register size_t n;
{

	if (n == 0)
		return 0;
	do
	{
		if ((pg_wchar) * s1 != *s2++)
			return (*(const pg_wchar *) s1 -
					*(const pg_wchar *) (s2 - 1));
		if (*s1++ == 0)
			break;
	} while (--n != 0);
	return 0;
}

size_t
pg_wchar_strlen(str)
const pg_wchar *str;
{
	register const pg_wchar *s;

	for (s = str; *s; ++s);
	return (s - str);
}
