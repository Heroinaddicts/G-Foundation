#include "SceneRendererShadows.h"
#include <array>
#include <stdexcept>
#include <utility>

namespace SceneRendererShadows {
namespace {
using GameSproutRuntime::ShadowCastingMode;
constexpr std::array CastingModes{
    std::pair{ShadowCastingMode::Off, "Off"},
    std::pair{ShadowCastingMode::On, "On"},
    std::pair{ShadowCastingMode::TwoSided, "TwoSided"},
    std::pair{ShadowCastingMode::ShadowsOnly, "ShadowsOnly"}
};

ShadowCastingMode ReadMode(const Json::Value& value, const std::string& path) {
    if (value.isString()) {
        const auto name = value.asString();
        for (const auto& [mode, label] : CastingModes)
            if (name == label) return mode;
    }
    throw std::runtime_error(path + ": expected Off, On, TwoSided or ShadowsOnly");
}
}

Settings Read(const Json::Value& component, const std::string& path) {
    Settings result;
    if (component.isMember("ShadowCastingMode"))
        result.Casting = ReadMode(component["ShadowCastingMode"], path + ".ShadowCastingMode");
    if (component.isMember("ReceiveShadows")) {
        const auto& value = component["ReceiveShadows"];
        if (!value.isBool()) throw std::runtime_error(path + ".ReceiveShadows: expected boolean");
        result.Receive = value.asBool();
    }
    return result;
}

void Serialize(Json::Value& component, ShadowCastingMode casting, bool receive) {
    // Omit the normal defaults so the canonical document stays compact.
    if (casting != ShadowCastingMode::On) {
        const char* name = nullptr;
        for (const auto& [mode, label] : CastingModes)
            if (mode == casting) { name = label; break; }
        if (!name) throw std::runtime_error("ShadowCastingMode: invalid Runtime mode");
        component["ShadowCastingMode"] = name;
    }
    if (!receive) component["ReceiveShadows"] = false;
}
}
