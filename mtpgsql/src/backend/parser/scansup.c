/*-------------------------------------------------------------------------
 *
 * scansup.c
 *	  support routines for the lex/flex scanner, used by both the normal
 * backend as well as the bootstrap backend
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>



#include "postgres.h"

#include "miscadmin.h"
#include "parser/scansup.h"

/* ----------------
 *		scanstr
 *
 * if the string passed in has escaped codes, map the escape codes to actual
 * chars
 *
 * the string returned is palloc'd and should eventually be pfree'd by the
 * caller!
 * ----------------
 */
 

char *
scanstr(char *s)
{
	char	   *newStr;
	int			len,
				i,
				j;

	if (s == NULL || s[0] == '\0')
		return pstrdup("");

	len = strlen(s);

	newStr = palloc(len + 1);	/* string cannot get longer */

	for (i = 0, j = 0; i < len; i++)
	{
		if (s[i] == '\'')
		{

			/*
			 * Note: if scanner is working right, unescaped quotes can
			 * only appear in pairs, so there should be another character.
			 */
			i++;
			newStr[j] = s[i];
		}
		else if (s[i] == '\\')
		{
			i++;
			switch (s[i])
			{
				case 'b':
					newStr[j] = '\b';
					break;
				case 'f':
					newStr[j] = '\f';
					break;
				case 'n':
					newStr[j] = '\n';
					break;
				case 'r':
					newStr[j] = '\r';
					break;
				case 't':
					newStr[j] = '\t';
					break;
				case 'x':
					{
						int hold;
						char conv[5];
						char* check;
						conv[0] = '0';
						conv[1] = 'x';
						conv[2] = s[i+1];
						conv[3] = s[i+2];
						conv[4] = 0;
						hold = strtol(conv,&check,16);
						newStr[j] = (char)hold;
			/*			printf("conv:%s hold:%d check:%s\n",conv,hold,check);  */
					}
					break;
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
					{
						int			k;
						long		octVal = 0;

						for (k = 0;
							 s[i + k] >= '0' && s[i + k] <= '7' && k < 3;
							 k++)
							octVal = (octVal << 3) + (s[i + k] - '0');
						i += k - 1;
						newStr[j] = ((char) octVal);
					}
					break;
				default:
					newStr[j] = s[i];
					break;
			}					/* switch */
		}						/* s[i] == '\\' */
		else
			newStr[j] = s[i];
		j++;
	}
	newStr[j] = '\0';
	return newStr;
}
