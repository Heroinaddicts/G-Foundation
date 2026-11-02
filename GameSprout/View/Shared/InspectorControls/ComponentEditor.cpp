#include "ComponentEditor.h"
#include "InspectorNumberText.h"
#include <QApplication>
#include <QLineEdit>
#include <QLocale>
#include <cmath>
#include <limits>

void ComponentEditor::Bind(const ComponentEditContext& context) {
    const bool changed = context.Runtime != _Context.Runtime || context.SceneID != _Context.SceneID || context.ObjectID != _Context.ObjectID || context.ComponentID != _Context.ComponentID;
    _Context = context;
    if (changed) { _Force = true; ContextChanged(); }
}
void ComponentEditor::Load(GameSproutRuntime::IComponent& component) {
    _Type = &component.GetComponentType();
    if(_Context.ComponentID!=component.GetComponentInstanceID()) {
        _Context.ComponentID=component.GetComponentInstanceID();_Force=true;ContextChanged();
    }
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
        if (component && component->GetComponentType() == *_Type &&
            (!_Context.ComponentID||component->GetComponentInstanceID()==_Context.ComponentID)) {
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
void ComponentEditor::ShowNumber(QLineEdit* edit, float value) {
    const QString text=InspectorNumbers::Format(value);
    const bool focused=edit->hasFocus();
    const int cursor=edit->cursorPosition(),selection=edit->selectionStart(),length=edit->selectedText().size();
    edit->setText(text);
    edit->setToolTip(text);
    // setText normally scrolls a narrow field to its trailing digits. Idle and
    // read-only inspectors should show the sign/integer prefix, retaining the
    // full round-trip float text and exposing it in the tooltip.
    if(!focused) edit->setCursorPosition(0);
    else if(selection>=0) edit->setSelection(cursor==selection?selection+length:selection,cursor==selection?-length:length);
    else edit->setCursorPosition(cursor);
}
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
