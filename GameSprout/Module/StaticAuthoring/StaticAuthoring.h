#pragma once
#include "Header.h"
class ISceneManager;
class IPlayMode;
class StaticAuthoring final : public IStaticAuthoring {
public:
    ~StaticAuthoring() override;
    std::vector<StaticFlagDefinition> GetFlagDefinitions() const override;
    GameSproutRuntime::StaticFlags GetSupportedFlags() const override;
    bool SetFlags(std::uint64_t,GameSproutRuntime::StaticFlags,GameSproutRuntime::StaticFlags,bool,std::string&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override { return _Scenes && _Runtime; }
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    ISceneManager* _Scenes=nullptr;
    IPlayMode* _Play=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    bool _Busy=false;
};
