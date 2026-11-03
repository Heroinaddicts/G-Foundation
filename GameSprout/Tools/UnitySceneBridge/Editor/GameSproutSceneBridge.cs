// Install these files under an Editor folder in Unity. Source scenes/assets are only read.
using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditorInternal;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace GameSprout.UnitySceneBridge
{
    public static class GameSproutSceneBridge
    {
        public static string DefaultOutputRoot
        {
            get
            {
                // Unity's Mono maps MyDocuments to the home directory on macOS.
                string documents = Application.platform == RuntimePlatform.OSXEditor
                    ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Documents")
                    : Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
                return Path.Combine(documents, "GameSproutFantasyKingdom");
            }
        }

        [MenuItem("Tools/GameSprout/Export Active Scene to New Project...")]
        public static void ExportWithDialog()
        {
            var path = EditorUtility.SaveFilePanel("导出新的 GameSprout 项目目录（必须不存在）",
                Path.GetDirectoryName(DefaultOutputRoot), Path.GetFileName(DefaultOutputRoot), "");
            if (!String.IsNullOrEmpty(path)) RunMenu(path);
        }
        [MenuItem("Tools/GameSprout/Export Active Scene to GameSproutFantasyKingdom")]
        public static void ExportToDefaultProject() { RunMenu(DefaultOutputRoot); }
        [MenuItem("Tools/GameSprout/Export Active Environment Lighting to...")]
        public static void ExportEnvironmentWithDialog()
        {
            string path = EditorUtility.SaveFilePanel("导出环境光快照（不覆盖文件，不改场景）",
                Path.Combine(DefaultOutputRoot, "Info"), "UnityEnvironmentLighting.json", "json");
            if (String.IsNullOrEmpty(path)) return;
            try { Debug.Log("GameSprout environment lighting snapshot: " + WriteActiveEnvironmentLighting(path)); }
            catch (Exception error) { Debug.LogException(error); EditorUtility.DisplayDialog("环境光导出失败", error.Message, "确定"); }
        }
        [MenuItem("Tools/GameSprout/Validate Active Environment Lighting")]
        public static void ValidateActiveEnvironmentLighting()
        {
            string path = Path.Combine(Path.GetTempPath(), "GameSproutEnvironmentProbe-" + System.Guid.NewGuid().ToString("N") + ".json");
            Debug.Log("GameSprout environment lighting validation: " + WriteActiveEnvironmentLighting(path));
        }
        // Explicit environment-only snapshot for an existing review project.
        // The caller decides when/how to apply EnvironmentLighting to its scene.
        public static string WriteActiveEnvironmentLighting(string outputPath)
        {
            var scene = SceneManager.GetActiveScene();
            if (!scene.IsValid() || !scene.isLoaded) throw new InvalidOperationException("No active loaded Unity scene");
            if (EditorApplication.isPlayingOrWillChangePlaymode) throw new InvalidOperationException("Stop Unity Play mode before exporting environment lighting");
            string path = Path.GetFullPath(outputPath);
            if (Directory.Exists(path) || File.Exists(path)) throw new IOException("Output already exists; environment export refuses to overwrite: " + path);
            string unityRoot = Path.GetFullPath(Path.Combine(Application.dataPath, "..")).TrimEnd(Path.DirectorySeparatorChar);
            if (path == unityRoot || path.StartsWith(unityRoot + Path.DirectorySeparatorChar, StringComparison.Ordinal))
                throw new IOException("Choose an environment output outside the Unity project");
            if (!Directory.Exists(Path.GetDirectoryName(path))) throw new DirectoryNotFoundException("Output parent directory must already exist");
            Dictionary<string, object> details;
            var environment = BridgeEnvironmentLighting.Capture(out details);
            var document = BridgeJson.Object("Version", 2, "CoordinateSystem", "LeftHandedYUp", "UnityVersion", Application.unityVersion,
                "SourceScene", scene.path, "SourceSceneDirty", scene.isDirty,
                "EnvironmentLighting", environment, "EnvironmentLightingConversion", details);
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(BridgeJson.Serialize(document));
            bool created = false;
            try
            {
                using (var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                { created = true; file.Write(bytes, 0, bytes.Length); }
            }
            catch { if (created) File.Delete(path); throw; }
            if ((bool)details["FinalProbeAllZero"])
                Debug.LogWarning("Unity final ambient probe is all zero. Export retained black SH; no sky-color or intensity fallback was fabricated.");
            return path;
        }
        static void RunMenu(string path)
        {
            try
            {
                var result = ExportActiveScene(path);
                Debug.Log("GameSprout bridge completed: " + result);
                EditorUtility.DisplayDialog("GameSprout 导出完成", result + "\n\n限制及身份映射记录在 Info/UnityExportManifest.json。", "确定");
            }
            catch (Exception error)
            {
                Debug.LogException(error);
                EditorUtility.DisplayDialog("GameSprout 导出失败", error.Message + "\n不会覆盖已有项目或修改 Unity 源文件。", "确定");
            }
        }
        // -executeMethod GameSprout.UnitySceneBridge.GameSproutSceneBridge.ExportFromCommandLine
        // -gamesproutOutput /absolute/new/project [-gamesproutScene Assets/.../Demo_01.unity]
        public static void ExportFromCommandLine()
        {
            var args = Environment.GetCommandLineArgs(); string output = null, source = null;
            for (int i = 0; i + 1 < args.Length; ++i)
            {
                if (args[i] == "-gamesproutOutput") output = args[++i];
                else if (args[i] == "-gamesproutScene") source = args[++i];
            }
            if (String.IsNullOrEmpty(output)) throw new ArgumentException("Missing -gamesproutOutput");
            if (!String.IsNullOrEmpty(source))
            {
                for (int i = 0; i < SceneManager.sceneCount; ++i)
                    if (SceneManager.GetSceneAt(i).isDirty) throw new InvalidOperationException("Refusing to replace a dirty loaded Unity scene");
                EditorSceneManager.OpenScene(source, OpenSceneMode.Single);
            }
            Debug.Log("GameSprout bridge completed: " + ExportActiveScene(output));
        }
        public static string ExportActiveScene(string outputRoot)
        {
            var scene = SceneManager.GetActiveScene();
            if (!scene.IsValid() || !scene.isLoaded) throw new InvalidOperationException("No active loaded Unity scene");
            if (EditorApplication.isPlayingOrWillChangePlaymode) throw new InvalidOperationException("Stop Unity Play mode before exporting the scene snapshot");
            var target = Path.GetFullPath(outputRoot);
            if (Directory.Exists(target) || File.Exists(target)) throw new IOException("Output already exists; export refuses to overwrite: " + target);
            var unityRoot = Path.GetFullPath(Path.Combine(Application.dataPath, "..")).TrimEnd(Path.DirectorySeparatorChar);
            if (target == unityRoot || target.StartsWith(unityRoot + Path.DirectorySeparatorChar, StringComparison.Ordinal))
                throw new IOException("Choose an output outside the Unity project");
            var parent = Path.GetDirectoryName(target);
            if (String.IsNullOrEmpty(parent) || !Directory.Exists(parent)) throw new DirectoryNotFoundException("Output parent directory must already exist");
            var stage = Path.Combine(parent, "." + Path.GetFileName(target) + ".export-" + System.Guid.NewGuid().ToString("N"));
            if (Directory.Exists(stage) || File.Exists(stage)) throw new IOException("Export transaction path already exists");
            Directory.CreateDirectory(stage);
            try
            {
                var exporter = new SceneExporter(scene, new ExportContext(stage));
                exporter.Export(target);
                // Directory.Move fails rather than merges when another writer creates target meanwhile.
                Directory.Move(stage, target); return target;
            }
            catch
            {
                // This directory was created exclusively for this failed export; never touch an existing project.
                if (Directory.Exists(stage)) Directory.Delete(stage, true);
                throw;
            }
            finally { EditorUtility.ClearProgressBar(); }
        }
    }

    internal sealed class SceneExporter
    {
        readonly Scene scene;
        readonly ExportContext context;
        readonly List<Transform> transforms = new List<Transform>();
        readonly Dictionary<Transform, ulong> nodeIDs = new Dictionary<Transform, ulong>();
        readonly Dictionary<string, ulong> tags = new Dictionary<string, ulong>();
        readonly int[] layers = new int[32];
        readonly string[] layerNames = new string[32];
        ulong nextID = 1;
        ulong mainCameraID;

        public SceneExporter(Scene source, ExportContext destination) { scene = source; context = destination; }
        static object[] Plain(Vector3 value) { return new object[] { value.x, value.y, value.z }; }
        static object[] UV(Vector4 value) { return new object[] { value.x, value.y, value.z, value.w }; }
        void Gather(Transform transform)
        {
            transforms.Add(transform); nodeIDs.Add(transform, nextID++);
            for (int i = 0; i < transform.childCount; ++i) Gather(transform.GetChild(i));
        }
        public void Export(string finalOutput)
        {
            foreach (var root in scene.GetRootGameObjects()) Gather(root.transform);
            ConfigureProject();
            Dictionary<string, object> environmentDetails;
            var environment = BridgeEnvironmentLighting.Capture(out environmentDetails);
            if ((bool)environmentDetails["FinalProbeAllZero"])
                context.AddWarning("AmbientProbeAllZero", "Unity final ambient probe is all zero. Export keeps black SH without fabricating sky colors or multiplying source intensity again.");
            var objects = new List<object>();
            var main = Camera.main;
            if (main && main.gameObject.scene == scene && main.enabled && main.gameObject.activeInHierarchy)
                mainCameraID = nodeIDs[main.transform];
            for (int index = 0; index < transforms.Count; ++index)
            {
                if (index % 64 == 0) EditorUtility.DisplayProgressBar("导出 GameSprout 场景快照", transforms[index].name, index / (float)Math.Max(1, transforms.Count));
                var transform = transforms[index]; var gameObject = transform.gameObject;
                var components = new List<object>();
                components.Add(Component("GameSproutRuntime.Transform", "Position", ExportContext.Vector(transform.localPosition),
                    "Rotation", ExportContext.Rotation(transform.localRotation), "Scale", ExportContext.Scale(transform.localScale)));
                foreach (var component in gameObject.GetComponents<Component>()) ExportComponent(component, gameObject, components);
                ulong parentID = 0;
                if (transform.parent) nodeIDs.TryGetValue(transform.parent, out parentID);
                ulong tagID; if (!tags.TryGetValue(gameObject.tag, out tagID)) throw new InvalidDataException("Undefined Unity Tag: " + gameObject.tag);
                uint staticFlags = (uint)GameObjectUtility.GetStaticEditorFlags(gameObject);
                if ((staticFlags & ~127u) != 0) context.AddWarning("UnsupportedStaticBits", "Only the seven current GameSprout StaticFlags are preserved.", gameObject);
                objects.Add(BridgeJson.Object("ID", nodeIDs[transform], "ParentID", parentID, "Name", gameObject.name,
                    "Active", gameObject.activeSelf, "Layer", layers[gameObject.layer], "TagID", tagID, "StaticFlags", staticFlags & 127u, "Components", components));
                context.Count("Nodes"); if (gameObject.activeInHierarchy) context.Count("ActiveNodes");
                context.Report.Objects.Add(BridgeJson.Object("UnityGlobalID", GlobalObjectId.GetGlobalObjectIdSlow(gameObject).ToString(),
                    "Path", HierarchyPath(transform), "NodeID", nodeIDs[transform]));
            }
            string relativeScene = "Scenes/" + ExportContext.SafeName(scene.name) + ".scene";
            var document = BridgeJson.Object("Version", 2, "Name", scene.name, "NextObjectID", nextID,
                "MainCameraID", mainCameraID, "EnvironmentLighting", environment, "Objects", objects);
            context.WriteJsonFile(relativeScene, document);
            var sceneReference = new BridgeReference(ExportContext.Hash("GameSprout.UnitySceneBridge.Scene:" + AssetDatabase.AssetPathToGUID(scene.path)), 1);
            context.WriteMeta(relativeScene, sceneReference, "Scene", 13, BridgeJson.Object("MainObjectID", 1UL), "main", scene.name);
            context.WriteProjectJson("Info/project.json", BridgeJson.Object("Project", BridgeJson.Object("name", scene.name)));
            context.WriteProjectJson("UserSettings/EditorState.json", BridgeJson.Object("LastScenePath", relativeScene));
            context.Report.Assets.Add(BridgeJson.Object("UnityPath", scene.path, "UnityGuid", AssetDatabase.AssetPathToGUID(scene.path), "Path", relativeScene, "Reference", sceneReference.ToJson()));
            context.AddWarning("LightingEnvironmentPartial", "Scene-wide ambient diffuse SH and directional shadow settings are exported. Spatial light probes, baked GI/lightmaps, reflection probes, skybox rendering and tone mapping remain unsupported; source environment is recorded in this manifest.");
            var warningValues = new List<object>(); foreach (var warning in context.Report.Warnings.Values) warningValues.Add(warning);
            var layerMapping = new List<object>(); for (int i = 0; i < 32; ++i) layerMapping.Add(BridgeJson.Object("UnityLayer", i, "UnityName", LayerMask.LayerToName(i), "GameSproutLayer", layers[i]));
            context.WriteProjectJson("Info/UnityExportManifest.json", BridgeJson.Object("Version", 2,
                "UnityVersion", Application.unityVersion, "SourceScene", scene.path, "SourceSceneDirty", scene.isDirty,
                "OutputProject", finalOutput, "CoordinateConversion", "Unity LH/YUp/+ZForward -> Runtime LH/YUp/+ZForward: preserve positions, quaternions, triangle winding, tangents and SH",
                "PrefabPolicy", "Expanded resolved hierarchy snapshot, including inactive nodes and final property/material overrides; prefab linkage and instance override metadata are not exported",
                "SkinningPolicy", "SkinnedMeshRenderer.BakeMesh local current pose exported as static Mesh; no Animator state or animation playback",
                "ColorSpace", QualitySettings.activeColorSpace.ToString(), "LayerMapping", layerMapping,
                "Counts", context.Report.Counts, "Assets", context.Report.Assets, "Objects", context.Report.Objects,
                "MaterialConversions", context.Report.MaterialConversions, "TextureConversions", context.Report.TextureConversions,
                "EnvironmentLighting", environment, "EnvironmentLightingConversion", environmentDetails,
                "Warnings", warningValues, "UnityEnvironment", EnvironmentSettings()));
        }
        Dictionary<string, object> Component(string type, params object[] fields)
        {
            var value = BridgeJson.Object(fields); value.Add("Type", type); value.Add("ID", nextID++); context.Count("ExportedComponents"); return value;
        }
        void ExportComponent(Component source, GameObject owner, List<object> output)
        {
            if (!source) { context.AddWarning("MissingScript", "Missing Unity component script was omitted.", owner); return; }
            if (source is Transform) return;
            var meshFilter = source as MeshFilter;
            if (meshFilter)
            {
                if (meshFilter.sharedMesh) output.Add(Component("GameSproutRuntime.Mesh", "Mesh", GameSproutMeshExport.Export(meshFilter.sharedMesh, context).ToJson()));
                else context.AddWarning("MeshFilterWithoutMesh", "MeshFilter has no source mesh.", source);
                return;
            }
            var skinned = source as SkinnedMeshRenderer;
            if (skinned)
            {
                var baked = new Mesh(); baked.name = skinned.sharedMesh ? skinned.sharedMesh.name + "_Pose" : skinned.name + "_Pose";
                try
                {
                    skinned.BakeMesh(baked, false);
                    output.Add(Component("GameSproutRuntime.Mesh", "Mesh", GameSproutMeshExport.Export(baked, context, skinned).ToJson()));
                    output.Add(Renderer(skinned)); context.Count("BakedSkinnedRenderers");
                    context.AddWarning("SkinnedPoseSnapshot", "Skinned renderer exported as a static local-pose snapshot; skeleton animation is not exported.", skinned);
                }
                finally { Object.DestroyImmediate(baked); }
                return;
            }
            var renderer = source as MeshRenderer;
            if (renderer) { output.Add(Renderer(renderer)); return; }
            var collider = source as Collider;
            if (collider) { ExportCollider(collider, output); return; }
            var rigidbody = source as Rigidbody;
            if (rigidbody)
            {
                output.Add(Component("GameSproutRuntime.Rigidbody", "Settings", BridgeJson.Object("Enabled", true,
                    "MotionType", rigidbody.isKinematic ? "Kinematic" : "Dynamic", "Mass", rigidbody.mass,
                    "GravityFactor", rigidbody.useGravity ? 1f : 0f, "LinearDamping", rigidbody.linearDamping,
                    "AngularDamping", rigidbody.angularDamping, "Constraints", ((uint)rigidbody.constraints >> 1) & 63u,
                    "Continuous", rigidbody.collisionDetectionMode != CollisionDetectionMode.Discrete)));
                context.Count("Rigidbodies"); return;
            }
            var light = source as Light;
            if (light)
            {
                if (light.type != LightType.Directional) { context.AddWarning("UnsupportedLightType", "Only directional lights are currently supported; light type " + light.type + " omitted.", light); return; }
                if (light.useColorTemperature) context.AddWarning("LightTemperature", "Color temperature is not evaluated; exported light.color and intensity only.", light);
                output.Add(Component("GameSproutRuntime.Light", "Settings", BridgeJson.Object("Type", "Directional", "Enabled", light.enabled,
                    "Color", ExportContext.Color(light.color.linear), "Intensity", light.intensity, "CullingMask", ConvertMask((uint)light.cullingMask),
                    "Shadows", GameSproutShadowExport.Capture(light, context))));
                context.Count("DirectionalLights");
                return;
            }
            var camera = source as Camera;
            if (camera)
            {
                output.Add(Component("GameSproutRuntime.Camera", "Settings", BridgeJson.Object("Projection", camera.orthographic ? "Orthographic" : "Perspective",
                    "VerticalFovRadians", camera.fieldOfView * Mathf.Deg2Rad, "OrthographicHeight", camera.orthographicSize * 2f,
                    "NearPlane", camera.nearClipPlane, "FarPlane", camera.farClipPlane, "CullingMask", ConvertMask((uint)camera.cullingMask))));
                context.Count("Cameras");
                if (mainCameraID == 0 && camera.enabled && owner.activeInHierarchy) mainCameraID = nodeIDs[owner.transform];
                if (!camera.enabled) context.AddWarning("DisabledCamera", "Camera component is preserved but not selected as the initial main camera.", camera);
                if (camera.targetTexture || camera.clearFlags != CameraClearFlags.SolidColor) context.AddWarning("CameraEnvironment", "RenderTexture and skybox/depth-clear composition are not supported by this scene snapshot.", camera);
                return;
            }
            var animator = source as Animator;
            if (animator)
            {
                context.Count("AnimatorsOmitted"); if (!animator.runtimeAnimatorController) context.Count("AnimatorsWithoutController");
                context.AddWarning("AnimatorNotExported", "Animation state/controllers are omitted; skinned renderers use the exported pose snapshot.", animator); return;
            }
            var type = source.GetType().FullName;
            if (type == "UnityEngine.Rendering.Universal.UniversalAdditionalLightData")
            {
                context.Count("URPAdditionalLightData");
                context.AddWarning("URPAdditionalLightData", "Supported Light color/intensity/culling mask and active pipeline directional shadow settings are exported; URP-specific rendering layers, cookies and exact soft-shadow quality are not consumed.", source);
                return;
            }
            context.AddWarning("UnsupportedComponent:" + type, "Component is not supported by the current Runtime snapshot bridge and was omitted: " + type, source);
        }
        Dictionary<string, object> Renderer(Renderer renderer)
        {
            var materials = new List<object>();
            foreach (var material in renderer.sharedMaterials) materials.Add(material ? GameSproutMaterialExport.Export(material, context).ToJson() : null);
            context.Count("RendererComponents"); if (renderer.enabled && renderer.gameObject.activeInHierarchy) context.Count("ActiveEnabledRenderers");
            if (renderer.shadowCastingMode != ShadowCastingMode.On || !renderer.receiveShadows)
                context.AddWarning("RendererShadowSettings", "Current Runtime shadow pass uses the supported built-in opaque geometry; individual Renderer cast/receive modes are not yet exported.", renderer);
            if (renderer.lightmapIndex >= 0 && renderer.lightmapIndex < 65534)
                context.AddWarning("RendererLightmap", "Renderer uses a baked lightmap; binding and scale/offset are not exported to unsupported Runtime fields.", renderer);
            return Component("GameSproutRuntime.MeshRenderer", "Visible", renderer.enabled, "Materials", materials);
        }
        void ExportCollider(Collider collider, List<object> output)
        {
            var box = collider as BoxCollider; var sphere = collider as SphereCollider; var capsule = collider as CapsuleCollider; var mesh = collider as MeshCollider;
            if (!box && !sphere && !capsule && !mesh) { context.AddWarning("UnsupportedCollider", "Unsupported collider type: " + collider.GetType().Name, collider); return; }
            var settings = BridgeJson.Object("Enabled", collider.enabled, "IsTrigger", collider.isTrigger,
                "Center", ExportContext.Vector(box ? box.center : sphere ? sphere.center : capsule ? capsule.center : Vector3.zero),
                "Rotation", new object[] { 0f, 0f, 0f, 1f }, "Material", collider.sharedMaterial ? ExportPhysicsMaterial(collider.sharedMaterial).ToJson() : null);
            Dictionary<string, object> shape; string type;
            if (box) { type = "BoxCollider"; shape = BridgeJson.Object("Size", Plain(box.size)); }
            else if (sphere) { type = "SphereCollider"; shape = BridgeJson.Object("Radius", sphere.radius); }
            else if (capsule) { type = "CapsuleCollider"; shape = BridgeJson.Object("Radius", capsule.radius, "Height", Mathf.Max(capsule.height, capsule.radius * 2), "Axis", capsule.direction == 0 ? "X" : capsule.direction == 2 ? "Z" : "Y"); }
            else
            {
                if (!mesh.sharedMesh) { context.AddWarning("MeshColliderWithoutMesh", "MeshCollider has no sharedMesh and was omitted.", mesh); return; }
                type = "MeshCollider"; shape = BridgeJson.Object("Mesh", GameSproutMeshExport.Export(mesh.sharedMesh, context).ToJson(), "Convex", mesh.convex);
            }
            output.Add(Component("GameSproutRuntime." + type, "Settings", settings, "Shape", shape)); context.Count(type + "Components");
        }
        BridgeReference ExportPhysicsMaterial(PhysicsMaterial material)
        {
            BridgeReference reference; if (context.TryGetSource(material, out reference)) return reference;
            reference = context.GetUnityIdentity(material); var path = context.AllocateAssetPath(material, "PhysicsMaterials", ".physicmaterial");
            context.WriteJsonFile(path, BridgeJson.Object("Version", 1, "Friction", material.dynamicFriction,
                "Restitution", material.bounciness, "FrictionCombine", material.frictionCombine.ToString(), "RestitutionCombine", material.bounceCombine.ToString()));
            context.WriteMeta(path, reference, "PhysicsMaterial", 19, BridgeJson.Object("MainObjectID", reference.LocalID), "main", material.name);
            context.RegisterSource(material, reference, path); context.Count("PhysicsMaterialSources");
            if (!Mathf.Approximately(material.staticFriction, material.dynamicFriction)) context.AddWarning("StaticFrictionMerged", "Runtime has one friction coefficient; Unity dynamicFriction is exported while distinct staticFriction is recorded as unsupported.", material);
            return reference;
        }
        uint ConvertMask(uint source)
        {
            uint result = 0; for (int layer = 0; layer < 32; ++layer) if ((source & (1u << layer)) != 0) result |= 1u << layers[layer]; return result;
        }
        void ConfigureProject()
        {
            var assigned = new bool[32]; for (int i = 0; i < 32; ++i) { layers[i] = -1; layerNames[i] = ""; }
            layers[0] = 0; layers[2] = 1; layers[5] = 2; assigned[0] = assigned[1] = assigned[2] = true;
            layerNames[0] = "Default"; layerNames[1] = "Ignore Raycast"; layerNames[2] = "UI";
            // Reserve named layers before unnamed slots, keeping source indices when possible.
            for (int pass = 0; pass < 2; ++pass) for (int source = 0; source < 32; ++source)
            {
                string name = LayerMask.LayerToName(source);
                if (layers[source] >= 0 || (pass == 0) != !String.IsNullOrEmpty(name)) continue;
                int target = source >= 3 && !assigned[source] ? source : Array.FindIndex(assigned, 3, value => !value);
                if (target < 0) throw new InvalidDataException("Cannot map all Unity layers");
                layers[source] = target; assigned[target] = true; layerNames[target] = name;
            }
            var tagValues = new List<object>(); tags.Add("Untagged", 0); tagValues.Add(BridgeJson.Object("ID", 0UL, "Name", "Untagged"));
            ulong tagID = 1;
            foreach (var tag in InternalEditorUtility.tags) if (!tags.ContainsKey(tag)) { tags.Add(tag, tagID); tagValues.Add(BridgeJson.Object("ID", tagID++, "Name", tag)); }
            var collisions = new uint[32];
            for (int a = 0; a < 32; ++a) for (int b = 0; b < 32; ++b)
                if (!Physics.GetIgnoreLayerCollision(a, b)) collisions[layers[a]] |= 1u << layers[b];
            context.WriteProjectJson("ProjectSettings/ProjectSettings.json", BridgeJson.Object("Version", 1, "Layers", layerNames,
                "NextTagID", tagID, "Tags", tagValues, "CollisionMatrix", collisions));
        }
        static string HierarchyPath(Transform transform)
        {
            string path = transform.name + "[" + transform.GetSiblingIndex() + "]";
            while (transform.parent) { transform = transform.parent; path = transform.name + "[" + transform.GetSiblingIndex() + "]/" + path; }
            return path;
        }
        static Dictionary<string, object> EnvironmentSettings()
        {
            var lightmaps = new List<object>();
            foreach (var lightmap in LightmapSettings.lightmaps)
                lightmaps.Add(BridgeJson.Object("Color", ExportContext.SourcePath(lightmap.lightmapColor), "Direction", ExportContext.SourcePath(lightmap.lightmapDir), "ShadowMask", ExportContext.SourcePath(lightmap.shadowMask)));
            return BridgeJson.Object("AmbientMode", RenderSettings.ambientMode.ToString(), "AmbientIntensity", RenderSettings.ambientIntensity,
                "AmbientSky", ExportContext.Color(RenderSettings.ambientSkyColor), "AmbientEquator", ExportContext.Color(RenderSettings.ambientEquatorColor),
                "AmbientGround", ExportContext.Color(RenderSettings.ambientGroundColor), "Skybox", ExportContext.SourcePath(RenderSettings.skybox),
                "Fog", RenderSettings.fog, "FogColor", ExportContext.Color(RenderSettings.fogColor), "FogMode", RenderSettings.fogMode.ToString(),
                "FogDensity", RenderSettings.fogDensity, "FogStart", RenderSettings.fogStartDistance, "FogEnd", RenderSettings.fogEndDistance,
                "ReflectionIntensity", RenderSettings.reflectionIntensity, "Lightmaps", lightmaps, "Gravity", Plain(Physics.gravity));
        }
    }

    internal static class BridgeEnvironmentLighting
    {
        const string Basis = "[1,y,z,x,xy,yz,3*z*z-1,xz,x*x-y*y]";
        public static Dictionary<string, object> Capture(out Dictionary<string, object> details)
        {
            // C# ambientProbe is bound to GetFinalAmbientProbe, already linear,
            // diffuse-convolved and scaled for Skybox by gamma-to-linear ambient
            // intensity. Flat/Trilight generate their final SH from linearized
            // ambient colors and ignore the Skybox intensity slider.
            // Do not mutate RenderSettings or call DynamicGI.UpdateEnvironment.
            SphericalHarmonicsL2 probe = RenderSettings.ambientProbe;
            var coefficients = Coefficients(probe);
            bool zero = true;
            for (int coefficient = 0; coefficient < 9; ++coefficient)
                for (int channel = 0; channel < 3; ++channel)
                    if (probe[channel, coefficient] != 0) zero = false;
            Color sky = LinearColor(RenderSettings.ambientSkyColor);
            Color equator = LinearColor(RenderSettings.ambientEquatorColor);
            Color ground = LinearColor(RenderSettings.ambientGroundColor);
            var environment = BridgeJson.Object("Mode", "SphericalHarmonics", "Intensity", 1f,
                "AmbientColor", ExportContext.Color(sky), "SkyColor", ExportContext.Color(sky),
                "EquatorColor", ExportContext.Color(equator), "GroundColor", ExportContext.Color(ground),
                "SHCoefficients", coefficients);
            float maximumError;
            var validation = ValidateDirections(probe, out maximumError);
            var synthetic = new SphericalHarmonicsL2();
            for (int channel = 0; channel < 3; ++channel)
                for (int coefficient = 0; coefficient < 9; ++coefficient)
                    synthetic[channel, coefficient] = (channel + 1) * (coefficient + 1) * (coefficient % 2 == 0 ? .03125f : -.015625f);
            float syntheticError;
            var syntheticValidation = ValidateDirections(synthetic, out syntheticError);
            details = BridgeJson.Object("SourceAmbientMode", RenderSettings.ambientMode.ToString(),
                "SourceAmbientIntensity", Finite(RenderSettings.ambientIntensity), "ExportedIntensity", 1f,
                "SourceColorSpace", QualitySettings.activeColorSpace.ToString(), "SHColorSpace", "Linear",
                "Normalization", "Already diffuse-convolved E/pi polynomial coefficients; no extra basis normalization or pi factor",
                "SHBasis", Basis, "CoordinateConversion", "Identity: Unity and Runtime use the same LH/Y-up/+Z-forward world coordinates",
                "IntensityPolicy", "RenderSettings.ambientProbe is final; source Skybox intensity is already gamma-to-linear scaled. Do not multiply again. Flat/Trilight ignore Skybox intensity.",
                "TrilightApproxNotUsed", true, "AllModesUseFinalProbeSnapshot", true,
                "FinalProbeAllZero", zero, "ZeroProbePolicy", "Preserve exact black SH; do not fabricate sky-color fallback",
                "SourceFinalSHCoefficients", coefficients, "ExportedSHCoefficients", coefficients,
                "AmbientColorSourceSRGB", ExportContext.Color(RenderSettings.ambientSkyColor),
                "SkyColorLinear", ExportContext.Color(sky), "EquatorColorLinear", ExportContext.Color(equator), "GroundColorLinear", ExportContext.Color(ground),
                "DirectionalValidation", validation, "DirectionalValidationMaxError", maximumError,
                "SyntheticValidation", syntheticValidation, "SyntheticValidationMaxError", syntheticError);
            return environment;
        }
        static object[] Coefficients(SphericalHarmonicsL2 probe)
        {
            var values = new object[9];
            for (int index = 0; index < values.Length; ++index)
            {
                values[index] = new object[] { Finite(probe[0, index]), Finite(probe[1, index]), Finite(probe[2, index]) };
            }
            return values;
        }
        static Color LinearColor(Color source)
        {
            Color result = source.linear;
            result.a = source.a;
            if (Finite(result.r) < 0 || Finite(result.g) < 0 || Finite(result.b) < 0 || Finite(result.a) < 0 || result.a > 1)
                throw new InvalidDataException("Ambient context color must be nonnegative linear RGB and alpha in [0,1]");
            return result;
        }
        static List<object> ValidateDirections(SphericalHarmonicsL2 probe, out float maximumError)
        {
            var directions = new List<Vector3>();
            for (int x = -1; x <= 1; ++x)
                for (int y = -1; y <= 1; ++y)
                    for (int z = -1; z <= 1; ++z)
                        if (x != 0 || y != 0 || z != 0) directions.Add(new Vector3(x, y, z).normalized);
            var evaluated = new Color[directions.Count];
            probe.Evaluate(directions.ToArray(), evaluated);
            var result = new List<object>(); maximumError = 0;
            for (int index = 0; index < directions.Count; ++index)
            {
                Vector3 source = directions[index];
                var runtime = source;
                var basis = new[] { 1f, runtime.y, runtime.z, runtime.x, runtime.x * runtime.y,
                    runtime.y * runtime.z, 3 * runtime.z * runtime.z - 1, runtime.x * runtime.z,
                    runtime.x * runtime.x - runtime.y * runtime.y };
                var rgb = new float[3];
                for (int channel = 0; channel < 3; ++channel)
                    for (int coefficient = 0; coefficient < 9; ++coefficient)
                    {
                        rgb[channel] += probe[channel, coefficient] * basis[coefficient];
                    }
                float error = Mathf.Max(Mathf.Abs(rgb[0] - evaluated[index].r), Mathf.Max(Mathf.Abs(rgb[1] - evaluated[index].g), Mathf.Abs(rgb[2] - evaluated[index].b)));
                float scale = Mathf.Max(1f, Mathf.Max(Mathf.Abs(evaluated[index].r), Mathf.Max(Mathf.Abs(evaluated[index].g), Mathf.Abs(evaluated[index].b))));
                Finite(error); if (error > .0001f * scale) throw new InvalidDataException("Unity SH Evaluate disagrees with exported polynomial contract at direction " + source + "; error=" + error);
                maximumError = Mathf.Max(maximumError, error);
                result.Add(BridgeJson.Object("UnityDirection", new object[] { source.x, source.y, source.z },
                    "RuntimeDirection", new object[] { runtime.x, runtime.y, runtime.z },
                    "UnityEvaluate", new object[] { Finite(evaluated[index].r), Finite(evaluated[index].g), Finite(evaluated[index].b) },
                    "RuntimePolynomial", rgb, "MaxAbsoluteError", error));
            }
            return result;
        }
        static float Finite(float value)
        {
            if (Single.IsNaN(value) || Single.IsInfinity(value)) throw new InvalidDataException("Nonfinite ambient lighting value");
            return value;
        }
    }
}
