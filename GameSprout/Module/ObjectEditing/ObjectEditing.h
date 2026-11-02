#pragma once
#include "Header.h"
#include <map>
class IPlayMode;
class IRuntimeHost;
class QThread;

class ObjectEditing final : public IObjectEditing {
public:
    ~ObjectEditing() override;
    bool CanEdit(const ObjectEditTarget&,std::string&) const override;
    bool EditObject(const ObjectEditTarget&,const std::string&,
        const ISceneManager::fSceneEditOperation&,const ISceneManager::fSceneEditOperation&,std::string&) override;
    bool SetLayer(const ObjectEditTarget&,std::uint8_t,bool,std::string&) override;
    bool RegisterEditingListener(IObjectEditingListener*) override;
    void UnregisterEditingListener(IObjectEditingListener*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return _Ready;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameObject* Resolve(const ObjectEditTarget&) const;
    bool TransientEdit(const ObjectEditTarget&,const ISceneManager::fSceneEditOperation&,
        const ISceneManager::fSceneEditOperation&,std::string&);
    bool OwnerThread() const;
    void Notify(const ObjectEditTarget&,bool);
    ISceneManager* _Scenes=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
    bool _Ready=false,_Busy=false;
    std::uint64_t _NextListener=0;
    std::map<std::uint64_t,IObjectEditingListener*> _Listeners;
};
