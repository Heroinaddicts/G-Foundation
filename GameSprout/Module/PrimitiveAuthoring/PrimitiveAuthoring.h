#pragma once
#include "Header.h"
class ISceneManager;
class ISelectionManager;
class IPlayMode;
class IRuntimeHost;
class QThread;
class PrimitiveAuthoring final : public IPrimitiveAuthoring {
public:
    ~PrimitiveAuthoring() override;
    bool CanCreate(std::uint64_t,std::uint64_t,std::string&) const override;
    bool CreatePrimitive(std::uint64_t,std::uint64_t,GameSproutRuntime::PrimitiveType,
        std::uint64_t&,std::string&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return _Ready;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameObject* Parent(std::uint64_t) const;
    ISceneManager* _Scenes=nullptr;
    ISelectionManager* _Selection=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
    bool _Ready=false;
};
