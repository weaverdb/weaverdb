/*-------------------------------------------------------------------------
 *
 * dbcommands.c
 *
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
#include "postgres.h"
#include "env/env.h"

#include "commands/dbcommands.h"
#include <errno.h>
#include <fcntl.h>


#include <unistd.h>
#include <sys/stat.h>


#include "access/heapam.h"
#include "access/htup.h"
#include "access/skey.h"
#include "access/xact.h"
#include "catalog/catname.h"
#include "catalog/indexing.h"
#include "catalog/pg_database.h"
#include "catalog/pg_schema.h"
#include "catalog/pg_shadow.h"
#include "commands/comment.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"		/* for DropBuffers */
#include "storage/sinval.h"		/* for DatabaseHasActiveBackends */
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/palloc.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "commands/defrem.h"
#include "commands/creatinh.h"
#include "env/poolsweep.h"
#include "catalog/heap.h"


/* non-export function prototypes */
static bool
get_user_info(const char *name, int4 *use_sysid, bool *use_super, bool *use_createdb);

static bool
get_db_info(const char *name, char *dbpath, Oid *dbIdP, int4 *ownerIdP);



/*
 * CREATE DATABASE
 */

void
createdb(const char *dbname, const char *dbpath, int encoding)
{
	char		buf[2 * MAXPGPATH + 100];
	char	   *loc;
	char		locbuf[512];
	int4		user_id;
	bool		use_super,
				use_createdb;
	Relation	pg_database_rel;
	HeapTuple	tuple;
	TupleDesc	pg_database_dsc;
	Datum		new_record[Natts_pg_database];
	char		new_record_nulls[Natts_pg_database] = {' ', ' ', ' ', ' '};
/* due raw database file reads, be careful here  */
	SetTransactionCommitType(TRANSACTION_SYNCED_COMMIT);
        if (!get_user_info(GetPgUserName(), &user_id, &use_super, &use_createdb))
		elog(ERROR, "current user name is invalid");

	if (!use_createdb && !use_super)
		elog(ERROR, "CREATE DATABASE: permission denied");

        if (get_db_info(dbname, NULL, NULL, NULL))
		elog(ERROR, "CREATE DATABASE: database \"%s\" already exists", dbname);

	/* don't call this in a transaction block */
	if (IsTransactionBlock())
		elog(ERROR, "CREATE DATABASE: may not be called in a transaction block");

	/* Generate directory name for the new database */
	if (dbpath == NULL || strcmp(dbpath, dbname) == 0)
		strcpy(locbuf, dbname);
	else
		snprintf(locbuf, sizeof(locbuf), "%s/%s", dbpath, dbname);

	loc = ExpandDatabasePath(locbuf);

	if (loc == NULL)
		elog(ERROR,
			 "The database path '%s' is invalid. "
			 "This may be due to a character that is not allowed or because the chosen "
			 "path isn't permitted for databases", dbpath);


	/*
	 * Insert a new tuple into pg_database
	 */
	pg_database_rel = heap_openr(DatabaseRelationName, AccessExclusiveLock);
	pg_database_dsc = RelationGetDescr(pg_database_rel);

	/* Form tuple */
	new_record[Anum_pg_database_datname - 1] = NameGetDatum(namein(dbname));
	new_record[Anum_pg_database_datdba - 1] = Int32GetDatum(user_id);
	new_record[Anum_pg_database_encoding - 1] = Int32GetDatum(encoding);
	new_record[Anum_pg_database_datpath - 1] = PointerGetDatum(textin(locbuf));

	tuple = heap_formtuple(pg_database_dsc, new_record, new_record_nulls);

	/*
	 * Update table
	 */
	heap_insert(pg_database_rel, tuple);

	/*
	 * Update indexes (there aren't any currently)
	 */
#ifdef Num_pg_database_indices
	if (RelationGetForm(pg_database_rel)->relhasindex)
	{
		Relation	idescs[Num_pg_database_indices];

		CatalogOpenIndices(Num_pg_database_indices,
						   Name_pg_database_indices, idescs);
		CatalogIndexInsert(idescs, Num_pg_database_indices, pg_database_rel,
						   tuple);
		CatalogCloseIndices(Num_pg_database_indices, idescs);
	}
#endif

	heap_close(pg_database_rel, NoLock);

	/* Copy the template database to the new location */

	if (mkdir(loc, S_IRWXU) != 0)
		elog(ERROR, "CREATE DATABASE: unable to create database directory '%s': %s", loc, strerror(errno));

	snprintf(buf, sizeof(buf), "cp %s%cbase%ctemplate1%c* '%s'",
			 DataDir, SEP_CHAR, SEP_CHAR, SEP_CHAR, loc);
	if (my_system(buf) != 0)
	{
		int			ret;

                snprintf(buf, sizeof(buf), "rm -r '%s'", loc);

		ret = my_system(buf);
		if (ret == 0 )
			elog(ERROR, "CREATE DATABASE: could not initialize database directory");
		else
			elog(ERROR, "CREATE DATABASE: Could not initialize database directory. Delete failed as well");
	}
}

/*
 * CREATE SCHEMA
 */

void
createschema(const char *schemaname,int encoding)
{
	char		buf[2 * MAXPGPATH + 100];
	char		locbuf[512];
	int		errno;
        Datum 		new_schema[Natts_pg_schema];
        char		new_nulls[Natts_pg_schema];
        int             ct = 0;
        int4             user_id;
        bool            use_super,use_createdb;
        Relation 	schema_relation;
        TupleDesc	schema_dsc;
        HeapTuple 	tuple;

        for(ct = 0;ct<Natts_pg_schema;ct++) {
            new_schema[ct] = (Datum)NULL;
            new_nulls[ct] = ' ';
        }

        if (!get_user_info(GetPgUserName(), &user_id, &use_super, &use_createdb))
		elog(ERROR, "current user name is invalid");

        memset(buf,0,2 * MAXPGPATH + 100);
        memset(locbuf,0,512);
        strcpy(locbuf, schemaname);
	snprintf(buf, sizeof(buf), "%s%c%s", GetDatabasePath(), SEP_CHAR,locbuf);

	/*
	 * Insert a new tuple into pg_database
	 */
	schema_relation = heap_openr(SchemaRelationName, AccessExclusiveLock);
	schema_dsc = RelationGetDescr(schema_relation);

	/* Form tuple */
	new_schema[Anum_pg_schema_schemaname - 1] = NameGetDatum(namein(locbuf));
	new_schema[Anum_pg_schema_owner - 1] = Int32GetDatum(user_id);
	new_schema[Anum_pg_schema_encoding - 1] = Int32GetDatum(encoding);
	new_schema[Anum_pg_schema_database - 1] = LongGetDatum(GetDatabaseId());
	new_schema[Anum_pg_schema_datpath - 1] = PointerGetDatum(textin(locbuf));

	tuple = heap_formtuple(schema_dsc, new_schema, new_nulls);
	/*
	 * Update table
	 */
	heap_insert(schema_relation, tuple);
		
        heap_close(schema_relation,NoLock);

	if ((errno = mkdir(buf, S_IRWXU)) != 0) {
		elog(ERROR, "CREATE SCHEMA: unable to create database directory '%s': %s", buf, strerror(errno));
	}
}

void
dropschema(const char *schemaname)
{
        ScanKeyData     key;
        Relation        rel = heap_openr(RelationRelationName, AccessShareLock);
        TupleDesc       desc = RelationGetDescr(rel);
        HeapScanDesc    scan;
        HeapTuple       tup;
        text*		ele;
        List*		tables = NULL;
        List*		indexes = NULL;
        List*		seq = NULL;
        List*		rels = NULL;        
        List*		arg = NULL;
        char		schname[NAMEDATALEN];
        NameData	schema_name;        
        
        MemoryContext oldcxt = MemoryContextSwitchTo(MemoryContextGetEnv()->QueryContext);

        strncpy(schname,schemaname,NAMEDATALEN);
        *(schname+strlen(schname)) = SEP_CHAR;
        *(schname+strlen(schname)) = '%';
        *(schname+strlen(schname)) = '\0';
         
        ele = textin(schname);
/*	 get a list of tables and sequences in this schema 	*/	
        ScanKeyEntryInitialize(&key,0,Anum_pg_class_relname,F_NAMELIKE,PointerGetDatum(ele));
        scan = (HeapScanDesc)heap_beginscan(rel,SnapshotSelf,1,&key);
        tup = (HeapTuple)heap_getnext(scan);
        
        while ( HeapTupleIsValid(tup) ) {
            bool isnull = false;
/*  add them to the list to be terminated   */		
            char* name = pstrdup(NameStr(*DatumGetName(HeapGetAttr(tup,Anum_pg_class_relname,desc,&isnull))));
            char  type = (char)DatumGetChar(HeapGetAttr(tup,Anum_pg_class_relkind,desc,&isnull));
            switch ( type ) {
                case RELKIND_INDEX:
                    indexes = lcons(name,indexes);
                    break;
                case RELKIND_RELATION: 
                    tables = lcons(name,tables);
                    break;
                case RELKIND_SEQUENCE:
                    seq = lcons(name,seq);
                    break;
                default:
                    elog(ERROR,"unknown relation type");
            }
                
            tup = (HeapTuple)heap_getnext(scan);
        }
        heap_endscan(scan);
        heap_close(rel,NoLock);
        
        foreach(arg,indexes) {
            char* name = (char*)lfirst(arg);
            RemoveIndex(name);
        }
/*  need to do this so the previous deletes are visible to the relation catalog
    I have no idea if this is correct but it works for now.
    MKS 07.15.2002 
*/        
        CommandCounterIncrement();
        foreach(arg,tables) {
            char* name = (char*)lfirst(arg);
            RemoveSchemaInheritance(name);
        }
        
        CommandCounterIncrement();
        foreach(arg,tables) {
            char* name = (char*)lfirst(arg);
            RemoveRelation(name);
        }
/*  need to do this so the previous deletes are visible to the relation catalog
    I have no idea if this is correct but it works for now.
    MKS 07.15.2002 
*/
        CommandCounterIncrement();
        foreach(arg,seq) {
            char* name = (char*)lfirst(arg);
            RemoveRelation(name);
        }
/*  need to do this so the previous deletes are visible to the relation calalog
    I have no idea if this is correct but it works for now.
    MKS 07.15.2002 
*/
        CommandCounterIncrement();        
        {
            rel = heap_openr(SchemaRelationName, AccessExclusiveLock);
            namestrcpy(&schema_name,schemaname);
            ScanKeyEntryInitialize(&key, 0, Anum_pg_schema_schemaname,
			F_NAMEEQ, NameGetDatum(&schema_name));

            scan = heap_beginscan(rel, SnapshotNow, 1, &key);

            tup = heap_getnext(scan);
            if (!HeapTupleIsValid(tup)) {
                elog(ERROR,"schema: %s not found",schemaname);
            }

            heap_delete(rel, &tup->t_self, NULL,NULL);
            heap_endscan(scan);
            heap_close(rel, NoLock);    
        }

		
        MemoryContextSwitchTo(oldcxt);
        {			
            char		buf[2 * MAXPGPATH + 100];
            char		locbuf[512];
            int                 err;

            memset(locbuf,0,512);
            memset(buf,0,2 * MAXPGPATH + 100);
            strncpy(locbuf, schemaname,strlen(schemaname));

            snprintf(buf, sizeof(buf), "%s%c%s", GetDatabasePath(), SEP_CHAR,locbuf);
    /*   terminate the dir you made for the schema   */
            err = rmdir(buf);
            if (err != 0) {
                elog(ERROR, "DROP SCHEMA: unable to remove database directory '%s': %s", buf, strerror(err));
                perror("DROP SCHEMA");
            }
        }

}


/*
 * DROP DATABASE
 */

void
dropdb(const char *dbname)
{
	int4		user_id,
				db_owner;
	bool		use_super,use_createdb;
	Oid			db_id;
	char	   *path,
				dbpath[MAXPGPATH],
				buf[MAXPGPATH + 100];
	Relation	pgdbrel;
	HeapScanDesc pgdbscan;
	ScanKeyData key;
	HeapTuple	tup;

	AssertArg(dbname);
/* database ops need to be careful */
	SetTransactionCommitType(TRANSACTION_SYNCED_COMMIT);
	if (strcmp(dbname, "template1") == 0)
		elog(ERROR, "DROP DATABASE: May not be executed on the template1 database");

	if (strcmp(dbname, GetDatabaseName()) == 0)
		elog(ERROR, "DROP DATABASE: Cannot be executed on the currently open database");

	if (IsTransactionBlock())
		elog(ERROR, "DROP DATABASE: May not be called in a transaction block");

	if (!get_user_info(GetPgUserName(), &user_id, &use_super, &use_createdb) )
		elog(ERROR, "Current user name is invalid");

	if (!get_db_info(dbname, dbpath, &db_id, &db_owner))
		elog(ERROR, "DROP DATABASE: Database \"%s\" does not exist", dbname);
        
	if (user_id != db_owner && !use_super)
		elog(ERROR, "DROP DATABASE: Permission denied");

        path = ExpandDatabasePath(dbpath);
	if (path == NULL)
		elog(ERROR,
			 "The database path '%s' is invalid. "
			 "This may be due to a character that is not allowed or because the chosen "
			 "path isn't permitted for databases", path);

	/*
	 * Obtain exclusive lock on pg_database.  We need this to ensure that
	 * no new backend starts up in the target database while we are
	 * deleting it.  (Actually, a new backend might still manage to start
	 * up, because it will read pg_database without any locking to
	 * discover the database's OID.  But it will detect its error in
	 * ReverifyMyDatabase and shut down before any serious damage is done.
	 * See postinit.c.)
	 */
	pgdbrel = heap_openr(DatabaseRelationName, AccessExclusiveLock);

	/*
	 * Check for active backends in the target database.
	 */
        DropVacuumRequests(InvalidOid,db_id);
        StopPoolsweepsForDB(db_id);
        
        if (DatabaseHasActiveBackends(db_id))
	{
		heap_close(pgdbrel, AccessExclusiveLock);
		elog(ERROR, "DROP DATABASE: Database \"%s\" is being accessed by other users", dbname);
	}

	/*
	 * Find the database's tuple by OID (should be unique, we trust).
	 */
	ScanKeyEntryInitialize(&key, 0, ObjectIdAttributeNumber,
						   F_OIDEQ, ObjectIdGetDatum(db_id));

	pgdbscan = heap_beginscan(pgdbrel, SnapshotNow, 1, &key);

	tup = heap_getnext(pgdbscan);
	if (!HeapTupleIsValid(tup))
	{
		heap_close(pgdbrel, AccessExclusiveLock);

		/*
		 * This error should never come up since the existence of the
		 * database is checked earlier
		 */
		elog(ERROR, "DROP DATABASE: Database \"%s\" doesn't exist despite earlier reports to the contrary",
			 dbname);
	}

	/* Delete any comments associated with the database */
	DeleteComments(db_id);

	/* Remove the database's tuple from pg_database */
	heap_delete(pgdbrel, &tup->t_self, NULL, NULL);

	heap_endscan(pgdbscan);

	/*
	 * Close pg_database, but keep exclusive lock till commit to ensure
	 * that any new backend scanning pg_database will see the tuple dead.
	 */
	heap_close(pgdbrel, NoLock);

	/*
	 * Drop pages for this database that are in the shared buffer cache.
	 * This is important to ensure that no remaining backend tries to
	 * write out a dirty buffer to the dead database later...
	 */
	DropBuffers(db_id);

	/*
	 * Remove the database's subdirectory and everything in it.
	 */
	snprintf(buf, sizeof(buf), "rm -rf '%s'", path);
	if (my_system(buf) != 0)
		elog(NOTICE, "DROP DATABASE: The database directory '%s' could not be removed", path);
}



/*
 * Helper functions
 */

static bool
get_db_info(const char *name, char *dbpath, Oid *dbIdP, int4 *ownerIdP)
{
	Relation	relation;
	HeapTuple	tuple;
	ScanKeyData scanKey;
	HeapScanDesc scan;

	AssertArg(name);

	relation = heap_openr(DatabaseRelationName, AccessExclusiveLock /* ??? */ );

	ScanKeyEntryInitialize(&scanKey, 0, Anum_pg_database_datname,
						   F_NAMEEQ, NameGetDatum(name));

	scan = heap_beginscan(relation, SnapshotNow, 1, &scanKey);
	if (!HeapScanIsValid(scan))
		elog(ERROR, "Cannot begin scan of %s.", DatabaseRelationName);

	tuple = heap_getnext(scan);

	if (HeapTupleIsValid(tuple))
	{
		text	   *tmptext;
		bool		isnull;

		/* oid of the database */
		if (dbIdP)
			*dbIdP = tuple->t_data->t_oid;
		/* uid of the owner */
		if (ownerIdP)
		{
			*ownerIdP = (int4) HeapGetAttr(tuple,
											Anum_pg_database_datdba,
											RelationGetDescr(relation),
											&isnull);
			if (isnull)
				*ownerIdP = -1; /* hopefully no one has that id already ;) */
		}
		/* database path (as registered in pg_database) */
		if (dbpath)
		{
			tmptext = (text *) HeapGetAttr(tuple,
											Anum_pg_database_datpath,
											RelationGetDescr(relation),
											&isnull);

			if (!isnull)
			{
				Assert(VARSIZE(tmptext) - VARHDRSZ < MAXPGPATH);

				strncpy(dbpath, VARDATA(tmptext), VARSIZE(tmptext) - VARHDRSZ);
				*(dbpath + VARSIZE(tmptext) - VARHDRSZ) = '\0';
			}
			else
				strcpy(dbpath, "");
		}
	}
	else
	{
		if (dbIdP)
			*dbIdP = InvalidOid;
	}

	heap_endscan(scan);

	/* We will keep the lock on the relation until end of transaction. */
	heap_close(relation, NoLock);

	return HeapTupleIsValid(tuple);
}



static bool
get_user_info(const char *name, int4 *use_sysid, bool *use_super, bool *use_createdb)
{
	HeapTuple	utup;

	AssertArg(name);
	utup = SearchSysCacheTuple(SHADOWNAME,
							   PointerGetDatum(name),
							   0, 0, 0);

	if (!HeapTupleIsValid(utup)) {
            if ( use_sysid ) *use_sysid = 0;
            if ( use_super ) *use_super = false;
            if ( use_createdb ) *use_createdb = true;
        } else {
            if (use_sysid)
                    *use_sysid = ((Form_pg_shadow) GETSTRUCT(utup))->usesysid;
            if (use_super)
                    *use_super = ((Form_pg_shadow) GETSTRUCT(utup))->usesuper;
            if (use_createdb)
                    *use_createdb = ((Form_pg_shadow) GETSTRUCT(utup))->usecreatedb;
        }

	return true;
}


