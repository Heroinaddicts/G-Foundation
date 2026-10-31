#pragma once
#include "ComponentEditor.h"
namespace Ui { class CameraComponentEditorForm; }
class CameraComponentEditor final : public ComponentEditor {
public:
    explicit CameraComponentEditor(QWidget* parent);
    ~CameraComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues();
    void SetMainCamera();
    Ui::CameraComponentEditorForm* _Ui = nullptr;
};
