#pragma once
#include "Header.h"
#include "IPlayMode.h"
#include "ISelectionManager.h"
#include <map>
class IPlayMode;
class IRuntimeHost;
class QThread;
class QObject;

class ObjectEditing final : public IObjectEditing,public IPlayModeListener,public ISceneListener,public ISelectionListener {
public:
    ~ObjectEditing() override;
    bool CanEdit(const ObjectEditTarget&,std::string&) const override;
    bool EditObject(const ObjectEditTarget&,const std::string&,
        const ISceneManager::fSceneEditOperation&,const ISceneManager::fSceneEditOperation&,std::string&) override;
    bool BeginContinuousEdit(const ObjectEditTarget&,const std::string&,const ISceneManager::fSceneEditOperation&,std::string&) override;
    bool PreviewContinuousEdit(const ISceneManager::fSceneEditOperation&,const ISceneManager::fSceneEditOperation&,std::string&) override;
    bool CommitContinuousEdit(const ISceneManager::fSceneEditOperation&,std::string&) override;
    bool CancelContinuousEdit(std::string&) override;
    bool HasContinuousEdit() const override {return _Continuous.Active;}
    bool IsContinuousEditFor(const ObjectEditTarget& target) const override {
        return _Continuous.Active&&_Continuous.Target==target;
    }
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>&) override;
    void OnSceneObjectSelectionChanged(const SceneObjectSelection&) override;
    bool SetLayer(const ObjectEditTarget&,std::uint8_t,bool,std::string&) override;
    bool RegisterEditingListener(IObjectEditingListener*) override;
    void UnregisterEditingListener(IObjectEditingListener*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameObject* Resolve(const ObjectEditTarget&) const;
    bool TransientEdit(const ObjectEditTarget&,const ISceneManager::fSceneEditOperation&,
        const ISceneManager::fSceneEditOperation&,std::string&);
    bool OwnerThread() const;
    void Notify(const ObjectEditTarget&,bool);
    bool ContinuousReady(std::string&) const;
    struct ContinuousEdit {
        bool Active=false,Playing=false;
        ObjectEditTarget Target;
        std::string Label;
        ISceneManager::fSceneEditOperation Restore;
    } _Continuous;
    bool _PlaySubscribed=false,_SceneSubscribed=false;
    ISelectionManager* _Selection=nullptr;
    bool _SelectionSubscribed=false;
    QObject* _Dispatch=nullptr;
    bool _CancelQueued=false,_CancelRetry=false;
    ISceneManager* _Scenes=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
    bool _Ready=false,_Busy=false;
    std::uint64_t _NextListener=0;
    std::map<std::uint64_t,IObjectEditingListener*> _Listeners;
};
