/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */


package org.weaverdb;

public interface ConnectionFactory {

    Connection connect(String database);
    
    Connection connectUser(String username, String password, String database);
    
    Runtime.Version builtFor();
}
