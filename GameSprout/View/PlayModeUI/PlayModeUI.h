#pragma once
#include "Header.h"
#include "IPlayMode.h"
#include "ISceneManager.h"
namespace Ui { class PlayModeUIWindow; }
class QWidget;
class QObject;

class PlayModeUI final : public IPlayModeUI, public IPlayModeListener,public ISceneListener {
public:
    ~PlayModeUI() override { Release(nullptr); }
    void Show() override;
    void Hide() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void OnActiveSceneChanged() override {OnPlayModeStateChanged(_State);}
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void ReportCommand(bool accepted, const std::string& error);
    IPlayMode* _Play = nullptr;
    ISceneManager* _Scenes=nullptr;
    Ui::PlayModeUIWindow* _Ui = nullptr;
    QWidget* _Window = nullptr;
    QObject* _Dispatch = nullptr;
    PlayModeState _State;
    bool _Subscribed = false;
    bool _SceneSubscribed=false;
    bool _Attached = false;
};
