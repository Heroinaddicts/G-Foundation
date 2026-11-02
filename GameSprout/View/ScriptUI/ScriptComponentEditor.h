#pragma once
#include "ComponentEditor.h"
#include "IScriptEditor.h"
namespace Ui {class ScriptComponentEditorForm;}
class ScriptPropertyRow;class ICodeEditor;
class ScriptComponentEditor final:public ComponentEditor,public IScriptEditorListener {
public:
    explicit ScriptComponentEditor(QWidget* parent,ICodeEditor* codeEditor=nullptr);
    ~ScriptComponentEditor()override;
    void OnScriptTypesChanged(const std::vector<EditorScriptType>&)override;
    void OnScriptBuildStateChanged(const ScriptBuildState&)override;
    void OnScriptFieldsChanged(const ScriptFieldTarget&)override;
private:
    bool eventFilter(QObject*,QEvent*)override;
    void OpenSource();
    void ContextChanged()override;
    void ReadValues(GameSproutRuntime::IComponent&)override;
    GameSproutRuntime::IScriptComponent* ResolveScript()const;
    void Refresh();void RefreshBuildState();
    void RefreshFields();
    void QueueRefresh();
    void CommitField(const std::string&,const GameSproutRuntime::ScriptValue&);
    void CommitEnabled(bool);
    void Remove();void ShowError(const std::string&);
    Ui::ScriptComponentEditorForm* _Ui;
    IScriptEditor* _Scripts=nullptr;
    ICodeEditor* _CodeEditor=nullptr; // Module outlives feature pages; never a ScriptUI callback.
    std::uint64_t _InstanceID=0;
    std::vector<ScriptPropertyRow*> _Rows;
    ScriptFieldTarget _FieldTarget;
    std::string _FieldError;
    bool _RefreshQueued=false;
};
