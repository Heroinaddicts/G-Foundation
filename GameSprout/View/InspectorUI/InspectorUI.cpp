#include "InspectorUI.h"
#include "AssetInfoInspector.h"
#include "ui_InspectorUI.h"
#include <QApplication>
#include <QMetaObject>
#include <algorithm>
InspectorUI::~InspectorUI() { Release(nullptr); }
void InspectorUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self = this; g_GameSproutEditor = editor; _Services.Editor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GetGameSproutModule(editor, _Services.Assets, AssetsManager);
    GetGameSproutModule(editor, _Services.Selection, SelectionManager);
    GetGameSproutModule(editor, _Services.Library, LibraryManager);
    GetGameSproutModule(editor, _Services.Scenes, SceneManager);
    _Runtime = editor ? editor->_Runtime : nullptr;
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
    return (!_Services.Scenes || _SceneSubscribed) && (!_Runtime || _RuntimeSubscribed);
}
void InspectorUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*) { _Launched = true; Refresh(); }
void InspectorUI::RequestRefresh() {
    if (!_Launched || !_Dispatch || !_Window || _RefreshQueued) return;
    _RefreshQueued = true;
    QMetaObject::invokeMethod(_Dispatch, [this] {
        _RefreshQueued = false;
        if (_ResolvingSelection) { _RefreshDeferred = true; return; }
        if (_Window) Refresh();
    }, Qt::QueuedConnection);
}
void InspectorUI::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    if (_Target.Scene.SceneID && event.SceneID == _Target.Scene.SceneID) RequestRefresh();
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
    _LockedSceneMissing = false; _OperationError.clear();
    if (g_Self == this) g_Self = nullptr;
}
