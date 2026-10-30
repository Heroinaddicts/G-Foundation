#include "InspectorUI.h"
#include "TextureInspector.h"
#include "AssetInfoInspector.h"
#include "ModelInspector.h"
#include "AudioInspector.h"
#include "TextInspector.h"
#include "FontInspector.h"
#include "VideoInspector.h"
#include "ShaderInspector.h"
#include "ui_InspectorUI.h"
#include <QApplication>
#include <QWidget>

InspectorUI::~InspectorUI() { Release(nullptr); }
void InspectorUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GetGameSproutModule(editor, _Assets, AssetsManager);
    GetGameSproutModule(editor, _Selection, SelectionManager);
    GetGameSproutModule(editor, _Library, LibraryManager);
}
bool InspectorUI::Initialize(GameSproutEditor::GameSproutViewApi *) {
    if (_Window) return true;
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()) || !_Assets || !_Selection || !_Library) return false;
    try {
        _Dispatch = new QObject();
        _Window = new QWidget();
        _Ui = new Ui::InspectorUIWindow();
        _Ui->setupUi(_Window);
        _Panels.emplace(eAssetImporterType::Texture,new TextureInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::Model,new ModelInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::Audio,new AudioInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::Text,new TextInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::Font,new FontInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::Video,new VideoInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::Shader,new ShaderInspector(_Ui->textureHost));
        _Panels.emplace(eAssetImporterType::ComputeShader,new ShaderInspector(_Ui->textureHost,true));
        for (const auto & entry:_Panels) {
            _Ui->textureHostLayout->addWidget(entry.second);
            entry.second->hide();
            entry.second->Changed=[this,panel=entry.second] {
                if (panel==_ActivePanel) { _OperationError.clear();UpdateButtons(); }
            };
        }
        _Info = new AssetInfoInspector(_Ui->infoHost);
        _Ui->infoHostLayout->addWidget(_Info);
        QObject::connect(_Window, &QObject::destroyed, _Dispatch, [this] {
            _Window=nullptr;_Info=nullptr;_Docker=nullptr;_Attached=false;_ActivePanel=nullptr;_Panels.clear();
        });
        QObject::connect(_Ui->applyButton, &QPushButton::clicked, _Dispatch, [this] { ApplyChanges(); });
        QObject::connect(_Ui->revertButton, &QPushButton::clicked, _Dispatch, [this] { RevertChanges(); });
        QObject::connect(_Ui->lockButton, &QToolButton::toggled, _Dispatch, [this] { _Dirty=true; });
        Clear(QStringLiteral("选择一个资源以查看属性。"));
    } catch (...) { Release(nullptr);throw; }
    return true;
}
bool InspectorUI::Launch(GameSproutEditor::GameSproutViewApi *) {
    if (!_Window || !g_IIDEWindow) return false;
    _Docker = g_IIDEWindow->CreateDocker("Inspector");
    if (!_Docker || !_Docker->AttachWidget(_Window)) return false;
    _Attached = true;
    _Docker->SetTitle("Inspector");
    _AssetSubscribed = _Assets->RegisterAssetListener(this);
    _SelectionSubscribed = _Selection->RegisterSelectionListener(this);
    if (!_AssetSubscribed || !_SelectionSubscribed) return false;
    Refresh();
    return true;
}
void InspectorUI::Update(GameSproutEditor::GameSproutViewApi *) { if (_Dirty && _Window && !_ResolvingSelection) Refresh(); }
void InspectorUI::Show() { if (_Window) _Window->show();if (_Docker) _Docker->Activate(); }
void InspectorUI::Hide() { if (_Window) _Window->hide(); }
void InspectorUI::Release(GameSproutEditor::GameSproutViewApi *) {
    if (_AssetSubscribed && _Assets) _Assets->UnregisterAssetListener(this);
    if (_SelectionSubscribed && _Selection) _Selection->UnregisterSelectionListener(this);
    _AssetSubscribed=false;_SelectionSubscribed=false;
    for (const auto & entry:_Panels) entry.second->Changed={};
    delete _Dispatch;_Dispatch=nullptr;
    if (!_Attached) delete _Window;
    _Window=nullptr;_Docker=nullptr;_Info=nullptr;_Attached=false;
    _ActivePanel=nullptr;_Panels.clear();
    delete _Ui;_Ui=nullptr;
    _Assets=nullptr;_Selection=nullptr;_Library=nullptr;
    _Inspected.clear();_BaselineSettings.clear();_Editable=false;
    if (g_Self==this) g_Self=nullptr;
}
