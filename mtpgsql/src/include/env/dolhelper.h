/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */



#ifndef _DOLHELPER_H_
#define _DOLHELPER_H_



typedef struct dol_connection_data * DolConnection;

PG_EXTERN void InitializeDol(void);

PG_EXTERN DolConnection GetDolConnection(void);

PG_EXTERN long DestroyDolConnection(DolConnection conn);

PG_EXTERN void ProcessDolCommand(DolConnection conn,void*(*start_routine)(void*),void* arg);

PG_EXTERN bool IsDolConnectionAvailable(void);

PG_EXTERN void ShutdownDolHelpers(void);

PG_EXTERN void CancelDolHelpers(void);

PG_EXTERN int CheckDolHelperErrors(void);

PG_EXTERN int GetDolHelperErrorMessage(char* state,char* msg);
#endif
