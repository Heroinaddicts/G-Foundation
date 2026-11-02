// Unity Editor-only authoring bridge. Runtime never loads Unity assemblies.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace GameSprout.UnitySceneBridge
{
    internal static class GameSproutMaterialExport
    {
        public static BridgeReference Export(Material source, ExportContext context)
        {
            if (source == null) return default(BridgeReference);
            BridgeReference previous;
            if (context.TryGetSource(source, out previous)) return previous;

            string shaderName = source.shader != null ? source.shader.name : "<missing>";
            if (shaderName != "Universal Render Pipeline/Lit")
                context.AddWarning("material.shader-approximation", "Shader '" + shaderName + "' is mapped to built-in GameSprout/Lit; custom shader behavior is not exported.", source);
            if (QualitySettings.activeColorSpace != ColorSpace.Linear)
                context.AddWarning("material.gamma-project", "Source project renders in Gamma space; exporting to GameSprout's linear lighting model is an explicit approximation.", source);

            var colorReport = new List<object>();
            Color baseColor = ReadLinearColor(source, "_BaseColor", Color.white, colorReport);
            bool emissionEnabled = source.IsKeywordEnabled("_EMISSION");
            Color emission = emissionEnabled ? ReadLinearColor(source, "_EmissionColor", Color.black, colorReport) : Color.black;
            if (!emissionEnabled && source.HasProperty("_EmissionColor"))
                colorReport.Add(ColorDetail(source, "_EmissionColor", source.GetColor("_EmissionColor"), Color.black, "disabled-by-_EMISSION-keyword"));

            Texture baseTexture = source.HasProperty("_BaseMap") ? source.GetTexture("_BaseMap") : null;
            if (!source.HasProperty("_BaseMap"))
                context.AddWarning("material.no-base-map", "Shader has no _BaseMap; source _MainTex is deliberately not substituted for URP's resolved base map.", source);
            bool normalEnabled = source.IsKeywordEnabled("_NORMALMAP");
            Texture normalTexture = normalEnabled && source.HasProperty("_BumpMap") ? source.GetTexture("_BumpMap") : null;
            var baseReference = ExportTexture(baseTexture, false, context);
            var normalReference = ExportTexture(normalTexture, true, context);
            Vector2 scale = source.HasProperty("_BaseMap") ? source.GetTextureScale("_BaseMap") : Vector2.one;
            Vector2 offset = source.HasProperty("_BaseMap") ? source.GetTextureOffset("_BaseMap") : Vector2.zero;
            // URP Lit samples its normal map using the BaseMap-transformed UV;
            // _BumpMap_ST is not the forward Lit sampling transform.
            var textureST = new object[] { Finite(scale.x), Finite(scale.y), Finite(offset.x), Finite(offset.y) };
            float smoothness = Mathf.Clamp01(ReadFloat(source, "_Smoothness", .5f));
            float metallic = Mathf.Clamp01(ReadFloat(source, "_Metallic", 0));
            float normalScale = ReadFloat(source, "_BumpScale", 1);
            if (normalScale < 0)
            {
                context.AddWarning("material.negative-normal-scale", "Negative normal strength is unsupported by current Lit; exporting zero strength.", source);
                normalScale = 0;
            }
            bool transparent = ReadFloat(source, "_Surface", 0) > .5f;
            bool alphaClip = ReadFloat(source, "_AlphaClip", 0) > .5f;
            var ignoredTextures = new List<object>();
            if (transparent)
                context.AddWarning("material.transparent-as-opaque", "Source transparent material (blend=" + ReadFloat(source, "_Blend", 0) + ", alpha=" + baseColor.a + ") is exported as opaque Lit. Alpha is retained as source data; current built-in Lit cannot reproduce its blending/depth state.", source);
            if (alphaClip)
                context.AddWarning("material.alpha-clip-unsupported", "Source alpha clipping is not supported by current built-in Lit; cutoff=" + ReadFloat(source, "_Cutoff", .5f) + ".", source);
            foreach (string property in new[] { "_MetallicGlossMap", "_SpecGlossMap", "_OcclusionMap", "_DetailAlbedoMap", "_DetailNormalMap", "_ParallaxMap" })
                if (source.HasProperty(property) && source.GetTexture(property) != null)
                {
                    ignoredTextures.Add(property);
                    context.AddWarning("material.unmapped-texture", "Current Lit does not expose " + property + "; its texture modulation is omitted, with scalar values retained.", source);
                }
            if (emissionEnabled && source.HasProperty("_EmissionMap") && source.GetTexture("_EmissionMap") != null)
            {
                ignoredTextures.Add("_EmissionMap");
                context.AddWarning("material.emission-map-unsupported", "Current Lit uses constant emission; _EmissionMap modulation is omitted.", source);
            }
            bool specularHighlights = ReadFloat(source, "_SpecularHighlights", 1) >= .5f;
            float sourceQueueOffset = ReadFloat(source, "_QueueOffset", 0);
            int sortingPriority = Mathf.Clamp(Mathf.RoundToInt(sourceQueueOffset), -50, 50);
            if (Math.Abs(sourceQueueOffset - sortingPriority) > .001f)
                context.AddWarning("material.sorting-priority-clamped", "Source queue offset is rounded and clamped to GameSprout's [-50,50] sorting priority.", source);
            if (ReadFloat(source, "_WorkflowMode", 1) < .5f)
                context.AddWarning("material.specular-workflow-approximation", "Source uses specular workflow; current Lit exports the metallic scalar approximation.", source);
            if (Math.Abs(ReadFloat(source, "_Cull", 2) - 1) < .001f)
                context.AddWarning("material.front-cull-approximation", "Source culls front faces; current built-in Lit exports back-face culling instead.", source);
            if (source.IsKeywordEnabled("_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A"))
                context.AddWarning("material.smoothness-texture-unsupported", "Source derives smoothness from base texture alpha; current Lit exports only scalar roughness.", source);

            var identity = context.GetUnityIdentity(source);
            string path = context.AllocateAssetPath(source, "Materials", ".mat");
            var document = Obj(
                "Version", 1, "Shader", "GameSprout/Lit",
                "BaseColor", ColorArray(baseColor),
                "BaseColorTexture", ReferenceJson(baseReference), "BaseColorTextureST", textureST,
                "Metallic", metallic, "Roughness", 1f - smoothness,
                "NormalTexture", ReferenceJson(normalReference), "NormalTextureST", textureST, "NormalScale", normalScale,
                "Emission", ColorArray(emission), "DoubleSided", ReadFloat(source, "_Cull", 2) < .5f,
                "SpecularHighlights", specularHighlights, "SortingPriority", sortingPriority,
                // Unity's legacy material checkbox does not control its GPU
                // Resident Drawer. Keep GameSprout's automatic grouping enabled
                // rather than translating a false legacy checkbox into a ban.
                "EnableGPUInstancing", true);
            context.WriteJsonFile(path, document);
            context.WriteMeta(path, identity, "Material", 5, Obj("MainObjectID", identity.LocalID), "main", source.name);
            context.RegisterSource(source, identity, path);
            context.Count("MaterialSources");
            context.RecordMaterial(source, Obj(
                "Shader", shaderName, "ExportedShader", "GameSprout/Lit", "ProjectColorSpace", QualitySettings.activeColorSpace.ToString(),
                "ExportedColorSpace", "Linear", "Colors", colorReport,
                "EmissionKeyword", emissionEnabled, "NormalKeyword", normalEnabled,
                "SourceSurface", transparent ? "Transparent" : "Opaque", "SourceBlend", ReadFloat(source, "_Blend", 0),
                "SourceAlphaClip", alphaClip, "SourceCull", ReadFloat(source, "_Cull", 2), "SourceRenderQueue", source.renderQueue,
                "SourceEnableGPUInstancing", source.enableInstancing, "ExportedEnableGPUInstancing", true,
                "InstancingPolicy", "RuntimeAutomaticGroupingNotUnityLegacyMaterialCheckbox",
                "SourceSpecularHighlights", specularHighlights, "SourceQueueOffset", sourceQueueOffset, "ExportedSortingPriority", sortingPriority,
                "OmittedTextureProperties", ignoredTextures,
                "SourceSmoothness", smoothness, "ExportedRoughness", 1f - smoothness,
                "BaseMapST", textureST, "NormalUsesBaseMapST", true));
            return identity;
        }

        static Color ReadLinearColor(Material material, string property, Color fallback, List<object> report)
        {
            Color raw = material.HasProperty(property) ? material.GetColor(property) : fallback;
            ShaderPropertyFlags flags = PropertyFlags(material, property);
            // Ordinary ShaderLab Color values are supplied as sRGB and decoded
            // for shader constants. HDR/Gamma SetColor values are already stored
            // in linear space by Unity in a Linear project. Do not decode an HDR
            // _EmissionColor a second time (for example Clouds' stored 0.3).
            bool storedLinear = QualitySettings.activeColorSpace == ColorSpace.Linear &&
                (flags & (ShaderPropertyFlags.HDR | ShaderPropertyFlags.Gamma)) != 0;
            Color value = storedLinear ? raw : raw.linear;
            value.a = raw.a;
            Finite(value.r); Finite(value.g); Finite(value.b); Finite(value.a);
            if (value.r < 0 || value.g < 0 || value.b < 0 || value.a < 0 || value.a > 1)
                throw new InvalidOperationException("Unsupported negative/HDR alpha material color: " + material.name + "." + property);
            report.Add(ColorDetail(material, property, raw, value, storedLinear ? "stored-linear-HDR-or-Gamma" : "sRGB-Color-to-linear"));
            return value;
        }

        static Dictionary<string, object> ColorDetail(Material source, string property, Color raw, Color exported, string policy)
        {
            return Obj("Property", property, "Flags", PropertyFlags(source, property).ToString(),
                "Raw", ColorArray(raw), "Exported", ColorArray(exported), "Policy", policy);
        }
        static ShaderPropertyFlags PropertyFlags(Material material, string property)
        {
            if (material.shader == null) return ShaderPropertyFlags.None;
            int index = material.shader.FindPropertyIndex(property);
            return index >= 0 ? material.shader.GetPropertyFlags(index) : ShaderPropertyFlags.None;
        }

        static BridgeReference ExportTexture(Texture source, bool normal, ExportContext context)
        {
            if (source == null) return default(BridgeReference);
            Texture2D texture = source as Texture2D;
            if (texture == null) throw new InvalidOperationException("Material texture is not a supported Texture2D: " + source.name);
            BridgeReference previous;
            if (context.TryGetSource(source, out previous)) return previous;
            string sourcePath = AssetDatabase.GetAssetPath(source);
            var importer = AssetImporter.GetAtPath(sourcePath) as TextureImporter;
            bool sourceNormal = importer != null && importer.textureType == TextureImporterType.NormalMap;
            bool srgb = !normal && !sourceNormal && (importer == null || importer.sRGBTexture);
            if (normal && !sourceNormal)
                context.AddWarning("texture.normal-importer-mismatch", "Normal slot references a texture not imported as a Unity NormalMap; exporting its decoded tangent normals as Linear.", source);
            string absoluteSource = String.IsNullOrEmpty(sourcePath) ? "" :
                Path.GetFullPath(Path.Combine(Application.dataPath, "..", sourcePath));
            bool sourcePNG = Path.GetExtension(sourcePath).Equals(".png", StringComparison.OrdinalIgnoreCase) && File.Exists(absoluteSource);
            bool copyPNG = sourcePNG && (!sourceNormal || !importer.convertToNormalmap);
            // Source PNG pixels contain RGB normals, not Unity's GPU AG/RG
            // packing. Reorder rows for GameSprout's top-first texture upload,
            // keeping mesh UV/ST and the normal tangent frame unchanged. Only
            // invert G when the source importer explicitly requests it.
            bool flipGreen = sourceNormal && importer.flipGreenChannel;
            byte[] bytes;
            string route;
            if (copyPNG)
            {
                var image = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
                try
                {
                    if (!ImageConversion.LoadImage(image, File.ReadAllBytes(absoluteSource), false))
                        throw new InvalidOperationException("Cannot decode PNG source " + sourcePath);
                    bytes = EncodeTopFirstPNG(image, flipGreen);
                }
                finally { Object.DestroyImmediate(image); }
                route = flipGreen ? "source-PNG-row-and-green-flipped" : "source-PNG-row-flipped";
            }
            else
            {
                bytes = ReadbackPNG(texture, sourceNormal || normal, srgb);
                route = "Unity-GPU-decoded-row-flipped-PNG";
                context.AddWarning("texture.decoded-copy", "Source texture is exported through Unity GPU sampling to RGBA8 PNG (including Unity normal unpack where applicable); source compression/HDR precision is not retained.", source);
            }
            if (bytes == null || bytes.Length == 0) throw new InvalidOperationException("Texture PNG encoding produced no data: " + sourcePath);
            var identity = context.GetUnityIdentity(source);
            string path = context.AllocateAssetPath(source, "Textures", ".png");
            context.WriteBytesFile(path, bytes);
            string filter = source.filterMode == FilterMode.Point ? "Point" : source.filterMode == FilterMode.Trilinear ? "Trilinear" : "Bilinear";
            var settings = Obj("ColorSpace", srgb ? "SRGB" : "Linear", "AlphaSource", "FromSource",
                "AlphaIsTransparency", importer != null && importer.alphaIsTransparency && !sourceNormal,
                "MaxSize", Math.Max(texture.width, texture.height), "GenerateMipmaps", importer != null && importer.mipmapEnabled,
                "KeepCPUCopy", false, "FilterMode", filter,
                "WrapU", WrapName(source.wrapModeU, source, context), "WrapV", WrapName(source.wrapModeV, source, context));
            context.WriteMeta(path, identity, "Texture", 1, settings, "main", source.name);
            context.RegisterSource(source, identity, path);
            context.Count("TextureSources");
            context.AddTextureDetails(source, Obj("Route", route, "ColorSpace", srgb ? "SRGB" : "Linear", "NormalMap", sourceNormal || normal,
                "FlipGreenChannel", flipGreen, "SourceWidth", texture.width, "SourceHeight", texture.height,
                "VerticalPixelFlip", true, "MeshUVAndSTUnchanged", true,
                "RowTransform", "Unity-bottom-left-to-GameSprout-top-first",
                "OriginalPNGBytesPreserved", false,
                "GreenChannelPolicy", copyPNG ? (flipGreen ? "invert-source-G-once" : "unchanged") : "Unity-importer-already-applied-before-readback",
                "UnityGraphicsFormat", texture.graphicsFormat.ToString(), "SourcePath", sourcePath));
            return identity;
        }

        static byte[] ReadbackPNG(Texture2D source, bool normal, bool srgb)
        {
            const string shaderSource = @"Shader ""Hidden/GameSprout/BridgeTextureReadback"" {
                Properties { _MainTex(""Texture"",2D)=""white""{} _DecodeNormal(""Decode normal"",Float)=0 }
                SubShader { Pass { Cull Off ZWrite Off ZTest Always
                CGPROGRAM
                #pragma vertex vert_img
                #pragma fragment frag
                #include ""UnityCG.cginc""
                sampler2D _MainTex; float _DecodeNormal;
                float4 frag(v2f_img input):SV_Target { float4 value=tex2D(_MainTex,input.uv);
                    if(_DecodeNormal>0.5) return float4(UnpackNormal(value)*0.5+0.5,1); return value; }
                ENDCG } } }";
            Shader shader = null; Material copy = null; Texture2D readback = null; Texture2D png = null;
            RenderTexture target = null; RenderTexture previous = RenderTexture.active; bool previousSRGBWrite = GL.sRGBWrite;
            try
            {
                shader = ShaderUtil.CreateShaderAsset(shaderSource, true);
                if (shader == null || ShaderUtil.ShaderHasError(shader)) throw new InvalidOperationException("Unity texture readback shader compilation failed");
                copy = new Material(shader); copy.SetFloat("_DecodeNormal", normal ? 1 : 0);
                target = RenderTexture.GetTemporary(source.width, source.height, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear);
                GL.sRGBWrite = false; Graphics.Blit(source, target, copy); RenderTexture.active = target;
                readback = new Texture2D(source.width, source.height, TextureFormat.RGBAFloat, false, true);
                readback.ReadPixels(new Rect(0, 0, source.width, source.height), 0, 0, false); readback.Apply(false, false);
                Color[] values = readback.GetPixels();
                // Sampling an sRGB source into the linear float target decoded
                // RGB once; re-encode only for a PNG tagged sRGB in GameSprout.
                for (int index = 0; index < values.Length; ++index)
                {
                    Color value = values[index];
                    if (srgb) { Color encoded = value.gamma; encoded.a = value.a; value = encoded; }
                    values[index] = value;
                }
                png = new Texture2D(source.width, source.height, TextureFormat.RGBA32, false, true);
                png.SetPixels(values); png.Apply(false, false);
                // The sampled Unity texture already includes its importer's
                // green-channel policy. Row conversion must not invert G again.
                return EncodeTopFirstPNG(png, false);
            }
            finally
            {
                GL.sRGBWrite = previousSRGBWrite; RenderTexture.active = previous;
                if (target != null) RenderTexture.ReleaseTemporary(target);
                if (png != null) Object.DestroyImmediate(png);
                if (readback != null) Object.DestroyImmediate(readback);
                if (copy != null) Object.DestroyImmediate(copy);
                if (shader != null) Object.DestroyImmediate(shader);
            }
        }

        static byte[] EncodeTopFirstPNG(Texture2D image, bool flipGreen)
        {
            // GetPixels32 is bottom-first in Unity, whereas PNG decoding in
            // GameSprout produces top-first QImage rows uploaded unchanged.
            // Reverse pixel rows before PNG encoding so Runtime UV=(0,0) reads
            // the same texel as Unity. Work only on RGBA bytes: no RGB gamma,
            // alpha or normal-value conversion belongs to this row operation.
            Color32[] pixels = image.GetPixels32();
            int width = image.width;
            for (int y = 0; y < image.height / 2; ++y)
            {
                int first = y * width;
                int last = (image.height - 1 - y) * width;
                for (int x = 0; x < width; ++x)
                {
                    Color32 value = pixels[first + x];
                    pixels[first + x] = pixels[last + x];
                    pixels[last + x] = value;
                }
            }
            if (flipGreen)
                for (int index = 0; index < pixels.Length; ++index)
                    pixels[index].g = (byte)(255 - pixels[index].g);
            image.SetPixels32(pixels); image.Apply(false, false);
            return ImageConversion.EncodeToPNG(image);
        }

        static string WrapName(TextureWrapMode mode, Texture source, ExportContext context)
        {
            switch (mode)
            {
                case TextureWrapMode.Repeat: return "Repeat";
                case TextureWrapMode.Mirror: return "MirrorRepeat";
                case TextureWrapMode.Clamp: return "ClampToEdge";
                default: context.AddWarning("texture.wrap-approximation", "Texture wrap " + mode + " is approximated by ClampToEdge.", source); return "ClampToEdge";
            }
        }
        static float ReadFloat(Material source, string property, float fallback) { return Finite(source.HasProperty(property) ? source.GetFloat(property) : fallback); }
        static float Finite(float value) { if (Single.IsNaN(value) || Single.IsInfinity(value)) throw new InvalidOperationException("Nonfinite source material value"); return value; }
        static object[] ColorArray(Color color) { return new object[] { Finite(color.r), Finite(color.g), Finite(color.b), Finite(color.a) }; }
        static object ReferenceJson(BridgeReference reference) { return String.IsNullOrEmpty(reference.Guid) ? null : reference.ToJson(); }
        static Dictionary<string, object> Obj(params object[] values)
        {
            var result = new Dictionary<string, object>();
            for (int index = 0; index < values.Length; index += 2) result.Add((string)values[index], values[index + 1]);
            return result;
        }
    }
}
