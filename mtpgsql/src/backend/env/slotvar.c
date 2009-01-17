/*-------------------------------------------------------------------------
 *
 * slotvar.c
 *	  slotvariables
 *
 *-------------------------------------------------------------------------
 */
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <float.h>

#include "postgres.h"
#include "env/env.h"
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include "env/slotvar.h"
#include "utils/builtins.h"

/*----------------------------------------------------------
 * Formatting and conversion routines.
 *---------------------------------------------------------*/

/* slotvar_in()
 */
void *
slotvar_in(char *str)
{
        
	int*		result,ret;
	char	   *ptr = str;

	if (!PointerIsValid(str))
		elog(ERROR, "Bad (null) slotvar external representation");
        
        while ( isspace(*str) ) str++;
        
	if ( strcmp(str,"NIL") == 0 ) {
            result = ret = palloc(8);
            *result++ = 8;
            *result = SYNNIL;
        } else if ( strcmp(str,"ARRAY") == 0 ) {
            result = ret = palloc(8);
            *result++ = 8;
            *result = SYNARRAY;
         } else if ( strcmp(str,"PATTERN") == 0 ) {
            result = ret = palloc(8);
            *result++ = 8;
            *result = SYNPATTERN;
         }  else if ( strcmp(str,"f") == 0 ) {
            result = ret = palloc(12);
            *result++ = 12;
            *result++ = SYNBOOLEAN;
            *result = 0x00000000;
         }  else if ( strcmp(str,"t") == 0 ) {
            result = ret = palloc(12);
            *result++ = 12;
            *result = SYNBOOLEAN;
            *result = 0x7fffffff;
        } else if ( isalpha(*str) ) {
            int len = strlen(str);
            printf("%i\n",len);            
            result = ret = palloc(MAXALIGN(8 + len + 2));
            *result++ = 8 + len;
            *result++ = SYNSTRING;
            strncpy((char*)result,str,len + 2);
        } else {
            elog(ERROR,"unparseable slot variable");
        }
	return ret;
}	/* slotvar_in() */

void*
booltoslot(bool val)
{
    int* result,ret;
    result = ret = palloc(9);
    *result++ = 9;
    *result++ = SYNBOOLEAN;
    *result = val;
    return ret;
}

void*
arraytoslot(void* val)
{
    int* result,ret;
    result = ret = palloc(8);
    *result++ = 8;
    *result = SYNARRAY;
    return ret;
}

void*
patterntoslot(void* val)
{
    int* result,ret;
    result = ret = palloc(8);
    *result++ = 8;
    *result++ = SYNPATTERN;
    return ret;
}

void*
int4toslot(int val)
{
    int* result,ret;
    result = ret = palloc(12);
    *result++ = 12;
    *result++ = SYNINTEGER;
    *result = val;
    return ret;
}

void*
varchartoslot(struct varlena* var)
{
    int* result,ret;
    result = ret = palloc(VARSIZE(var) + 4);
    *result++ = VARSIZE(var) + 4;
    *result++ = SYNSTRING;
    memcpy(result,VARDATA(var),VARSIZE(var) - 4);
    return ret;
}


void*
byteatoslot(struct varlena* var)
{
    int* result,ret;
    result = ret = palloc(VARSIZE(var) + 4);
    *result++ = VARSIZE(var) + 4;
    *result++ = SYNBINARYOBJECT;
    memcpy(result,VARDATA(var),VARSIZE(var) - 4);
    return ret;
}

void*
texttoslot(struct varlena* var)
{
    int* result,ret;
    result = ret = palloc(VARSIZE(var) + 4);
    *result++ = VARSIZE(var) + 4;
    *result++ = SYNBLOB;
    memcpy(result,VARDATA(var),VARSIZE(var) - 4);
    return ret;
}



/* slotvar_out()
 */
char *
slotvar_out(void *val)
{
	char	   *result;
        int*		temp = (int*)val;

	int len = *temp++;
        int type = *temp++;
        
        switch(type) {
            case SYNPATTERN:
                result = pstrdup("PATTERN");
                break;
            case SYNARRAY:
                result = pstrdup("ARRAY");
                break;
            case SYNCHARACTER:
                result = palloc(1);
                *result = *(((char*)val) + len - 1);
                break;
            case SYNBOOLEAN:
                if ( *temp ) {
                    result = pstrdup("TRUE");
                } else {
                    result = pstrdup("FALSE");
                }
            case SYNNIL:
                result = pstrdup("NIL");
                break;
            case SYNSTRING:
                result = palloc(len - 7);
                memcpy(result,(char*)temp,len - 8);
                *(result + len - 8) = '\0'; 
                break;
            case SYNINTEGER:
                result = palloc(64);
                snprintf(result,64,"%i",*temp);
                break;
            default:
                result = pstrdup("<binary data>");
                break;
        }
        
        return result;
        
}	/* slotvar_out() */


/*----------------------------------------------------------
 *	Relational operators for int8s.
 *---------------------------------------------------------*/

/* int8relop()
 * Is val1 relop val2?
 */
bool
slotvareq(void *val1, void *val2)
{
            int d1 = *((int*)val1)++;
            int d2 = *((int*)val2)++;

            int t1 = *((int*)val1)++;
            int t2 = *((int*)val2)++;
     /*  compare type */       
            if ( t1 != t2 ) {
                return false;
            }
     /*  compare length or value  */
            
            if ( d1 != d2 ) {
                return false;
            }
    /*  compare variable mem if string or binary  */
            switch ( t1 ) {
                case SYNNIL:
                case SYNPATTERN:
                case SYNARRAY:
                case SYNCHARACTER:
                case SYNINTEGER:
                case SYNBOOLEAN:
                    return true;
                default:
                    return memcmp(val1,val2,d1);
            }
}

bool
slotvarneq(void *val1, void *val2)
{
            int d1 = *((int*)val1)++;
            int d2 = *((int*)val2)++;

            int t1 = *((int*)val1)++;
            int t2 = *((int*)val2)++;
     /*  compare type */       
            if ( t1 != t2 ) {
                return true;
            }
     /*  compare length or value  */
            
            if ( d1 != d2 ) {
                return true;
            }
    /*  compare variable mem if string or binary  */
            switch ( t1 ) {
                case SYNNIL:
                case SYNPATTERN:
                case SYNARRAY:
                case SYNCHARACTER:
                case SYNINTEGER:
                case SYNBOOLEAN:
                    return false;
                default:
                    return !memcmp(val1,val2,d1);
            }
}	

bool
slotvarlike(void *val1, void *val2)
{
            int d1 = *((int*)val1)++;
            int d2 = *((int*)val2)++;

            int t1 = *((int*)val1);
            int t2 = *((int*)val2);
     /*  compare type */       
            if ( t1 != t2 ) {
                return false;
            }
     /*  compare length or value  */
            
            if ( d1 != d2 ) {
                return false;
            }
    /*  compare variable mem if string or binary  */
            switch ( t1 ) {
                case SYNNIL:
                case SYNPATTERN:
                case SYNARRAY:
                case SYNCHARACTER:
                case SYNINTEGER:
                case SYNBOOLEAN:
                    return false;
                default:
                    {
                    bool like = false;
                    *(int*)val1 = d1;
                    *(int*)val2 = d2;
                    like = textlike((struct varlena*)val1,(struct varlena*)val2);
                    *(int*)val1 = t1;
                    *(int*)val2 = t2;
                    return like;
                    }
            }
}	

bool
slotvarnlike(void *val1, void *val2)
{
            int d1 = *((int*)val1)++;
            int d2 = *((int*)val2)++;
            int t1 = *((int*)val1);
            int t2 = *((int*)val2);
     /*  compare type */       
            if ( t1 != t2 ) {
                return true;
            }
     /*  compare length or value  */
            
            if ( d1 != d2 ) {
                return true;
            }
    /*  compare variable mem if string or binary  */
            switch ( t1 ) {
                case SYNNIL:
                case SYNPATTERN:
                case SYNARRAY:
                case SYNCHARACTER:
                case SYNINTEGER:
                case SYNBOOLEAN:
                    return false;
                default:
                    {
                    bool like = false;
                    *(int*)val1 = d1;
                    *(int*)val2 = d2;
                    like = !textlike((struct varlena*)val1,(struct varlena*)val2);
                    *(int*)val1 = t1;
                    *(int*)val2 = t2;
                    return like;
                    }
            }
}	

PG_EXTERN bool vctosloteq(void *val1, struct varlena* val2)
{
    int* temp = (int*)val1;
    if ( *temp++ != VARSIZE(val2) ) return false;
    if ( *temp++ != SYNSTRING ) return false;
    
    return (memcmp((char*)temp,VARDATA(val2),VARSIZE(val2)));
}

PG_EXTERN bool vctoslotneq(void *val1, struct varlena* val2)
{
    int* temp = (int*)val1;
    if ( *temp++ == VARSIZE(val2) ) return false;
    if ( *temp++ == SYNSTRING ) return false;
    
    return !(memcmp((char*)temp,VARDATA(val2),VARSIZE(val2)));
}

PG_EXTERN bool vctoslotlike(void *val1, struct varlena* val2)
{
    bool like = false;
    
    int* temp = (int*)val1;
    int len = *temp++;
    int type = *temp;
    
    if ( len != VARSIZE(val2) ) return false;
    if ( type != SYNSTRING ) return false;
    
    *temp = len;
    like = textlike((struct varlena*)temp,val2);
    *temp = type;
    return like;
}

PG_EXTERN bool vctoslotnlike(void *val1, struct varlena* val2)
{
    return !vctoslotlike(val1,val2);
}

PG_EXTERN bool inttosloteq(void *val1, int val2)
{
    int* temp = (int*)val1;
    int len = *temp++;
    int type = *temp++;

    if ( type != SYNINTEGER ) return false;
    
    return ( *temp == val2);
}

PG_EXTERN bool inttoslotneq(void *val1, int val2)
{
    return !inttosloteq( val1,val2);
}

PG_EXTERN bool inttoslotgt(void *val1, int val2)
{
    return false;
}

PG_EXTERN bool inttoslotlt(void *val1, int val2)
{
    return false;
}

PG_EXTERN bool booltosloteq(void *val1, bool val2)
{
    return false;

}

PG_EXTERN bool booltoslotneq(void *val1, bool val2)
{
    return false;
}
