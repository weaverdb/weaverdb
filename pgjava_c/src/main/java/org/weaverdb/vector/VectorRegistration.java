package org.weaverdb.vector;

import org.weaverdb.DBReference;
import org.weaverdb.FunctionInstaller;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/**
 * Helper to easily register vector functions with a WeaverDB instance
 * so they can be called from SQL using the existing Java language support.
 *
 * Example:
 * <pre>
 * VectorRegistration.register(db);
 * // Now you can call from SQL:
 * // SELECT cosine_similarity(v1, v2) FROM vectors;
 * </pre>
 */
public final class VectorRegistration {

    private VectorRegistration() {}

    /**
     * Registers the standard vector functions (cosine, euclidean, etc.)
     * under friendly SQL names.
     */
    public static void register(DBReference db) throws Exception {
        FunctionInstaller installer = new FunctionInstaller(db);

        // Register common distance/similarity functions
        installer.installFunction("cosine_similarity", MethodHandles.lookup().findStatic(
                VectorFunctions.class,
                "cosineSimilarity",
                MethodType.methodType(double.class, Vector.class, Vector.class)
        ));

        installer.installFunction("cosine_distance", MethodHandles.lookup().findStatic(
                VectorFunctions.class,
                "cosineDistance",
                MethodType.methodType(double.class, Vector.class, Vector.class)
        ));

        installer.installFunction("euclidean_distance", MethodHandles.lookup().findStatic(
                VectorFunctions.class,
                "euclidean",
                MethodType.methodType(double.class, Vector.class, Vector.class)
        ));

        installer.installFunction("squared_euclidean", MethodHandles.lookup().findStatic(
                VectorFunctions.class,
                "squaredEuclidean",
                MethodType.methodType(double.class, Vector.class, Vector.class)
        ));

        installer.installFunction("inner_product", MethodHandles.lookup().findStatic(
                VectorFunctions.class,
                "innerProduct",
                MethodType.methodType(double.class, Vector.class, Vector.class)
        ));
    }

    /**
     * Registers functions using a specific distance metric as the default.
     */
    public static void registerWithDefault(DBReference db, InMemoryVectorStore.DistanceFunction distance) {
        // Future: could register a default "distance" function based on the chosen metric.
    }
}
