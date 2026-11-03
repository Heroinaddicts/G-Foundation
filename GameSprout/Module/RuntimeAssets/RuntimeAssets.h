#pragma once
#include "IRuntimeAssets.h"
#include <map>
class IAssetsManager;
class ILibraryManager;
class LibraryRuntimeResources;
class RuntimeAssets final : public IRuntimeAssets {
public:
    ~RuntimeAssets() override;
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider() const override;
    bool RegisterResourceListener(IRuntimeAssetsListener*) override;
    void UnregisterResourceListener(IRuntimeAssetsListener*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return _Resources!=nullptr;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    LibraryRuntimeResources* _Resources=nullptr;
    void NotifyResourcesChanged();
    std::map<std::uint64_t,IRuntimeAssetsListener*> _Listeners;
    std::uint64_t _NextListener=0;
};
