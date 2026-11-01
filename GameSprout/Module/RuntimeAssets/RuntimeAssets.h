#pragma once
#include "IRuntimeAssets.h"
class IAssetsManager;
class ILibraryManager;
class LibraryRuntimeResources;
class RuntimeAssets final : public IRuntimeAssets {
public:
    ~RuntimeAssets() override;
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider() const override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return _Resources!=nullptr;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    LibraryRuntimeResources* _Resources=nullptr;
};
