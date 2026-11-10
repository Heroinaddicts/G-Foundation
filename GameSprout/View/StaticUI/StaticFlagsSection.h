#pragma once
#include "InspectorPanel.h"
#include "IStaticAuthoring.h"
#include "IPlayMode.h"
#include <array>
#include <QPointer>
class QAction;
class QDialog;
class QMenu;
namespace Ui {class CompactStaticHeaderForm;class StaticFlagsMenuForm;}

// StaticUI owns this control. The Inspector supplies its locked target identity.
class StaticFlagsSection final : public InspectorSection,public IPlayModeListener {
public:
    StaticFlagsSection(const InspectorServices&,QWidget*);
    ~StaticFlagsSection() override;
    bool Load(const InspectorTarget&,bool,std::string&) override;
    void Deactivate() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
private:
    GameSproutRuntime::IGameObject* Resolve() const;
    bool CanAuthor() const;
    GameSproutRuntime::StaticFlags AvailableFlags() const;
    void Refresh();
    void ToggleAll();
    void Change(GameSproutRuntime::StaticFlags values,GameSproutRuntime::StaticFlags changedFlags);
    void Error(const std::string&);
    Ui::CompactStaticHeaderForm* _Ui=nullptr;
    Ui::StaticFlagsMenuForm* _MenuUi=nullptr;
    QMenu* _Menu=nullptr;
    std::array<QAction*,8> _Options{};
    IStaticAuthoring* _Authoring=nullptr;
    IPlayMode* _Play=nullptr;
    InspectorTarget _Target;
    QPointer<QDialog> _Prompt;
    bool _PlaySubscribed=false,_Editable=true,_Loading=false;
};
