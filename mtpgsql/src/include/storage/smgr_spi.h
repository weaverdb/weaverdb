/* 
 * File:   smgr_spi.h
 * Author: mscott
 *
 * Created on July 28, 2006, 10:05 AM
 */

#ifndef _smgr_spi_H
#define	_smgr_spi_H

#include "c.h"

#include "nodes/memnodes.h"


typedef struct smgrdata {
    int16       which;

    int         fd;
    bool        unlinked;
    char        relkind;
    
    long        nblocks;
        
    Oid         relid;
    Oid         dbid;
    NameData    relname;
    NameData    dbname;
    void*       info;
} SmgrData;


PG_EXTERN MemoryContext GetSmgrMemoryContext(void);
PG_EXTERN int   smgraddrecoveredpage(char* dbname, Oid dbid, Oid relid, BlockNumber block);
#endif	/* _smgr_spi_H */

