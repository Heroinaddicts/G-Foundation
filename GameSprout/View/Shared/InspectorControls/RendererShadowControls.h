#pragma once
#include "ComponentEditContext.h"
#include <QWidget>

namespace Ui { class RendererShadowControlsForm; }

class RendererShadowControls final : public QWidget {
public:
    explicit RendererShadowControls(QWidget* parent);
    ~RendererShadowControls() override;
    void Load(const ComponentEditContext&, const GameSproutRuntime::IMeshRendererComponent*);
private:
    using Edit = std::function<bool(GameSproutRuntime::IMeshRendererComponent&)>;
    void SetCasting(int index);
    void SetReceive(bool receive);
    void Execute(const std::string& label, const Edit& apply, const Edit& revert);
    void Refresh(const GameSproutRuntime::IMeshRendererComponent*);
    ComponentEditContext _Context;
    Ui::RendererShadowControlsForm* _Ui;
};
