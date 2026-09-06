/*-------------------------------------------------------------------------
 *
 * weaver_embed_sql.c
 *	  Test-only SQL driver for the crash-consistency harness.
 *
 * Starts the engine through initweaverbackend() / GoMultiuser() so the
 * shadow log (pg_shadowlog) is on, then runs stdin SQL via the public
 * embed API. This binary is not production; it is the SIGKILL target
 * for index_heap_crash_consistency.sh. Initdb still uses CLI postgres.
 *
 * Usage: weaver_embed_sql -D datadir dbname  < sql
 *
 * SELECT output matches CLI debugtup enough for the harness parser:
 *   col = "value"
 *
 *-------------------------------------------------------------------------
 */

#ifndef BLCKSZ
#define BLCKSZ 8192
#endif
#ifndef LIB_EXTERN
#define LIB_EXTERN
#endif

#include <stddef.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "WeaverInterface.h"
#include "connectionutil.h"

struct colxfer {
	char name[64];
	int have_name;
};

static int
is_blank(const char *s)
{
	while (*s != '\0') {
		if (!isspace((unsigned char) *s))
			return 0;
		s++;
	}
	return 1;
}

static const char *
skip_ws(const char *s)
{
	while (*s != '\0' && isspace((unsigned char) *s))
		s++;
	return s;
}

static int
is_select(const char *sql)
{
	sql = skip_ws(sql);
	return (strncasecmp(sql, "select", 6) == 0 &&
		(sql[6] == '\0' || isspace((unsigned char) sql[6])));
}

static void
report_error(OpaqueWConn conn, const char *sql)
{
	const char *text = WGetErrorText(conn);
	int code = (int) WGetErrorCode(conn);

	if (text == NULL || text[0] == '\0')
		text = "unknown error";
	printf("ERROR: %s\n", text);
	fflush(stdout);
	fprintf(stderr, "ERROR: %s (code=%d) sql=%.80s\n", text, code, sql);
	fflush(stderr);
}

static int
out_transfer(void *userenv, int varType, void *varAdd, int varSize)
{
	struct colxfer *c = (struct colxfer *) userenv;

	if (varSize < 0)
		return 0;
	if (varAdd == NULL)
		return 0;

	if (varType == METANAMETYPE) {
		size_t n = (size_t) varSize;

		if (n >= sizeof(c->name))
			n = sizeof(c->name) - 1;
		memcpy(c->name, varAdd, n);
		c->name[n] = '\0';
		c->have_name = 1;
		return varSize;
	}

	printf("\t%s = \"%.*s\"\n",
		   c->have_name ? c->name : "col",
		   varSize, (char *) varAdd);
	fflush(stdout);
	return varSize;
}

static int
exec_select(OpaqueWConn conn, const char *sql)
{
	OpaquePreparedStatement plan;
	struct colxfer col;
	long rc;

	memset(&col, 0, sizeof(col));

	if (WBegin(conn, 0) != 0) {
		report_error(conn, sql);
		WRollback(conn);
		return 1;
	}

	plan = WPrepareStatement(conn, sql);
	if (plan == NULL) {
		report_error(conn, sql);
		WRollback(conn);
		return 1;
	}

	WOutputTransfer(plan, 1, VARCHARTYPE, &col, out_transfer);
	if (WGetErrorCode(conn) != 0) {
		report_error(conn, sql);
		WDestroyPreparedStatement(plan);
		WRollback(conn);
		return 1;
	}

	rc = WExec(plan);
	if (rc != 0 || WGetErrorCode(conn) != 0) {
		report_error(conn, sql);
		WDestroyPreparedStatement(plan);
		WRollback(conn);
		return 1;
	}

	for (;;) {
		rc = WFetch(plan);
		if (rc == 4)
			break;
		if (rc != 0 || WGetErrorCode(conn) != 0) {
			report_error(conn, sql);
			WDestroyPreparedStatement(plan);
			WRollback(conn);
			return 1;
		}
	}

	WDestroyPreparedStatement(plan);
	if (WCommit(conn) != 0) {
		report_error(conn, sql);
		return 1;
	}
	return 0;
}

static int
exec_sql(OpaqueWConn conn, const char *sql)
{
	sql = skip_ws(sql);
	if (*sql == '\0')
		return 0;

	if (is_select(sql))
		return exec_select(conn, sql);

	if (WStreamExec(conn, sql) != 0 || WGetErrorCode(conn) != 0) {
		report_error(conn, sql);
		WRollback(conn);
		return 1;
	}
	return 0;
}

/*
 * Split stdin into statements on ';' that are not inside single quotes.
 * The harness emits one statement per line; this also accepts a batch.
 */
static int
run_stdin(OpaqueWConn conn)
{
	char *buf = NULL;
	size_t cap = 0;
	size_t len = 0;
	int in_quote = 0;
	int c;
	int rc = 0;

	while ((c = fgetc(stdin)) != EOF) {
		if (len + 1 >= cap) {
			size_t ncap = cap == 0 ? 4096 : cap * 2;
			char *nbuf = realloc(buf, ncap);

			if (nbuf == NULL) {
				fprintf(stderr, "weaver_embed_sql: out of memory\n");
				free(buf);
				return 2;
			}
			buf = nbuf;
			cap = ncap;
		}

		if (in_quote) {
			buf[len++] = (char) c;
			if (c == '\'') {
				int next = fgetc(stdin);

				if (next == '\'') {
					if (len + 1 >= cap) {
						size_t ncap = cap * 2;
						char *nbuf = realloc(buf, ncap);

						if (nbuf == NULL) {
							fprintf(stderr, "weaver_embed_sql: out of memory\n");
							free(buf);
							return 2;
						}
						buf = nbuf;
						cap = ncap;
					}
					buf[len++] = '\'';
				} else {
					in_quote = 0;
					if (next != EOF)
						ungetc(next, stdin);
				}
			}
			continue;
		}

		if (c == '\'') {
			buf[len++] = (char) c;
			in_quote = 1;
			continue;
		}

		if (c == ';') {
			buf[len] = '\0';
			if (!is_blank(buf) && exec_sql(conn, buf) != 0)
				rc = 1;
			len = 0;
			continue;
		}

		buf[len++] = (char) c;
	}

	if (len > 0) {
		buf[len] = '\0';
		if (!is_blank(buf) && exec_sql(conn, buf) != 0)
			rc = 1;
	}

	free(buf);
	return rc;
}

static void
mark_ready(void)
{
	const char *gate = getenv("WEAVER_CRASH_GATE");
	int fd;

	if (gate != NULL && gate[0] != '\0') {
		fd = open(gate, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (fd >= 0)
			close(fd);
	}
	fprintf(stderr, "weaver_embed_sql: ready\n");
	fflush(stderr);
}

static void
quiet_wrapup(void)
{
	int nfd;

	fflush(stdout);
	fflush(stderr);
	nfd = open("/dev/null", O_WRONLY);
	if (nfd >= 0) {
		dup2(nfd, STDOUT_FILENO);
		dup2(nfd, STDERR_FILENO);
		close(nfd);
	}
	if (prepareforshutdown())
		wrapupweaverbackend();
}

int
main(int argc, char **argv)
{
	const char *datadir = getenv("PGDATA");
	const char *dbname = "template1";
	char vars[4096];
	OpaqueWConn conn;
	int i;
	int rc;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-D") == 0 && i + 1 < argc)
			datadir = argv[++i];
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			fprintf(stderr, "usage: %s -D datadir [dbname]\n", argv[0]);
			return 2;
		} else if (argv[i][0] != '-')
			dbname = argv[i];
		else {
			fprintf(stderr, "usage: %s -D datadir [dbname]\n", argv[0]);
			return 2;
		}
	}

	if (datadir == NULL || datadir[0] == '\0') {
		fprintf(stderr, "weaver_embed_sql: datadir required (-D or PGDATA)\n");
		return 2;
	}

	/*
	 * transcareful=false -> SOFT_COMMIT, the production multiuser path:
	 * pages go through pg_shadowlog, then xid COMMIT.
	 */
	if (snprintf(vars, sizeof(vars),
				 "datadir=%s;allow_anonymous=true;disable_crc=TRUE;"
				 "transcareful=false;stdlog=FALSE;",
				 datadir) >= (int) sizeof(vars)) {
		fprintf(stderr, "weaver_embed_sql: datadir too long\n");
		return 2;
	}

	if (!initweaverbackend(vars)) {
		fprintf(stderr, "weaver_embed_sql: initweaverbackend failed for %s\n",
				datadir);
		return 2;
	}

	conn = WCreateConnection("", "", dbname);
	if (conn == NULL || !WIsValidConnection(conn)) {
		fprintf(stderr, "weaver_embed_sql: cannot connect to %s\n", dbname);
		quiet_wrapup();
		return 2;
	}

	mark_ready();

	rc = run_stdin(conn);

	WDestroyConnection(conn);
	/*
	 * Skip wrapupweaverbackend() after a SQL error. The test shutdown
	 * path can abort and leave the datadir looking like a failed initdb
	 * (bootstrap xid not committed). That is not a crash-recovery case:
	 * production expects a successful initdb to persist that xid, and
	 * otherwise you re-initdb. Keep the last successful wrapup as the
	 * durable baseline. Crash children never reach this (SIGKILL).
	 */
	if (rc == 0)
		quiet_wrapup();
	return 0;
}
