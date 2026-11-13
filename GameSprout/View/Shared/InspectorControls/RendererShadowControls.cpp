#include "RendererShadowControls.h"
#include "ui_RendererShadowControls.h"
#include <algorithm>
#include <array>

namespace {
using GameSproutRuntime::IMeshRendererComponent;
using GameSproutRuntime::ShadowCastingMode;
constexpr std::array CastingModes{ShadowCastingMode::Off, ShadowCastingMode::On,
    ShadowCastingMode::TwoSided, ShadowCastingMode::ShadowsOnly};
}

RendererShadowControls::RendererShadowControls(QWidget* parent)
    : QWidget(parent), _Ui(new Ui::RendererShadowControlsForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->shadowCastingModeCombo, qOverload<int>(&QComboBox::activated),
        this, [this](int index) { SetCasting(index); });
    QObject::connect(_Ui->receiveShadowsCheck, &QCheckBox::clicked,
        this, [this](bool receive) { SetReceive(receive); });
}

RendererShadowControls::~RendererShadowControls() { delete _Ui; }

void RendererShadowControls::Load(const ComponentEditContext& context, const IMeshRendererComponent* renderer) {
    _Context = context;
    _Context.ComponentID = renderer ? renderer->GetComponentInstanceID() : 0;
    Refresh(renderer);
}

void RendererShadowControls::Refresh(const IMeshRendererComponent* renderer) {
    setEnabled(renderer != nullptr);
    const auto mode = renderer ? renderer->GetShadowCastingMode() : ShadowCastingMode::On;
    const auto option = std::find(CastingModes.begin(), CastingModes.end(), mode);
    _Ui->shadowCastingModeCombo->setCurrentIndex(option == CastingModes.end() ? -1 : static_cast<int>(option - CastingModes.begin()));
    _Ui->receiveShadowsCheck->setChecked(renderer ? renderer->GetReceiveShadows() : true);
}

void RendererShadowControls::SetCasting(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= CastingModes.size()) return;
    const auto* renderer = dynamic_cast<IMeshRendererComponent*>(_Context.ResolveComponent());
    if (!renderer) { Refresh(nullptr); return; }
    const auto previous = renderer->GetShadowCastingMode();
    const auto next = CastingModes[index];
    if (previous == next) return;
    Execute("修改投射阴影模式",
        [next](auto& target) { return target.SetShadowCastingMode(next); },
        [previous](auto& target) { return target.SetShadowCastingMode(previous); });
}

void RendererShadowControls::SetReceive(bool receive) {
    const auto* renderer = dynamic_cast<IMeshRendererComponent*>(_Context.ResolveComponent());
    if (!renderer) { Refresh(nullptr); return; }
    const bool previous = renderer->GetReceiveShadows();
    if (previous == receive) return;
    const auto operation = [](bool value) {
        return [value](IMeshRendererComponent& target) { target.SetReceiveShadows(value); return true; };
    };
    Execute("修改接收阴影", operation(receive), operation(previous));
}

void RendererShadowControls::Execute(const std::string& label, const Edit& apply, const Edit& revert) {
    auto* scenes = _Context.Scenes;
    const auto instanceID = _Context.ComponentID;
    const auto fileID = scenes ? scenes->GetComponentFileID(_Context.ObjectID, instanceID) : 0;
    const auto operation = [scenes, fileID, instanceID](const Edit& edit) {
        return [scenes, fileID, instanceID, edit](GameSproutRuntime::IGameObject& object, std::string& error) {
            auto* renderer = fileID ? dynamic_cast<IMeshRendererComponent*>(scenes->QueryComponentByFileID(object.GetInstanceID(), fileID))
                : object.GetComponent<IMeshRendererComponent>();
            if (renderer && (fileID || renderer->GetComponentInstanceID() == instanceID) && edit(*renderer)) return true;
            error = "无法修改阴影属性，MeshRenderer 已移除或 Runtime 拒绝该值。";
            return false;
        };
    };
    _Context.Execute(label, operation(apply), operation(revert));
    Refresh(dynamic_cast<IMeshRendererComponent*>(_Context.ResolveComponent()));
}
