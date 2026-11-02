#include "SceneObjectPanel.h"
#include "GameObjectInspector.h"
#include "IInspectorUI.h"
#include "IRuntimeHost.h"
#include "IScriptEditor.h"
#include "ui_SceneObjectPanel.h"
#include <QStringList>
SceneObjectPanel::SceneObjectPanel(const InspectorServices& services, QWidget* parent)
    : InspectorPanel(services, parent), _Ui(new Ui::SceneObjectPanelForm) {
    _Ui->setupUi(this);
    _PlayMode=services.Editor?services.Editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    if(_PlayMode){_PlaySubscribed=_PlayMode->RegisterPlayModeListener(this);OnPlayModeStateChanged(_PlayMode->GetState());}
    _Ui->objectInspector->LockChanged = [this](bool locked) { if (RequestLock) RequestLock(locked); };
}
SceneObjectPanel::~SceneObjectPanel() { if(_PlaySubscribed&&_PlayMode)_PlayMode->UnregisterPlayModeListener(this); _Ui->objectInspector->LockChanged = {}; delete _Ui; }
bool SceneObjectPanel::Load(const InspectorTarget& target, bool, std::string& error) {
    error.clear(); _Target = target;
    ComponentEditContext context;
    context.Scenes = _Services.Scenes;
    context.Runtime = _Services.Editor ? _Services.Editor->_Runtime : nullptr;
    context.Preview = _Services.Editor ? _Services.Editor->GetGameSproutModule<IAnimationPreview>("AnimationPreview") : nullptr;
    context.Inspector = _Services.Editor ? _Services.Editor->GetGameSproutView<IInspectorUI>("InspectorUI") : nullptr;
    context.Host = _Services.Editor ? _Services.Editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost") : nullptr;
    context.Scripts = _Services.Editor ? _Services.Editor->GetGameSproutModule<IScriptEditor>("ScriptEditor") : nullptr;
    context.Assets = _Services.Assets; context.Library = _Services.Library; context.Selection = _Services.Selection;
    context.SceneID = target.Scene.SceneID; context.ObjectID = target.Scene.ObjectID;
    context.Report = [this](const std::string& message) { if (ReportError) ReportError(message); };
    auto* object = context.Resolve();
    if (!object) { error = "节点已删除或场景已切换。"; Deactivate(); return false; }
    _Ui->objectInspector->Bind(context); _Ui->objectInspector->Load(*object);_Ui->objectInspector->SetReadOnly(_ReadOnly);
    return true;
}
InspectorPresentation SceneObjectPanel::Describe() const {
    InspectorPresentation description; description.SceneObject = true;
    auto* runtime = _Services.Editor ? _Services.Editor->_Runtime : nullptr;
    auto* object = runtime ? runtime->QueryGameObject(_Target.Scene.ObjectID) : nullptr;
    if (!object) return description;
    description.Name = QString::fromStdString(object->GetName());
    description.Type = object->GetScene() == object ? QStringLiteral("Scene") : QStringLiteral("GameObject");
    description.Guid = QString::number(_Target.Scene.SceneID); description.LocalID = QString::number(_Target.Scene.ObjectID);
    QStringList path; for (auto* current = object; current; current = current->Parent()) path.prepend(QString::fromStdString(current->GetName()));
    description.Path = path.join(" / "); return description;
}
void SceneObjectPanel::Deactivate() { _Ui->objectInspector->Clear(); _Target = {}; }
void SceneObjectPanel::SetDebugMode(bool debug) { _Ui->objectInspector->SetDebugMode(debug); }
void SceneObjectPanel::SetLocked(bool locked) { _Ui->objectInspector->SetLocked(locked); }
void SceneObjectPanel::OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) { _Ui->objectInspector->OnAnimationEvent(event); }

void SceneObjectPanel::OnPlayModeStateChanged(const PlayModeState& state) {
    _ReadOnly=state.State!=EditorPlayState::Edit;
    if(_Ui)_Ui->objectInspector->SetReadOnly(_ReadOnly);
}
