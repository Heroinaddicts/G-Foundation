#include "InspectorUI.h"
#include "AssetInfoInspector.h"
#include "ui_InspectorUI.h"
#include <QApplication>
#include <QMetaObject>
#include <algorithm>
#include <utility>
InspectorUI::~InspectorUI() { Release(nullptr); }
void InspectorUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self = this; g_GameSproutEditor = editor; _Services.Editor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GetGameSproutModule(editor, _Services.Assets, AssetsManager);
    GetGameSproutModule(editor, _Services.Selection, SelectionManager);
    GetGameSproutModule(editor, _Services.Library, LibraryManager);
    GetGameSproutModule(editor, _Services.Scenes, SceneManager);
    _Runtime = editor ? editor->_Runtime : nullptr;
    _Editing=editor?editor->GetGameSproutModule<IObjectEditing>("ObjectEditing"):nullptr;
}
bool InspectorUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if (_Window) return true;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance()) || !_Services.Assets || !_Services.Selection || !_Services.Library) return false;
    try {
        _Dispatch = new QObject(); _Window = new QWidget(); _Ui = new Ui::InspectorUIWindow(); _Ui->setupUi(_Window);
        _Info = new AssetInfoInspector(_Ui->infoHost); _Ui->infoHostLayout->addWidget(_Info);
        QObject::connect(_Window, &QObject::destroyed, _Dispatch, [this] {
            _Window = nullptr; _Info = nullptr; _Docker = nullptr; _Attached = false;
            _ActivePanel = nullptr; _ActiveExtension = nullptr; _Panels.clear();
            _SceneSections.clear();
            _NormalModeMenu = nullptr; _DebugModeMenu = nullptr;
        });
        QObject::connect(_Ui->applyButton, &QPushButton::clicked, _Dispatch, [this] { ApplyChanges(); });
        QObject::connect(_Ui->revertButton, &QPushButton::clicked, _Dispatch, [this] { RevertChanges(); });
        QObject::connect(_Ui->lockButton, &QToolButton::toggled, _Dispatch, [this](bool locked) {
            if (!locked) _LockedSceneMissing = false;
            if (_ActivePanel) _ActivePanel->SetLocked(locked);
            RequestRefresh();
        });
        Clear(QStringLiteral("选择一个资源或场景节点以查看属性。"));
    } catch (...) { Release(nullptr); throw; }
    return true;
}
bool InspectorUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if (!_Window || !g_IIDEWindow) return false;
    _Docker = g_IIDEWindow->CreateDocker("Inspector");
    if (!_Docker || !_Docker->AttachWidget(_Window)) return false;
    _Attached = true; _Docker->SetTitle("Inspector");
    if (auto* mode = _Docker->CreateChildMenu(_Ui->inspectorModeAction->text())) {
        _NormalModeMenu = mode->CreateChildMenu(_Ui->normalInspectorAction->text());
        _DebugModeMenu = mode->CreateChildMenu(_Ui->debugInspectorAction->text());
        if (_NormalModeMenu) _NormalModeMenu->SetClickCallback([] { if (g_Self) g_Self->SetDebugMode(false); });
        if (_DebugModeMenu) _DebugModeMenu->SetClickCallback([] { if (g_Self) g_Self->SetDebugMode(true); });
    }
    const bool assets = _Services.Assets->RegisterAssetListener(this);
    const bool selection = _Services.Selection->RegisterSelectionListener(this);
    _Subscribed = assets && selection;
    if (!assets || !selection) {
        if (assets) _Services.Assets->UnregisterAssetListener(this);
        if (selection) _Services.Selection->UnregisterSelectionListener(this);
        return false;
    }
    _SceneSubscribed = _Services.Scenes && _Services.Scenes->RegisterSceneListener(this);
    _RuntimeSubscribed = _Runtime && _Runtime->RegisterListener(this);
    _EditingSubscribed=_Editing&&_Editing->RegisterEditingListener(this);
    return (!_Services.Scenes || _SceneSubscribed) && (!_Runtime || _RuntimeSubscribed);
}
void InspectorUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*) {
    _Launched = true;
    _FullRefreshRequested=true;
    RefreshPending();
}
void InspectorUI::RequestRefresh() {
    _FullRefreshRequested = true;
    QueueRefresh();
}
void InspectorUI::QueueRefresh() {
    if (!_Launched || !_Dispatch || !_Window || _RefreshQueued) return;
    _RefreshQueued = true;
    QMetaObject::invokeMethod(_Dispatch, [this] {
        _RefreshQueued = false;
        if (_ResolvingSelection) { _RefreshDeferred = true; return; }
        if (_Window) RefreshPending();
    }, Qt::QueuedConnection);
}
void InspectorUI::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    using GameSproutRuntime::SceneNodeEventType;
    if(!_Target.Scene.ObjectID||event.SceneID!=_Target.Scene.SceneID)return;
    if(event.Type==SceneNodeEventType::MainCameraChanged) {
        _ObjectChanges.ComponentTypes.insert("GameSproutRuntime.Camera");
    } else if(event.ObjectID==_Target.Scene.ObjectID) {
        switch(event.Type) {
        case SceneNodeEventType::ComponentChanged:
            if(event.ComponentType.empty()) {RequestRefresh();return;}
            _ObjectChanges.ComponentTypes.insert(event.ComponentType);
            break;
        case SceneNodeEventType::Rename:
        case SceneNodeEventType::ActiveChanged:
        case SceneNodeEventType::TagChanged:
        case SceneNodeEventType::LayerChanged:
        case SceneNodeEventType::StaticFlagsChanged:
            _ObjectChanges.Header=true;
            break;
        default: RequestRefresh();return;
        }
    } else {
        const bool pathChanged=event.Type==SceneNodeEventType::Rename||event.Type==SceneNodeEventType::Reparent;
        const bool worldChanged=_DebugMode&&event.Type==SceneNodeEventType::ComponentChanged&&
            event.ComponentType=="GameSproutRuntime.Transform";
        if(!pathChanged&&!worldChanged)return;
        auto* selected=_Runtime?_Runtime->QueryGameObject(_Target.Scene.ObjectID):nullptr;
        auto* ancestor=selected?selected->Parent():nullptr;
        while(ancestor&&ancestor->GetInstanceID()!=event.ObjectID)ancestor=ancestor->Parent();
        if(!ancestor)return;
        if(pathChanged)_ObjectChanges.Header=true;
        if(worldChanged||event.Type==SceneNodeEventType::Reparent)
            _ObjectChanges.ComponentTypes.insert("GameSproutRuntime.Transform");
    }
    QueueRefresh();
}
void InspectorUI::OnAssetChanged(const AssetEvent& event) {
    if(_Target.Assets.empty())return;
    const auto& guid=event.GuidValue.GetGuidString();
    if(!_ActivePanel||_ActivePanel->TracksAsset(guid)||
       std::any_of(_Target.Assets.begin(),_Target.Assets.end(),[&](const auto& asset){return asset.GuidString==guid;}))
        RequestRefresh();
}
void InspectorUI::OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) {
    if (_ActivePanel) _ActivePanel->OnAnimationEvent(event);
}
bool InspectorUI::RegisterExtension(IInspectorExtension* extension) {
    if (!extension || std::find(_Extensions.begin(), _Extensions.end(), extension) != _Extensions.end()) return false;
    _Extensions.push_back(extension);
    std::stable_sort(_Extensions.begin(), _Extensions.end(), [](auto* left, auto* right) { return left->Priority() > right->Priority(); });
    RequestRefresh(); return true;
}
void InspectorUI::UnregisterExtension(IInspectorExtension* extension) {
    const auto found = std::find(_Extensions.begin(), _Extensions.end(), extension);
    if (found == _Extensions.end()) return;
    _Extensions.erase(found);
    _SectionsToRefresh.erase(extension);
    const auto section=_SceneSections.find(extension);
    if(section!=_SceneSections.end()) {
        if(section->second.Section){section->second.Section->Deactivate();delete section->second.Section.data();}
        _SceneSections.erase(section);
    }
    // Remaining node pages can contain component editors created by this extension.
    for (auto& entry : _Panels) entry.second->Deactivate();
    const auto panel = _Panels.find(extension);
    if (panel != _Panels.end()) {
        if (_ActivePanel == panel->second) { _ActivePanel = nullptr; _ActiveExtension = nullptr; }
        delete panel->second; _Panels.erase(panel);
    }
    RequestRefresh();
}
ComponentEditor* InspectorUI::CreateComponentEditor(const std::type_info& type, QWidget* parent) {
    for (auto* extension : _Extensions) if (auto* editor = extension->CreateComponentEditor(type, parent)) return editor;
    return nullptr;
}
void InspectorUI::Show() { if (_Window) _Window->show(); if (_Docker) _Docker->Activate(); }
void InspectorUI::Hide() { if (_Window) _Window->hide(); }
void InspectorUI::Release(GameSproutEditor::GameSproutViewApi*) {
    _Launched = false;
    if (_NormalModeMenu) _NormalModeMenu->SetClickCallback(nullptr);
    if (_DebugModeMenu) _DebugModeMenu->SetClickCallback(nullptr);
    ObserveObject(0);
    if(_EditingSubscribed&&_Editing)_Editing->UnregisterEditingListener(this);
    _EditingSubscribed=false;_Editing=nullptr;
    if (_RuntimeSubscribed && _Runtime) _Runtime->UnregisterListener(this);
    if (_SceneSubscribed && _Services.Scenes) _Services.Scenes->UnregisterSceneListener(this);
    if (_Subscribed) { _Services.Assets->UnregisterAssetListener(this); _Services.Selection->UnregisterSelectionListener(this); }
    _Subscribed = false; _RuntimeSubscribed = false; _SceneSubscribed = false;
    for(auto& entry:_SceneSections)if(auto* section=entry.second.Section.data()) {
        section->ReportError={};section->RequestSelection={};section->RequestRefresh={};
        section->Deactivate();delete section;
    }
    _SceneSections.clear();
    for (auto& entry : _Panels) {
        entry.second->Changed = {}; entry.second->RequestSelection = {}; entry.second->RequestLock = {};
        entry.second->ReportError = {};
        entry.second->ResolvePendingChanges = {}; entry.second->Deactivate(); delete entry.second;
    }
    _Panels.clear(); _Extensions.clear(); _ActivePanel = nullptr; _ActiveExtension = nullptr;
    delete _Dispatch; _Dispatch = nullptr;
    if (!_Attached) delete _Window;
    _Window = nullptr; _Docker = nullptr; _Info = nullptr; _Attached = false;
    _NormalModeMenu = nullptr; _DebugModeMenu = nullptr;
    delete _Ui; _Ui = nullptr;
    _Services = {}; _Runtime = nullptr; _Target = {}; _RefreshQueued = false; _RefreshDeferred = false;
    _FullRefreshRequested=false;_SectionsToRefresh.clear();_ObservedPropertiesPending=false;_ObjectChanges={};
    _LockedSceneMissing = false; _OperationError.clear();
    if (g_Self == this) g_Self = nullptr;
}

void InspectorUI::ObserveObject(std::uint64_t objectID) {
    if(_ObservedObjectID==objectID)return;
    if(_Runtime&&_ObservedObjectID)_Runtime->UnregisterObjectPropertyObserver(_ObservedObjectID,this);
    _ObservedObjectID=0;
    if(_Runtime&&objectID&&_Runtime->RegisterObjectPropertyObserver(objectID,this))_ObservedObjectID=objectID;
}
void InspectorUI::OnRuntimeObjectPropertiesChanged(std::uint64_t sceneID,std::uint64_t objectID) {
    if(_Target.Scene.SceneID==sceneID&&_Target.Scene.ObjectID==objectID) {
        _ObservedPropertiesPending=true;
        QueueRefresh();
    }
}
void InspectorUI::OnObjectPropertiesChanged(const ObjectEditEvent& event) {
    if(_Target.Scene.SceneID!=event.Target.SceneID||_Target.Scene.ObjectID!=event.Target.ObjectID)return;
    if(event.Target.ComponentID)_ObjectChanges.ComponentIDs.insert(event.Target.ComponentID);
    else _ObservedPropertiesPending=true;
    QueueRefresh();
}
