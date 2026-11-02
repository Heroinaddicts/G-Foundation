#pragma once
#include "IScriptEditor.h"
#include "ComponentEditContext.h"
#include <QWidget>
#include <functional>
class SceneObjectField;class ScriptCompositeField;class QLineEdit;
namespace Ui{class ScriptPropertyRowForm;}
class ScriptPropertyRow final:public QWidget {
    Q_OBJECT
public:
    ScriptPropertyRow(const ScriptFieldSnapshot&,const ScriptFieldTarget&,const ComponentEditContext&,QWidget*,const ScriptFieldPath& path={},const std::string& rootID={});
    ~ScriptPropertyRow() override;
    const GameSproutRuntime::ScriptFieldInfo& Definition() const{return _Field;}
    static bool SameDefinition(const GameSproutRuntime::ScriptFieldInfo&,const GameSproutRuntime::ScriptFieldInfo&);
    void Load(const ScriptFieldSnapshot&);
    void AttachItemControls(QWidget*);
    void SaveExpansion(std::map<std::string,bool>&) const;
    void RestoreExpansion(const std::map<std::string,bool>&);
    std::function<void(const GameSproutRuntime::ScriptValue&)> Changed;
    std::function<void(const ScriptFieldPath&,const GameSproutRuntime::ScriptValue&)> PathChanged;
    std::function<void(const ScriptFieldPath&,const ScriptContainerEdit&)> ContainerChanged;
    std::function<void(const std::string&)> Invalid;
private:
    void Commit(QLineEdit* changed=nullptr);
    void Dispatch(const GameSproutRuntime::ScriptValue&);
    bool Latest(GameSproutRuntime::ScriptValue&,std::string&) const;
    Ui::ScriptPropertyRowForm* _Ui;
    GameSproutRuntime::ScriptFieldInfo _Field;
    GameSproutRuntime::ScriptValue _Value;
    ScriptFieldTarget _Target;
    ComponentEditContext _Context;
    ScriptFieldPath _Path;
    std::string _RootField;
    SceneObjectField* _ObjectField=nullptr;
    ScriptCompositeField* _Composite=nullptr;
};
