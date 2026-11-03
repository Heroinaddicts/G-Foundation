using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Unity.Collections;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace GameSprout.UnitySceneBridge
{
    internal static class GameSproutMeshExport
    {
        const long MaximumBytes = 512L * 1024 * 1024;
        const int MaximumNameBytes = 1024;
        const int MaximumSections = 1024;
        static readonly UTF8Encoding StrictUTF8 = new UTF8Encoding(false, true);
        public static BridgeReference Export(Mesh mesh, ExportContext context, Object identitySource = null)
        {
            if (!mesh) throw new InvalidDataException("Renderer or collider has no Mesh");
            var identity = identitySource ? identitySource : mesh;
            BridgeReference existing;
            if (context.TryGetSource(identity, out existing)) return existing;
            string meshName = mesh.name;
            var reference = context.GetUnityIdentity(identity);
            var path = context.AllocateAssetPath(identity, "Meshes", ".mesh");
            byte[] payload;
            // Editor MeshUtility deliberately bypasses Mesh.isReadable without changing import settings.
            using (var meshes = MeshUtility.AcquireReadOnlyMeshData(mesh))
            {
                var data = meshes[0]; int count = data.vertexCount;
                if (count < 3) throw new InvalidDataException("Native mesh requires at least three vertices: " + meshName);
                if (data.subMeshCount < 1 || data.subMeshCount > MaximumSections)
                    throw new InvalidDataException("Native mesh requires 1..1024 submeshes: " + meshName);
                var name = EncodedName(meshName);
                bool hasNormals = data.HasVertexAttribute(VertexAttribute.Normal);
                bool hasTangents = data.HasVertexAttribute(VertexAttribute.Tangent);
                bool hasUV = data.HasVertexAttribute(VertexAttribute.TexCoord0);
                bool hasColor = data.HasVertexAttribute(VertexAttribute.Color);
                if (hasColor)
                {
                    context.Count("MeshSourcesWithIgnoredVertexColors");
                    context.AddWarning("URPLitIgnoresVertexColor", "Source vertex colors intentionally omitted: Unity URP Lit does not consume COLOR.", identity);
                }
                if (data.HasVertexAttribute(VertexAttribute.TexCoord1))
                    context.AddWarning("LightmapUVSnapshotNotExported", "UV2 exists; the current Runtime has no lightmap binding, so only UV0 is exported.", identity);
                uint flags = (hasNormals ? 1u : 0u) | (hasTangents ? 2u : 0u) | (hasUV ? 4u : 0u);
                long sourceFloats = 3L + (hasNormals ? 3 : 0) + (hasTangents ? 4 : 0) + (hasUV ? 2 : 0);
                long preparedStride = 24L + (hasTangents ? 16 : 0) + (hasUV ? 8 : 0);
                long indexCount = 0;
                for (int sub = 0; sub < data.subMeshCount; ++sub)
                {
                    var section = data.GetSubMesh(sub);
                    int step = section.topology == MeshTopology.Quads ? 4 : 3;
                    if (section.topology != MeshTopology.Triangles && section.topology != MeshTopology.Quads)
                        throw new InvalidDataException("Unsupported Mesh topology " + section.topology + ": " + meshName);
                    if (section.indexCount < 0 || section.indexCount % step != 0)
                        throw new InvalidDataException("Incomplete primitive indices: " + meshName);
                    indexCount = checked(indexCount + (long)section.indexCount / step * (step == 4 ? 6 : 3));
                }
                if (indexCount < 3 || indexCount > UInt32.MaxValue)
                    throw new InvalidDataException("Native mesh requires a nonempty uint32 triangle index array: " + meshName);
                long vertexStart = (32L + name.Length + 3) & ~3L;
                long sourceBytes = checked(vertexStart + (long)count * sourceFloats * 4 + indexCount * 4 + (long)data.subMeshCount * 12);
                if (sourceBytes > MaximumBytes || (long)count * preparedStride + 12 > MaximumBytes)
                    throw new InvalidDataException("Native mesh source or minimum prepared buffer exceeds 512 MiB: " + meshName);
                if (!data.HasVertexAttribute(VertexAttribute.Position))
                    throw new InvalidDataException("Native mesh requires a Position attribute: " + meshName);
                var positions = default(NativeArray<Vector3>);
                var normals = default(NativeArray<Vector3>);
                var tangents = default(NativeArray<Vector4>);
                var uv = default(NativeArray<Vector2>);
                try
                {
                    positions = new NativeArray<Vector3>(count, Allocator.Temp, NativeArrayOptions.UninitializedMemory);
                    if (hasNormals) normals = new NativeArray<Vector3>(count, Allocator.Temp, NativeArrayOptions.UninitializedMemory);
                    if (hasTangents) tangents = new NativeArray<Vector4>(count, Allocator.Temp, NativeArrayOptions.UninitializedMemory);
                    if (hasUV) uv = new NativeArray<Vector2>(count, Allocator.Temp, NativeArrayOptions.UninitializedMemory);
                    data.GetVertices(positions); if (hasNormals) data.GetNormals(normals);
                    if (hasTangents) data.GetTangents(tangents); if (hasUV) data.GetUVs(0, uv);
                    // NativeMeshImporter accepts finite authored normals (including zero),
                    // or generates normals when the normal attribute is absent. Keep the same rule.
                    for (int vertex = 0; vertex < count; ++vertex)
                    {
                        RequireFinite(positions[vertex], "Position", vertex, meshName);
                        if (hasNormals) RequireFinite(normals[vertex], "Normal", vertex, meshName);
                        if (hasTangents)
                        {
                            var t = tangents[vertex];
                            RequireFinite(new Vector3(t.x, t.y, t.z), "Tangent.xyz", vertex, meshName);
                            RequireFinite(t.w, "Tangent.w", vertex, meshName);
                        }
                        if (hasUV)
                        {
                            RequireFinite(uv[vertex].x, "UV0.x", vertex, meshName);
                            RequireFinite(uv[vertex].y, "UV0.y", vertex, meshName);
                        }
                    }
                    var indices = new List<uint>(checked((int)indexCount)); var sections = new List<uint[]>();
                    long nondegenerateTriangles = 0;
                    for (int sub = 0; sub < data.subMeshCount; ++sub)
                    {
                        var descriptor = data.GetSubMesh(sub);
                        if (descriptor.topology != MeshTopology.Triangles && descriptor.topology != MeshTopology.Quads)
                            throw new InvalidDataException("Unsupported Mesh topology " + descriptor.topology + ": " + meshName);
                        uint first = (uint)indices.Count;
                        using (var source = new NativeArray<int>(descriptor.indexCount, Allocator.Temp, NativeArrayOptions.UninitializedMemory))
                        {
                            data.GetIndices(source, sub, true);
                            int step = descriptor.topology == MeshTopology.Quads ? 4 : 3;
                            if (source.Length % step != 0) throw new InvalidDataException("Incomplete primitive indices: " + meshName);
                            for (int i = 0; i < source.Length; i += step)
                            {
                                if (AddTriangle(indices, source[i], source[i + 1], source[i + 2], positions)) ++nondegenerateTriangles;
                                if (step == 4 && AddTriangle(indices, source[i], source[i + 2], source[i + 3], positions)) ++nondegenerateTriangles;
                            }
                        }
                        sections.Add(new uint[] { first, (uint)indices.Count - first, (uint)sub });
                    }
                    if (indices.Count != indexCount || nondegenerateTriangles == 0)
                        throw new InvalidDataException("Native mesh contains no nondegenerate triangles or inconsistent indices: " + meshName);
                    long preparedBytes = checked((long)count * preparedStride + nondegenerateTriangles * 12);
                    if (preparedBytes > MaximumBytes)
                        throw new InvalidDataException("Native mesh prepared vertex/index buffer exceeds 512 MiB: " + meshName);
                    using (var stream = new MemoryStream(checked((int)sourceBytes)))
                    using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))
                    {
                        writer.Write(Encoding.ASCII.GetBytes("GSMESH02"));
                        writer.Write(2u); writer.Write(flags); writer.Write((uint)count); writer.Write((uint)indices.Count);
                        writer.Write((uint)sections.Count); writer.Write((uint)name.Length); writer.Write(name);
                        while ((stream.Position & 3) != 0) writer.Write((byte)0);
                        for (int i = 0; i < count; ++i) Write(writer, positions[i]);
                        if (hasNormals) for (int i = 0; i < count; ++i) Write(writer, normals[i]);
                        if (hasTangents) for (int i = 0; i < count; ++i)
                        {
                            var t = tangents[i]; writer.Write(t.x); writer.Write(t.y); writer.Write(t.z); writer.Write(t.w);
                        }
                        if (hasUV) for (int i = 0; i < count; ++i) { writer.Write(uv[i].x); writer.Write(uv[i].y); }
                        foreach (uint value in indices) writer.Write(value);
                        foreach (var section in sections) foreach (uint value in section) writer.Write(value);
                        writer.Flush();
                        if (stream.Length != sourceBytes) throw new InvalidDataException("Native mesh serialized byte count does not match its current-format layout: " + meshName);
                        payload = stream.ToArray();
                    }
                    context.Count("MeshVertices", count); context.Count("MeshTriangles", indices.Count / 3);
                }
                finally
                {
                    if (positions.IsCreated) positions.Dispose(); if (normals.IsCreated) normals.Dispose(); if (tangents.IsCreated) tangents.Dispose(); if (uv.IsCreated) uv.Dispose();
                }
            }
            context.WriteBytesFile(path, payload);
            context.WriteMeta(path, reference, "Mesh", 3, new Dictionary<string, object>(), "main", meshName);
            context.RegisterSource(identity, reference, path); context.Count("MeshSources"); return reference;
        }
        static bool AddTriangle(List<uint> output, int a, int b, int c, NativeArray<Vector3> positions)
        {
            int count = positions.Length;
            if (a < 0 || b < 0 || c < 0 || a >= count || b >= count || c >= count) throw new InvalidDataException("Mesh index outside vertex range");
            output.Add((uint)a); output.Add((uint)b); output.Add((uint)c);
            if (a == b || b == c || a == c) return false;
            // Match the importer's exact double-precision zero-area test, without an epsilon
            // that would incorrectly reject very small but valid source triangles.
            var p = positions[a]; var q = positions[b]; var r = positions[c];
            double ax = (double)q.x - p.x, ay = (double)q.y - p.y, az = (double)q.z - p.z;
            double bx = (double)r.x - p.x, by = (double)r.y - p.y, bz = (double)r.z - p.z;
            return ay * bz - az * by != 0 || az * bx - ax * bz != 0 || ax * by - ay * bx != 0;
        }
        static byte[] EncodedName(string name)
        {
            if (String.IsNullOrWhiteSpace(name) || name.IndexOf('\0') >= 0)
                throw new InvalidDataException("Native mesh name must be nonempty UTF8 without NUL");
            byte[] bytes;
            try { bytes = StrictUTF8.GetBytes(name); }
            catch (EncoderFallbackException error) { throw new InvalidDataException("Native mesh name contains invalid Unicode", error); }
            if (bytes.Length < 1 || bytes.Length > MaximumNameBytes)
                throw new InvalidDataException("Native mesh UTF8 name must contain 1..1024 bytes");
            return bytes;
        }
        static void RequireFinite(Vector3 value, string attribute, int vertex, string name)
        {
            if (Single.IsNaN(value.x) || Single.IsInfinity(value.x) || Single.IsNaN(value.y) || Single.IsInfinity(value.y)
                || Single.IsNaN(value.z) || Single.IsInfinity(value.z))
                throw new InvalidDataException("Native mesh has a nonfinite " + attribute + " at vertex " + vertex + ": " + name);
        }
        static void RequireFinite(float value, string attribute, int vertex, string name)
        {
            if (Single.IsNaN(value) || Single.IsInfinity(value))
                throw new InvalidDataException("Native mesh has a nonfinite " + attribute + " at vertex " + vertex + ": " + name);
        }
        static void Write(BinaryWriter writer, Vector3 value) { writer.Write(value.x); writer.Write(value.y); writer.Write(value.z); }
    }
}
