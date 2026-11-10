#include "StaticFlagsSection.h"
#include "ISceneManager.h"
#include "ui_CompactStaticHeader.h"
#include "ui_StaticFlagsMenu.h"
#include "ui_StaticChildrenDialog.h"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <algorithm>

namespace {
constexpr std::array<GameSproutRuntime::StaticFlags,8> Flags{
    GameSproutRuntime::StaticFlags::ContributeGI,
    GameSproutRuntime::StaticFlags::OccluderStatic,
    GameSproutRuntime::StaticFlags::BatchingStatic,
    GameSproutRuntime::StaticFlags::NavigationStatic,
    GameSproutRuntime::StaticFlags::OccludeeStatic,
    GameSproutRuntime::StaticFlags::OffMeshLinkGeneration,
    GameSproutRuntime::StaticFlags::ReflectionProbeStatic,
    GameSproutRuntime::StaticFlags::ShadowCasterStatic};
std::uint32_t Bits(GameSproutRuntime::StaticFlags value){return static_cast<std::uint32_t>(value);}
}
StaticFlagsSection::StaticFlagsSection(const InspectorServices& services,QWidget* parent)
    :InspectorSection(services,parent),_Ui(new Ui::CompactStaticHeaderForm()),_MenuUi(new Ui::StaticFlagsMenuForm()) {
    _Ui->setupUi(this);_Menu=new QMenu(this);_MenuUi->setupUi(_Menu);_Ui->staticMenuButton->setMenu(_Menu);
    _Options={_MenuUi->contributeGIAction,_MenuUi->occluderAction,_MenuUi->batchingAction,_MenuUi->navigationAction,
        _MenuUi->occludeeAction,_MenuUi->offMeshLinkAction,_MenuUi->reflectionAction,_MenuUi->shadowCasterAction};
    _Authoring=services.Editor?services.Editor->GetGameSproutModule<IStaticAuthoring>("StaticAuthoring"):nullptr;
    _Play=services.Editor?services.Editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    if(_Play){_PlaySubscribed=_Play->RegisterPlayModeListener(this);_Editable=_Play->GetState().State==EditorPlayState::Edit;}
    QObject::connect(_Ui->staticCheck,&QCheckBox::clicked,this,[this]{ToggleAll();});
    QObject::connect(_MenuUi->nothingAction,&QAction::triggered,this,[this]{Change(GameSproutRuntime::StaticFlags::None,AvailableFlags());});
    QObject::connect(_MenuUi->everythingAction,&QAction::triggered,this,[this]{const auto available=AvailableFlags();Change(available,available);});
    for(std::size_t index=0;index<_Options.size();++index)QObject::connect(_Options[index],&QAction::triggered,this,[this,index](bool checked){
        Change(checked?Flags[index]:GameSproutRuntime::StaticFlags::None,Flags[index]);
    });
    QObject::connect(_Menu,&QMenu::aboutToShow,this,[this]{Refresh();});
    Refresh();
}
StaticFlagsSection::~StaticFlagsSection() {
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _PlaySubscribed=false;delete _Prompt.data();_Target={};
    delete _Menu;delete _MenuUi;delete _Ui;
}
GameSproutRuntime::IGameObject* StaticFlagsSection::Resolve() const {
    auto* scene=_Services.Scenes?_Services.Scenes->GetActiveScene():nullptr;
    auto* runtime=_Services.Editor?_Services.Editor->_Runtime:nullptr;
    auto* object=runtime?runtime->QueryGameObject(_Target.Scene.ObjectID):nullptr;
    return scene&&scene->GetInstanceID()==_Target.Scene.SceneID&&object&&object!=scene&&object->GetScene()==scene?object:nullptr;
}
bool StaticFlagsSection::CanAuthor() const {
    return _Editable&&_Authoring&&_Services.Scenes&&!_Services.Scenes->IsPlaySessionActive();
}
bool StaticFlagsSection::Load(const InspectorTarget& target,bool changed,std::string& error) {
    error.clear();if(changed){_Menu->hide();if(_Prompt)_Prompt->reject();}
    _Target=target;const bool applicable=Resolve()!=nullptr&&AvailableFlags()!=GameSproutRuntime::StaticFlags::None;Refresh();return applicable;
}
void StaticFlagsSection::Deactivate(){_Target={};_Menu->hide();if(_Prompt)_Prompt->reject();Refresh();}
void StaticFlagsSection::OnPlayModeStateChanged(const PlayModeState& state) {
    _Editable=state.State==EditorPlayState::Edit;
    if(!_Editable){_Menu->hide();if(_Prompt)_Prompt->reject();}
    Refresh();
}
GameSproutRuntime::StaticFlags StaticFlagsSection::AvailableFlags() const {
    if(!_Authoring)return GameSproutRuntime::StaticFlags::None;
    const auto supported=_Authoring->GetSupportedFlags();
    const auto definitions=_Authoring->GetFlagDefinitions();
    auto available=GameSproutRuntime::StaticFlags::None;
    for(const auto flag:Flags) {
        const auto found=std::find_if(definitions.begin(),definitions.end(),[flag](const auto& definition){return definition.Flag==flag;});
        if(found!=definitions.end()&&found->Supported&&(Bits(supported)&Bits(flag))!=0)available|=flag;
    }
    return available;
}
void StaticFlagsSection::Refresh() {
    _Loading=true;const QSignalBlocker checkbox(_Ui->staticCheck);
    auto* object=Resolve();const auto value=object?object->GetStaticFlags():GameSproutRuntime::StaticFlags::None;
    const auto available=AvailableFlags();const auto selected=value&available;
    const bool editable=object&&CanAuthor();
    _Ui->staticCheck->setCheckState(selected==GameSproutRuntime::StaticFlags::None?Qt::Unchecked:
        selected==available?Qt::Checked:Qt::PartiallyChecked);
    _Ui->staticCheck->setEnabled(editable&&available!=GameSproutRuntime::StaticFlags::None);
    _Ui->staticCheck->setToolTip(available==GameSproutRuntime::StaticFlags::None?
        tr("暂无可用的静态用途。"):
        tr("选择当前可用的静态用途，保留其他用途的元数据。"));
    _Ui->staticMenuButton->setEnabled(object!=nullptr);
    _MenuUi->nothingAction->setChecked(selected==GameSproutRuntime::StaticFlags::None);
    _MenuUi->nothingAction->setEnabled(editable&&available!=GameSproutRuntime::StaticFlags::None);
    _MenuUi->everythingAction->setChecked(available!=GameSproutRuntime::StaticFlags::None&&selected==available);
    _MenuUi->everythingAction->setEnabled(editable&&available!=GameSproutRuntime::StaticFlags::None);
    _MenuUi->everythingAction->setToolTip(tr("启用当前菜单中的全部可用用途。"));
    const auto definitions=_Authoring?_Authoring->GetFlagDefinitions():std::vector<StaticFlagDefinition>{};
    for(std::size_t index=0;index<_Options.size();++index) {
        auto* option=_Options[index];const auto flag=Flags[index];const QSignalBlocker blocker(option);
        const auto found=std::find_if(definitions.begin(),definitions.end(),[flag](const auto& definition){return definition.Flag==flag;});
        const bool visible=(Bits(available)&Bits(flag))!=0;
        option->setVisible(visible);option->setChecked((Bits(value)&Bits(flag))!=0);
        option->setEnabled(editable&&visible);
        option->setToolTip(found==definitions.end()?QString{}:QString::fromStdString(found->Description));
    }
    _Loading=false;
}
void StaticFlagsSection::Error(const std::string& error){if(ReportError)ReportError(error);}
void StaticFlagsSection::ToggleAll() {
    if(_Loading)return;auto* object=Resolve();if(!object||!CanAuthor()){Refresh();return;}
    const auto available=AvailableFlags();
    const auto next=(object->GetStaticFlags()&available)==available?GameSproutRuntime::StaticFlags::None:available;
    Change(next,available);
}
void StaticFlagsSection::Change(GameSproutRuntime::StaticFlags values,GameSproutRuntime::StaticFlags changedFlags) {
    if(_Loading)return;
    auto* object=Resolve();if(!object||!CanAuthor()){Refresh();return;}
    changedFlags&=AvailableFlags();
    if(changedFlags==GameSproutRuntime::StaticFlags::None){Refresh();return;}
    const auto target=_Target.Scene;bool children=false;
    if(object->GetChildCount()) {
        QPointer<StaticFlagsSection> self(this);auto* prompt=new QDialog(this);_Prompt=prompt;
        Ui::StaticChildrenDialogForm form;form.setupUi(prompt);
        QObject::connect(form.selfButton,&QPushButton::clicked,prompt,[prompt]{prompt->done(1);});
        QObject::connect(form.childrenButton,&QPushButton::clicked,prompt,[prompt]{prompt->done(2);});
        QObject::connect(form.cancelButton,&QPushButton::clicked,prompt,&QDialog::reject);
        const int choice=prompt->exec();if(!self)return;delete _Prompt.data();_Prompt=nullptr;
        if(choice!=1&&choice!=2){Refresh();return;}children=choice==2;
    }
    if(_Target.Scene!=target||!Resolve()||!CanAuthor()){Refresh();return;}
    // A capability can disappear while the child-choice dialog is open.
    changedFlags&=AvailableFlags();
    if(changedFlags==GameSproutRuntime::StaticFlags::None){Refresh();return;}
    std::string error;
    if(!_Authoring->SetFlags(target.ObjectID,values&changedFlags,changedFlags,children,error))Error(error);
    if(RequestRefresh)RequestRefresh();else Refresh();
}
