/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef PASSWORD_H
#define PASSWORD_H

int			verify_password(char *auth_arg, char *user, char *password);

#endif
