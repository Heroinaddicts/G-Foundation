#pragma once
#include "IScriptEditor.h"
#include <QWidget>
#include <functional>

class QDialog;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QResizeEvent;
namespace Ui { class SceneObjectFieldForm; class SceneObjectPickerForm; }

// Feature-owned presentation of a serialized scene reference. All resolution,
// validation, selection and edits go through ScriptEditor value interfaces.
class SceneObjectField final : public QWidget {
public:
    SceneObjectField(const ScriptFieldSnapshot&,const ScriptFieldTarget&,IScriptEditor*,QWidget* parent=nullptr);
    ~SceneObjectField() override;
    std::function<void(const GameSproutRuntime::ScriptValue&)> Changed;
    std::function<void(const std::string&)> Invalid;
private:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool Decode(const QMimeData*,GameSproutRuntime::ScriptValue&,std::string&) const;
    void OpenPicker();
    void UpdateName();
    void Fail(const std::string&);
    Ui::SceneObjectFieldForm* _Ui;
    Ui::SceneObjectPickerForm* _PickerUi=nullptr;
    QDialog* _Picker=nullptr;
    IScriptEditor* _Scripts=nullptr;
    ScriptFieldTarget _Target;
    GameSproutRuntime::ScriptValue _Value;
    QString _DisplayName;
};
