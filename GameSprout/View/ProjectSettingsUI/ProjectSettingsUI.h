#pragma once
#include "Header.h"
#include "IProjectSettings.h"
#include "IPlayMode.h"
#include <QPointer>
class IIDEWindow;
class IIDEMenu;
class QDialog;
class QObject;
class ProjectTagsModel;
class ProjectLayersModel;
class ProjectCollisionModel;
namespace Ui {class ProjectSettingsForm;}

class ProjectSettingsUI final : public IProjectSettingsUI,public IProjectSettingsListener,public IPlayModeListener {
public:
    ~ProjectSettingsUI() override;
    void Show() override;
    void Hide() override;
    void OnProjectSettingsChanged(const GameSproutRuntime::RuntimeProjectSettings&,std::uint64_t) override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    static void ShowMenu();
    void ReadSnapshot();
    void RefreshModels();
    void SetDirty();
    void UpdateEnabled();
    void AddTag();
    void RemoveTag();
    void Apply();
    void Error(const std::string&);
    IProjectSettings* _Settings=nullptr;
    IPlayMode* _Play=nullptr;
    IIDEWindow* _IDE=nullptr;
    IIDEMenu* _Menu=nullptr;
    QPointer<QDialog> _Window;
    Ui::ProjectSettingsForm* _Ui=nullptr;
    ProjectTagsModel* _Tags=nullptr;
    ProjectLayersModel* _Layers=nullptr;
    ProjectCollisionModel* _Collision=nullptr;
    GameSproutRuntime::RuntimeProjectSettings _Draft;
    std::uint64_t _Revision=0;
    bool _SettingsSubscribed=false,_PlaySubscribed=false,_Editable=true,_Dirty=false,_Saving=false;
};
