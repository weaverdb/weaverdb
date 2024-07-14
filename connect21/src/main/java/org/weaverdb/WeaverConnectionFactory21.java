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


public class WeaverConnectionFactory21 implements DBReferenceFactory {

    @Override
    public DBReference connect(String db) {
        return BaseWeaverConnection.connectAnonymously(db, new StreamingTransformer21());
    }

    @Override
    public DBReference connectUser(String username, String password, String database) {
        return BaseWeaverConnection.connectUser(username, password, database, new StreamingTransformer21());
    }

    @Override
    public String builtFor() {
        return "21";
    }
    
}
