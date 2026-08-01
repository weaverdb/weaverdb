/*-------------------------------------------------------------------------
 *
 * Runs pgvector JNI regression tests on the Java 21 connection factory.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import org.junit.platform.suite.api.SelectClasses;
import org.junit.platform.suite.api.Suite;

@Suite
@SelectClasses({
        PgvectorSmokeTest.class,
        PgvectorOrderByTest.class,
        PgvectorHalfvecOrderByTest.class,
        PgvectorSparsevecOrderByTest.class,
        PgvectorBitOrderByTest.class,
        PgvectorIndexTest.class,
        PgvectorDistanceOpsTest.class,
        PgvectorMutationsTest.class,
        PgvectorIndirectBlobTest.class,
        PgvectorBlobIndexTest.class,
        PgvectorConnect21Test.class,
        PgvectorFeaturesShellTest.class,
        PgvectorOrderByShellTest.class
})
public class PgvectorJniSuite {
}
