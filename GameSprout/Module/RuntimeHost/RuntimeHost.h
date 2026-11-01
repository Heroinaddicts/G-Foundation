#pragma once
#include "IRuntimeHost.h"
#include <QElapsedTimer>
#include <map>
class IRuntimeAssets;

class RuntimeHost final : public IRuntimeHost {
public:
    ~RuntimeHost() override;
    void SetSimulationPlaying(bool playing) override;
    bool IsSimulationPlaying() const override {return _Playing;}
    void ResetClock() override;
    bool RegisterFrameListener(IRuntimeFrameListener*) override;
    void UnregisterFrameListener(IRuntimeFrameListener*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Update(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    void NotifyFrame(const RuntimeFrameEvent&);
    void NotifyPlayingChanged();
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    IRuntimeAssets* _Resources=nullptr;
    QElapsedTimer _Timer;
    bool _Playing=false;
    std::map<uint64_t,IRuntimeFrameListener*> _Listeners;
    uint64_t _NextListener=0;
    uint64_t _FrameNumber=0;
};
