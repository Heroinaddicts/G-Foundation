// Editor-only conversion of the active render pipeline's directional shadows.
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

namespace GameSprout.UnitySceneBridge
{
    internal static class GameSproutShadowExport
    {
        public static Dictionary<string, object> Capture(Light light, ExportContext context)
        {
            // Read the currently selected quality pipeline, not an arbitrary
            // project-default asset. SerializedObject keeps this optional bridge
            // independent of a mandatory URP assembly reference.
            var pipeline = GraphicsSettings.currentRenderPipeline;
            var asset = pipeline ? new SerializedObject(pipeline) : null;
            bool supported = Bool(asset, "m_MainLightShadowsSupported", QualitySettings.shadows != ShadowQuality.Disable);
            bool supportsSoft = Bool(asset, "m_SoftShadowsSupported", true);
            float distance = Number(asset, "m_ShadowDistance", QualitySettings.shadowDistance);
            int resolution = Integer(asset, "m_MainLightShadowmapResolution", 2048);
            int cascades = Integer(asset, "m_ShadowCascadeCount", QualitySettings.shadowCascades == 0 ? 1 : QualitySettings.shadowCascades);
            var additional = light.GetComponent("UniversalAdditionalLightData");
            var additionalData = additional ? new SerializedObject(additional) : null;
            bool pipelineBias = asset != null && Bool(additionalData, "m_UsePipelineSettings", true);
            float depthBias = pipelineBias ? Number(asset, "m_ShadowDepthBias", light.shadowBias) : light.shadowBias;
            float normalBias = pipelineBias ? Number(asset, "m_ShadowNormalBias", light.shadowNormalBias) : light.shadowNormalBias;

            if (resolution != 256 && resolution != 512 && resolution != 1024 && resolution != 2048 && resolution != 4096)
            {
                int sourceResolution = resolution;
                resolution = Mathf.ClosestPowerOfTwo(Mathf.Clamp(resolution, 256, 4096));
                context.AddWarning("ShadowResolutionClamped", "Source shadow atlas resolution " + sourceResolution + " becomes supported resolution " + resolution + ".", light);
            }
            if (cascades != 1 && cascades != 2 && cascades != 4)
            {
                context.AddWarning("ShadowCascadeCountApproximation", "Current Runtime supports 1/2/4 cascades; source " + cascades + " becomes 4.", light);
                cascades = 4;
            }
            if (!Finite(distance) || distance < 0 || !Finite(depthBias) || depthBias < 0 || depthBias > 10 ||
                !Finite(normalBias) || normalBias < 0 || normalBias > 10 || !Finite(light.shadowStrength) || light.shadowStrength < 0 || light.shadowStrength > 1)
                throw new InvalidOperationException("Invalid directional shadow settings: " + light.name);
            bool enabled = supported && light.shadows != LightShadows.None && distance > 0;
            if (enabled)
            {
                context.Count("DirectionalShadowLights");
                context.AddWarning("DirectionalShadowSamplingApproximation", "Directional shadow settings are exported in per-cascade texel units. Runtime uses its own cascade splits and receiver-side bias/PCF; it does not reproduce URP's exact caster bias or filter kernel.", light);
            }
            return BridgeJson.Object("Enabled", enabled, "Strength", light.shadowStrength,
                "Distance", distance > 0 ? distance : 50f, "Resolution", resolution, "CascadeCount", cascades,
                "DepthBias", depthBias, "NormalBias", normalBias, "Soft", supportsSoft && light.shadows == LightShadows.Soft);
        }

        static bool Finite(float value) { return !Single.IsNaN(value) && !Single.IsInfinity(value); }
        static bool Bool(SerializedObject value, string name, bool fallback)
        {
            var property = value == null ? null : value.FindProperty(name);
            return property == null ? fallback : property.boolValue;
        }
        static int Integer(SerializedObject value, string name, int fallback)
        {
            var property = value == null ? null : value.FindProperty(name);
            return property == null ? fallback : property.intValue;
        }
        static float Number(SerializedObject value, string name, float fallback)
        {
            var property = value == null ? null : value.FindProperty(name);
            return property == null ? fallback : property.floatValue;
        }
    }
}
