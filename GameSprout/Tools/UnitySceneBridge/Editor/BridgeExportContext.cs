// Editor-only conversion boundary. Generated projects contain no Unity dependency.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEditor;
using UnityEngine;
using Object = UnityEngine.Object;

namespace GameSprout.UnitySceneBridge
{
    internal struct BridgeReference
    {
        public string Guid;
        public ulong LocalID;
        public BridgeReference(string guid, ulong localID) { Guid = guid; LocalID = localID; }
        public Dictionary<string, object> ToJson() { return BridgeJson.Object("Guid", Guid, "LocalID", LocalID); }
    }

    internal sealed class BridgeReport
    {
        public readonly Dictionary<string, long> Counts = new Dictionary<string, long>();
        public readonly List<object> Assets = new List<object>();
        public readonly List<object> Objects = new List<object>();
        public readonly List<object> MaterialConversions = new List<object>();
        public readonly List<object> TextureConversions = new List<object>();
        public readonly Dictionary<string, Dictionary<string, object>> Warnings = new Dictionary<string, Dictionary<string, object>>();
    }

    internal sealed class ExportContext
    {
        public readonly string OutputRoot;
        public readonly string AssetsRoot;
        public readonly BridgeReport Report = new BridgeReport();
        readonly Dictionary<Object, BridgeReference> sources = new Dictionary<Object, BridgeReference>();
        readonly Dictionary<string, string> allocatedPaths = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        readonly HashSet<string> registeredIdentity = new HashSet<string>();

        public ExportContext(string outputRoot)
        {
            OutputRoot = Path.GetFullPath(outputRoot);
            AssetsRoot = Path.Combine(OutputRoot, "Assets");
            Directory.CreateDirectory(AssetsRoot);
        }
        public static string Hash(string text)
        {
            using (var md5 = MD5.Create())
            {
                var bytes = md5.ComputeHash(Encoding.UTF8.GetBytes(text));
                var result = new StringBuilder(32);
                foreach (byte b in bytes) result.Append(b.ToString("x2", CultureInfo.InvariantCulture));
                return result.ToString();
            }
        }
        public static string SourcePath(Object source)
        {
            return source ? AssetDatabase.GetAssetPath(source) : "";
        }
        public static string UnityIdentity(Object source)
        {
            if (!source) return "null";
            string guid; long localID;
            if (AssetDatabase.TryGetGUIDAndLocalFileIdentifier(source, out guid, out localID) && !String.IsNullOrEmpty(guid))
                return guid + ":" + localID.ToString(CultureInfo.InvariantCulture) + ":" + source.GetType().FullName;
            return GlobalObjectId.GetGlobalObjectIdSlow(source).ToString() + ":" + source.GetType().FullName + ":" + source.name;
        }
        public BridgeReference GetUnityIdentity(Object source)
        {
            if (!source) throw new ArgumentNullException("source");
            return new BridgeReference(Hash("GameSprout.UnitySceneBridge.1:" + UnityIdentity(source)), 1);
        }
        public bool TryGetSource(Object source, out BridgeReference reference)
        {
            reference = default(BridgeReference);
            return source && sources.TryGetValue(source, out reference);
        }
        public static string SafeName(string value)
        {
            var result = new StringBuilder();
            foreach (char c in value ?? "Asset")
                result.Append(Char.IsLetterOrDigit(c) || c == '_' || c == '-' ? c : '_');
            var name = result.ToString().Trim('_');
            if (String.IsNullOrEmpty(name)) name = "Asset";
            return name.Length > 80 ? name.Substring(0, 80) : name;
        }
        public string AllocateAssetPath(Object source, string category, string extension)
        {
            if (!source) throw new ArgumentNullException("source");
            if (String.IsNullOrEmpty(extension) || extension[0] != '.' || extension.IndexOfAny(new[] {'/', '\\'}) >= 0)
                throw new ArgumentException("Invalid output extension");
            var identity = GetUnityIdentity(source);
            var path = SafeName(category) + "/" + SafeName(source.name) + "_" + identity.Guid.Substring(0, 16) + extension;
            string previous;
            if (allocatedPaths.TryGetValue(path, out previous) && previous != identity.Guid)
                throw new InvalidOperationException("Output path collision: " + path);
            allocatedPaths[path] = identity.Guid;
            return path;
        }
        public void RegisterSource(Object source, BridgeReference reference, string relativeAssetsPath)
        {
            if (!source) throw new ArgumentNullException("source");
            sources[source] = reference;
            var identity = UnityIdentity(source);
            if (!registeredIdentity.Add(identity)) return;
            string unityGuid; long unityLocalID;
            AssetDatabase.TryGetGUIDAndLocalFileIdentifier(source, out unityGuid, out unityLocalID);
            Report.Assets.Add(BridgeJson.Object("UnityIdentity", identity, "UnityGuid", unityGuid ?? "",
                "UnityLocalID", unityLocalID, "UnityPath", SourcePath(source), "UnityType", source.GetType().FullName,
                "Path", relativeAssetsPath, "Reference", reference.ToJson()));
        }
        public void Count(string name, int amount = 1)
        {
            long previous; Report.Counts.TryGetValue(name, out previous); Report.Counts[name] = previous + amount;
        }
        public void AddWarning(string code, string message, Object source = null)
        {
            Dictionary<string, object> warning;
            if (!Report.Warnings.TryGetValue(code, out warning))
            {
                warning = BridgeJson.Object("Code", code, "Message", message, "Count", 0L, "Examples", new List<object>());
                Report.Warnings.Add(code, warning);
            }
            warning["Count"] = (long)warning["Count"] + 1;
            var examples = (List<object>)warning["Examples"];
            if (source && examples.Count < 8)
                examples.Add(BridgeJson.Object("Name", source.name, "Path", SourcePath(source), "Identity", UnityIdentity(source)));
        }
        public void RecordMaterial(Material source, Dictionary<string, object> details)
        {
            details["UnityIdentity"] = UnityIdentity(source); details["UnityPath"] = SourcePath(source);
            Report.MaterialConversions.Add(details);
        }
        public void AddTextureDetails(Texture source, Dictionary<string, object> details)
        {
            details["UnityIdentity"] = UnityIdentity(source); details["UnityPath"] = SourcePath(source);
            Report.TextureConversions.Add(details);
        }
        string Resolve(string relative, bool asset)
        {
            if (String.IsNullOrEmpty(relative) || Path.IsPathRooted(relative)) throw new ArgumentException("Output path must be relative");
            var root = asset ? AssetsRoot : OutputRoot;
            var path = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (!path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal)) throw new ArgumentException("Output path escapes project");
            return path;
        }
        public void WriteBytesFile(string relativeAssetsPath, byte[] bytes) { WriteNew(Resolve(relativeAssetsPath, true), bytes); }
        public void WriteJsonFile(string relativeAssetsPath, Dictionary<string, object> document)
        { WriteBytesFile(relativeAssetsPath, Encoding.UTF8.GetBytes(BridgeJson.Serialize(document))); }
        public void WriteProjectJson(string relativePath, Dictionary<string, object> document)
        { WriteNew(Resolve(relativePath, false), Encoding.UTF8.GetBytes(BridgeJson.Serialize(document))); }
        static void WriteNew(string absolutePath, byte[] bytes)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(absolutePath));
            using (var file = new FileStream(absolutePath, FileMode.CreateNew, FileAccess.Write, FileShare.None)) file.Write(bytes, 0, bytes.Length);
        }
        public void WriteMeta(string relativeAssetsPath, BridgeReference reference, string importerType, ushort assetType,
            Dictionary<string, object> settings = null, string sourceKey = "main", string name = null)
        {
            WriteJsonFile(relativeAssetsPath + ".meta", BridgeJson.Object("Version", 1, "Guid", reference.Guid,
                "ImporterType", importerType, "Path", relativeAssetsPath, "ImportSettings", settings ?? new Dictionary<string, object>(),
                "SubAssets", new object[] { BridgeJson.Object("LocalID", reference.LocalID, "Type", assetType,
                    "SourceKey", sourceKey, "Name", name ?? Path.GetFileNameWithoutExtension(relativeAssetsPath)) }));
        }
        public static object[] Vector(Vector3 v) { return new object[] { v.x, v.y, v.z }; }
        public static object[] Rotation(Quaternion q) { return new object[] { q.x, q.y, q.z, q.w }; }
        public static object[] Scale(Vector3 v) { return new object[] { v.x, v.y, v.z }; }
        public static object[] Color(Color c) { return new object[] { c.r, c.g, c.b, c.a }; }
    }

    // A narrow, deterministic JSON writer keeps this Editor tool independent of third-party packages.
    internal static class BridgeJson
    {
        public static Dictionary<string, object> Object(params object[] pairs)
        {
            if ((pairs.Length & 1) != 0) throw new ArgumentException("Object requires key/value pairs");
            var result = new Dictionary<string, object>();
            for (int i = 0; i < pairs.Length; i += 2) result.Add((string)pairs[i], pairs[i + 1]);
            return result;
        }
        public static string Serialize(object value)
        {
            var text = new StringBuilder(); Append(text, value, 0); text.Append('\n'); return text.ToString();
        }
        static void Indent(StringBuilder text, int level) { text.Append(' ', level * 2); }
        static void Quote(StringBuilder text, string value)
        {
            text.Append('"');
            foreach (char c in value)
            {
                switch (c)
                {
                    case '"': text.Append("\\\""); break; case '\\': text.Append("\\\\"); break;
                    case '\n': text.Append("\\n"); break; case '\r': text.Append("\\r"); break; case '\t': text.Append("\\t"); break;
                    default: if (c < 32) text.Append("\\u" + ((int)c).ToString("x4")); else text.Append(c); break;
                }
            }
            text.Append('"');
        }
        static void Append(StringBuilder text, object value, int level)
        {
            if (value == null) { text.Append("null"); return; }
            if (value is string) { Quote(text, (string)value); return; }
            if (value is bool) { text.Append((bool)value ? "true" : "false"); return; }
            var dictionary = value as IDictionary;
            if (dictionary != null)
            {
                text.Append('{'); bool first = true;
                foreach (DictionaryEntry entry in dictionary)
                {
                    if (!first) text.Append(','); first = false; text.Append('\n'); Indent(text, level + 1);
                    Quote(text, (string)entry.Key); text.Append(": "); Append(text, entry.Value, level + 1);
                }
                if (!first) { text.Append('\n'); Indent(text, level); } text.Append('}'); return;
            }
            var enumerable = value as IEnumerable;
            if (enumerable != null)
            {
                text.Append('['); bool first = true;
                foreach (object item in enumerable)
                {
                    if (!first) text.Append(','); first = false; text.Append('\n'); Indent(text, level + 1); Append(text, item, level + 1);
                }
                if (!first) { text.Append('\n'); Indent(text, level); } text.Append(']'); return;
            }
            if (value is float || value is double)
            {
                double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                if (Double.IsNaN(number) || Double.IsInfinity(number)) throw new InvalidDataException("Cannot export a nonfinite number");
                text.Append(value is float ? ((float)value).ToString("R", CultureInfo.InvariantCulture) : number.ToString("R", CultureInfo.InvariantCulture)); return;
            }
            if (value is IFormattable) { text.Append(((IFormattable)value).ToString(null, CultureInfo.InvariantCulture)); return; }
            throw new InvalidDataException("Unsupported JSON value: " + value.GetType().FullName);
        }
    }
}
