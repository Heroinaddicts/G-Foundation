#pragma once
#include "IScriptEditor.h"
#include "ComponentEditContext.h"
#include <QWidget>
#include <functional>
class ScriptPropertyRow;
namespace Ui {class ScriptCompositeFieldForm;}
class ScriptCompositeField final:public QWidget {
    Q_OBJECT
public:
    ScriptCompositeField(const ScriptFieldSnapshot&,const ScriptFieldTarget&,const ComponentEditContext&,const ScriptFieldPath&,const std::string& rootID,QWidget*);
    ~ScriptCompositeField() override;
    void Load(const ScriptFieldSnapshot&);
    void AttachItemControls(QWidget*);
    void SaveExpansion(std::map<std::string,bool>&) const;
    void RestoreExpansion(const std::map<std::string,bool>&);
    std::function<void(const ScriptFieldPath&,const GameSproutRuntime::ScriptValue&)> Changed;
    std::function<void(const ScriptFieldPath&,const ScriptContainerEdit&)> ContainerChanged;
    std::function<void(const std::string&)> Invalid;
private:
    void Rebuild(const ScriptFieldSnapshot&);
    Ui::ScriptCompositeFieldForm* _Ui;
    ScriptFieldSnapshot _Snapshot;
    ScriptFieldTarget _Target;
    ComponentEditContext _Context;
    ScriptFieldPath _Path;
    std::string _RootField;
    std::vector<ScriptPropertyRow*> _Rows;
    std::vector<QWidget*> _Hosts;
};
