#pragma once
#include "IScriptEditor.h"
#include "ComponentEditContext.h"
#include <QWidget>
#include <functional>
namespace Ui{class ScriptPropertyRowForm;}
class ScriptPropertyRow final:public QWidget {
public:
    ScriptPropertyRow(const ScriptFieldSnapshot&,const ScriptFieldTarget&,const ComponentEditContext&,QWidget*);
    ~ScriptPropertyRow() override;
    std::function<void(const GameSproutRuntime::ScriptValue&)> Changed;
    std::function<void(const std::string&)> Invalid;
private:
    void Commit();
    Ui::ScriptPropertyRowForm* _Ui;
    GameSproutRuntime::ScriptFieldInfo _Field;
};
