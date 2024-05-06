/*-------------------------------------------------------------------------
 *
 * pqcomm.c
 *	  Communication functions between the Frontend and the Backend
 *
 * These routines handle the low-level details of communication between
 * frontend and backend.  They just shove data across the communication
 * channel, and are ignorant of the semantics of the data --- or would be,
 * except for major brain damage in the design of the COPY OUT protocol.
 * Unfortunately, COPY OUT is designed to commandeer the communication
 * channel (it just transfers data without wrapping it into messages).
 * No other messages can be sent while COPY OUT is in progress; and if the
 * copy is aborted by an elog(ERROR), we need to close out the copy so that
 * the frontend gets back into sync.  Therefore, these routines have to be
 * aware of COPY OUT state.
 *
 * NOTE: generally, it's a bad idea to emit outgoing messages directly with
 * pq_putbytes(), especially if the message would require multiple calls
 * to send.  Instead, use the routines in pqformat.c to construct the message
 * in a buffer and then emit it in one call to pq_putmessage.  This helps
 * ensure that the channel will not be clogged by an incomplete message
 * if execution is aborted by elog(ERROR) partway through the message.
 * The only non-libpq code that should call pq_putbytes directly is COPY OUT.
 *
 * At one time, libpq was shared between frontend and backend, but now
 * the backend's "backend/libpq" is quite separate from "interfaces/libpq".
 * All that remains is similarities of names to trap the unwary...
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *	$Id: pqcomm.c,v 1.1.1.1 2006/08/12 00:20:39 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

/*------------------------
 * INTERFACE ROUTINES
 *
 * setup/teardown:
 *		StreamServerPort	- Open postmaster's server port
 *		StreamConnection	- Create new connection with client
 *		StreamClose			- Close a client/backend connection
 *		pq_getport		- return the PGPORT setting
 *		pq_init			- initialize libpq at backend startup
 *		pq_close		- shutdown libpq at backend exit
 *
 * low-level I/O:
 *		pq_getbytes		- get a known number of bytes from connection
 *		pq_getstring	- get a null terminated string from connection
 *		pq_peekbyte		- peek at next byte from connection
 *		pq_putbytes		- send bytes to connection (not flushed until pq_flush)
 *		pq_flush		- flush pending output
 *
 * message-level I/O (and COPY OUT cruft):
 *		pq_putmessage	- send a normal message (suppressed in COPY OUT mode)
 *		pq_startcopyout - inform libpq that a COPY OUT transfer is beginning
 *		pq_endcopyout	- end a COPY OUT transfer
 *
 *------------------------
 */
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/file.h>

#include "postgres.h"
#include "env/env.h"

#include "libpq/libpq.h"
#include "utils/trace.h"		/* needed for HAVE_FCNTL_SETLK */
#include "miscadmin.h"
#include "tcop/dest.h"

#define PQ_BUFFER_SIZE 8192

typedef struct commcursor {
        int			ptr;
        int                     end;
        int                     size;
        void*                   args;
        int                     (*datamove)(void*, int, void*, int);    
        char                    buffer[PQ_BUFFER_SIZE];
} CommCursor;

#ifndef SOMAXCONN
#define SOMAXCONN 5				/* from Linux listen(2) man page */
#endif	 /* SOMAXCONN */

extern FILE *debug_port;		/* in util.c */

/*
 * Message status
 */

/* --------------------------------
 *		pq_init - initialize libpq at backend startup
 * --------------------------------
 */
void
pq_init(void)
{

}

/* --------------------------------
 *		pq_getport - return the PGPORT setting
 * --------------------------------
 */
int
pq_getport(void)
{
	char	   *envport = getenv("PGPORT");

	if (envport)
		return atoi(envport);
	return atoi(DEF_PGPORT);
}

/* --------------------------------
 *		pq_close - shutdown libpq at backend exit
 *
 * Note: in a standalone backend MyProcPort will be null,
 * don't crash during exit...
 * --------------------------------
 */
void
pq_close(void)
{
    printf("trying to close port\n");

}

/*
 * StreamServerPort -- open a sock stream "listening" port.
 *
 * This initializes the Postmaster's connection-accepting port.
 *
 * RETURNS: STATUS_OK or STATUS_ERROR
 */

int
StreamServerPort(char *hostName, unsigned short portName, int *fdP)
{
printf("trying to do stream server port\n");
	return STATUS_OK;
}

/*
 * StreamConnection -- create a new connection with client using
 *		server port.
 *
 * ASSUME: that this doesn't need to be non-blocking because
 *		the Postmaster uses select() to tell when the server master
 *		socket is ready for accept().
 *
 * NB: this can NOT call elog() because it is invoked in the postmaster,
 * not in standard backend context.  If we get an error, the best we can do
 * is log it to stderr.
 *
 * RETURNS: STATUS_OK or STATUS_ERROR
 */
int
StreamConnection(int server_fd, Port *port)
{
    printf("trying to stream connection\n");
	return STATUS_OK;
}

/*
 * StreamClose -- close a client/backend connection
 */
void
StreamClose(int sock)
{
    printf("trying to stream close\n");
}


/* --------------------------------
 * Low-level I/O routines begin here.
 *
 * These routines communicate with a frontend client across a connection
 * already established by the preceding routines.
 * --------------------------------
 */


/* --------------------------------
 *		pq_recvbuf - load some bytes into the input buffer
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
static int
pq_recvbuf(void)
{
    Env* env = GetEnv();
    CommCursor* cursor = (CommCursor*)env->pipein;
    
    if ( cursor == NULL ) return -1;
    
    if (cursor->ptr > 0)
    {
        if (cursor->end > cursor->ptr)
        {
            /* still some unread data, left-justify it in the buffer */
            memmove(cursor->buffer, cursor->buffer + cursor->ptr, cursor->end - cursor->ptr);
            cursor->end -= cursor->ptr;
            cursor->ptr = 0;
        } else {
            cursor->end= cursor->ptr = 0;
        }
    }

    /* Can fill buffer from PqRecvLength and upwards */
    for (;;)
    {
        int			r;

        r = cursor->datamove(cursor->args, 0, cursor->buffer + cursor->end,cursor->size - cursor->end);


        if (r < 0)
        {
                if (errno == EINTR)
                        continue;		/* Ok if interrupted */

                /*
                 * We would like to use elog() here, but dare not because elog
                 * tries to write to the client, which will cause problems if
                 * we have a hard communications failure ... So just write the
                 * message to the postmaster log.
                 */
                fprintf(stderr, "pq_recvbuf: recv() failed: %s\n",
                                strerror(errno));
                return EOF;
        }
        if (r == 0)
        {
                /* as above, elog not safe */
                fprintf(stderr, "pq_recvbuf: unexpected EOF on client connection\n");
                return EOF;
        }
        /* r contains number of bytes read, so just incr length */
        cursor->end += r;
        return 0;
    }
}

/* --------------------------------
 *		pq_getbyte	- get a single byte from connection, or return EOF
 * --------------------------------
 */
static int
pq_getbyte(void)
{
   Env* env = GetEnv();
   CommCursor* cursor = (CommCursor*)env->pipein;
   if ( cursor == NULL ) return -1;
   
	while (cursor->ptr >= cursor->end)
	{
		if (pq_recvbuf())		/* If nothing in buffer, then recv some */
			return EOF;			/* Failed to recv data */
	}
	return cursor->buffer[cursor->ptr++];
}

/* --------------------------------
 *		pq_peekbyte		- peek at next byte from connection
 *
 *	 Same as pq_getbyte() except we don't advance the pointer.
 * --------------------------------
 */
int
pq_peekbyte(void)
{
    Env* env = GetEnv();
    CommCursor* cursor = (CommCursor*)env->pipein;
    if ( cursor == NULL ) return -1;
    while (cursor->ptr >= cursor->end)
    {
            if (pq_recvbuf())		/* If nothing in buffer, then recv some */
                    return EOF;			/* Failed to recv data */
    }
    return cursor->buffer[cursor->ptr];
}


/* --------------------------------
 *		pq_getbytes		- get a known number of bytes from connection
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_getbytes(char *s, size_t len)
{
	size_t		amount;
        Env*		env = GetEnv();
        CommCursor*	cursor = (CommCursor*)env->pipein;
        
        if ( cursor == NULL ) return -1;
	while (len > 0)
	{
		while (cursor->ptr >= cursor->end)
		{
			if (pq_recvbuf())	/* If nothing in buffer, then recv some */
				return EOF;		/* Failed to recv data */
		}
		amount = cursor->ptr - cursor->end;
		if (amount > len)
			amount = len;
		memcpy(s, cursor->buffer + cursor->ptr, amount);
                cursor->ptr += amount;
		s += amount;
		len -= amount;
	}
	return 0;
}

/* --------------------------------
 *		pq_getstring	- get a null terminated string from connection
 *
 *		The return value is placed in an expansible StringInfo.
 *		Note that space allocation comes from the current memory context!
 *
 *		NOTE: this routine does not do any MULTIBYTE conversion,
 *		even though it is presumably useful only for text, because
 *		no code in this module should depend on MULTIBYTE mode.
 *		See pq_getstr in pqformat.c for that.
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_getstring(StringInfo s)
{
	int			c;

	/* Reset string to empty */
	s->len = 0;
	s->data[0] = '\0';

	/* Read until we get the terminating '\0' */
	while ((c = pq_getbyte()) != EOF && c != '\0')
		appendStringInfoChar(s, c);

	if (c == EOF)
		return EOF;

	return 0;
}


/* --------------------------------
 *		pq_putbytes		- send bytes to connection (not flushed until pq_flush)
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_putbytes(const char *s, size_t len)
{
	size_t		amount;
        Env*		env = GetEnv();
        CommCursor*	cursor = (CommCursor*)env->pipeout;
        
        if ( cursor == NULL ) return -1;
	while (len > 0)
	{
		if (cursor->end >= cursor->size)
			if (pq_flush())		/* If buffer is full, then flush it out */
				return EOF;
		amount = cursor->size - cursor->end;
		if (amount > len)
			amount = len;
		memcpy(cursor->buffer + cursor->end, s, amount);
		cursor->end += amount;
		s += amount;
		len -= amount;
	}
	return 0;
}

/* --------------------------------
 *		pq_flush		- flush pending output
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_flush(void)
{
        Env* env = GetEnv();
	CommCursor* cursor = (CommCursor*)env->pipeout;
	if ( cursor == NULL ) return -1;
        cursor->datamove(cursor->args, 0, cursor->buffer + cursor->ptr,cursor->end - cursor->ptr);
        cursor->ptr = 0;
        cursor->end = 0;
	return 0;
}


/* --------------------------------
 * Message-level I/O routines begin here.
 *
 * These routines understand about COPY OUT protocol.
 * --------------------------------
 */


/* --------------------------------
 *		pq_putmessage	- send a normal message (suppressed in COPY OUT mode)
 *
 *		If msgtype is not '\0', it is a message type code to place before
 *		the message body (len counts only the body size!).
 *		If msgtype is '\0', then the buffer already includes the type code.
 *
 *		All normal messages are suppressed while COPY OUT is in progress.
 *		(In practice only NOTICE messages might get emitted then; dropping
 *		them is annoying, but at least they will still appear in the
 *		postmaster log.)
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_putmessage(char msgtype, const char *s, size_t len)
{
	if (msgtype)
		if (pq_putbytes(&msgtype, 1))
			return EOF;
	return pq_putbytes(s, len);
}

/* --------------------------------
 *		pq_startcopyout - inform libpq that a COPY OUT transfer is beginning
 * --------------------------------
 */
void
pq_startcopyout(void)
{
	SetCopyout(true);
}

/* --------------------------------
 *		pq_endcopyout	- end a COPY OUT transfer
 *
 *		If errorAbort is indicated, we are aborting a COPY OUT due to an error,
 *		and must send a terminator line.  Since a partial data line might have
 *		been emitted, send a couple of newlines first (the first one could
 *		get absorbed by a backslash...)
 * --------------------------------
 */
void
pq_endcopyout(bool errorAbort)
{
	if (!DoingCopyout() )
		return;
	if (errorAbort)
		pq_putbytes("\n\n\\.\n", 5);
	/* in non-error case, copy.c will have emitted the terminator line */
	SetCopyout(false);
}

extern void ConnectIO(void* args, int (*infunc)(void*, int, void*, int),int (*outfunc)(void*, int, void*, int)) {
    Env*     env = GetEnv();
    
    if ( env->pipein != NULL || env->pipeout != NULL ) {
        DisconnectIO();
    }
    
    CommCursor*  in = palloc(sizeof(CommCursor));
    in->ptr = 0;
    in->end = 0;
    in->size =PQ_BUFFER_SIZE;
    in->args =args;
    in->datamove = infunc;
    env->pipein = in;

    CommCursor*  out = palloc(sizeof(CommCursor));
    out->ptr = 0;
    out->end = 0;
    out->size =PQ_BUFFER_SIZE;
    out->args =args;
    out->datamove = outfunc;
    env->pipeout = out;
}

extern void* DisconnectIO() {
    Env* env = GetEnv();
    void*       args = NULL;
    if ( env->pipein != NULL ) {
        CommCursor* comm = (CommCursor*)env->pipein;
        if (comm->ptr < comm->end) {
            if ( comm->datamove(comm->args, 0, comm->buffer + comm->ptr,comm->end - comm->ptr) == COMM_ERROR ) {
                elog(ERROR,"piping error occurred");
            }
        }
        args = comm->args;
    } 
    
    if ( env->pipeout != NULL ) {
        CommCursor* comm = (CommCursor*)env->pipeout;
        if (comm->ptr < comm->end) {
            if ( comm->datamove(comm->args, 0, comm->buffer + comm->ptr, comm->end - comm->ptr) == COMM_ERROR ) {
                elog(ERROR,"piping error occurred");
            }
        }
    }
    
    env->pipein = NULL;
    env->pipeout = NULL;
    return args;
}

