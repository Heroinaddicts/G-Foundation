#pragma once
#include "IRuntimeHost.h"
#include <QElapsedTimer>
class IRuntimeAssets;

class RuntimeHost final : public IRuntimeHost {
public:
    ~RuntimeHost() override;
    void SetSimulationPlaying(bool playing) override;
    bool IsSimulationPlaying() const override {return _Playing;}
    void ResetClock() override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Update(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    IRuntimeAssets* _Resources=nullptr;
    QElapsedTimer _Timer;
    bool _Playing=false;
};
