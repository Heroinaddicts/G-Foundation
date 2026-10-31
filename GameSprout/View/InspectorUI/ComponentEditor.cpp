#include "ComponentEditor.h"
#include "TransformComponentEditor.h"
#include "RectTransformComponentEditor.h"
#include "CanvasComponentEditor.h"
#include "ImageComponentEditor.h"
#include "MeshComponentEditor.h"
#include "MeshRendererComponentEditor.h"
#include "CameraComponentEditor.h"
#include <QApplication>
#include <QLineEdit>
#include <QLocale>
#include <cmath>
#include <limits>

void ComponentEditor::Bind(const ComponentEditContext& context) {
    const bool changed = context.SceneID != _Context.SceneID || context.ObjectID != _Context.ObjectID;
    _Context = context;
    if (changed) { _Force = true; ContextChanged(); }
}
void ComponentEditor::Load(GameSproutRuntime::IComponent& component) {
    _Type = &component.GetComponentType();
    QWidget* focused = QApplication::focusWidget();
    auto* text = qobject_cast<QLineEdit*>(focused);
    if (!_Force && text && isAncestorOf(text) && text->isModified()) return;
    _Loading = true;
    setEnabled(true);
    struct Guard { bool& Value; ~Guard() { Value = false; } } guard{_Loading};
    ReadValues(component);
    _Force = false;
}
void ComponentEditor::Reload() {
    auto* object = _Context.Resolve();
    if (!object || !_Type) { setEnabled(false); return; }
    for (std::size_t index = 0; index < object->GetComponentCount(); ++index) {
        auto* component = object->GetComponentAt(index);
        if (component && component->GetComponentType() == *_Type) {
            _Force = true; Load(*component); return;
        }
    }
    setEnabled(false);
}
bool ComponentEditor::ReadNumber(QLineEdit* edit, float& output) const {
    bool valid = false;
    const double value = QLocale::c().toDouble(edit->text().trimmed(), &valid);
    if (!valid || !std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max()) return false;
    output = static_cast<float>(value);
    return true;
}
void ComponentEditor::ShowNumber(QLineEdit* edit, float value) { edit->setText(QString::number(value, 'g', 9)); }
bool ComponentEditor::ReadNumbers(std::initializer_list<QLineEdit*> edits, std::vector<float>& output) const {
    output.clear();
    for (QLineEdit* edit : edits) { float value; if (!ReadNumber(edit,value)) return false; output.push_back(value); }
    return true;
}
void ComponentEditor::ShowNumbers(std::initializer_list<QLineEdit*> edits, std::initializer_list<float> values) {
    auto value = values.begin();
    for (QLineEdit* edit : edits) { if (value == values.end()) break; ShowNumber(edit,*value++); }
}
void ComponentEditor::Fail(const std::string& error) { if (_Context.Report) _Context.Report(error); Reload(); }
ComponentEditor* CreateComponentEditor(const std::type_info& type, QWidget* parent) {
    if (type == typeid(GameSproutRuntime::ITransformComponent)) return new TransformComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IRectTransformComponent)) return new RectTransformComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::ICanvasComponent)) return new CanvasComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IImageComponent)) return new ImageComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IMeshComponent)) return new MeshComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IMeshRendererComponent)) return new MeshRendererComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::ICameraComponent)) return new CameraComponentEditor(parent);
    return nullptr;
}
