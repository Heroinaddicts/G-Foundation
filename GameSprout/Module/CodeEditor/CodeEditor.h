#pragma once
#include "ICodeEditor.h"
#include "IAssetsManager.h"
#include "IScriptCompiler.h"
#include "ITaskApi.h"
#include <QObject>
#include <map>

class IProject;
class IConsole;
class IIDEWindow;
class CodeEditorBackend;

class CodeEditor final : public ICodeEditor, public IAssetListener {
public:
    ~CodeEditor() override;
    bool OpenSource(const std::string&,std::string&) override;
    CodeEditorState GetState() const override {return _State;}
    bool RegisterCodeEditorListener(ICodeEditorListener*) override;
    void UnregisterCodeEditorListener(ICodeEditorListener*) override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void LaunchFinished(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool ValidateSource(const std::string&,QString& absolute,std::string& error) const;
    void SourceEvent(const AssetEvent&,bool removed);
    void Queue();
    void Submit();
    void Publish(bool preparing,const std::string& projectPath,const std::string& error);
    IProject* _Project=nullptr;
    IAssetsManager* _Assets=nullptr;
    IScriptCompiler* _Compiler=nullptr;
    IConsole* _Console=nullptr;
    IIDEWindow* _IDE=nullptr;
    Foundation::ITaskApi* _Tasks=nullptr;
    std::function<CodeEditorBackend*()> _CreateBackend;
    QObject* _Dispatch=nullptr;
    UInt64 _Owner=Foundation::InvalidTaskOwnerId;
    std::map<std::string,std::string> _Sources;
    uint64_t _Revision=0,_OpenSequence=0,_NextListener=0;
    std::map<uint64_t,ICodeEditorListener*> _Listeners;
    CodeEditorState _State;
    std::string _PendingOpen;
    bool _Launched=false,_Subscribed=false,_Queued=false,_Running=false;
    bool _HasProject=false,_NeedsSync=false,_Loading=false;
};
