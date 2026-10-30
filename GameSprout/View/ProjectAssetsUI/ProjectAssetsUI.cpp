#include "ProjectAssetsUI.h"
#include "ProjectAssetsModel.h"
#include "ui_ProjectAssetsUI.h"
#include <QApplication>
#include <QHeaderView>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QScrollBar>

ProjectAssetsUI::~ProjectAssetsUI() { Release(nullptr); }

void ProjectAssetsUI::BeforeInitialize(GameSprout::GameSproutEditor * editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor,g_IIDEWindow,IDEWindow);
    GetGameSproutModule(editor,_Assets,AssetsManager);
}

bool ProjectAssetsUI::Initialize(GameSprout::GameSproutEditor *) {
    if (_Window) return true;
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()) || !_Assets) return false;
    try {
        _Dispatch = new QObject();
        _Window = new QWidget();
        _Ui = new Ui::ProjectAssetsUIWindow();
        _Ui->setupUi(_Window);
        _FolderModel = new ProjectAssetsModel(_Window);
        _AssetModel = new ProjectAssetsModel(_Window);
        _AssetModel->ErrorColor = _Window->property("errorColor").value<QColor>();
        _AssetModel->PendingColor = _Window->property("pendingColor").value<QColor>();
        _Ui->folderTree->setModel(_FolderModel);
        _Ui->assetTree->setModel(_AssetModel);
        _Ui->splitter->setSizes({210,650});
        _Ui->assetTree->header()->setStretchLastSection(false);
        QObject::connect(_Window,&QObject::destroyed,_Dispatch,[this] {
            _Window=nullptr;_FolderModel=nullptr;_AssetModel=nullptr;_Docker=nullptr;_Attached=false;
        });
        QObject::connect(_Ui->searchEdit,&QLineEdit::textChanged,_Dispatch,[this] { _Dirty=true; });
        QObject::connect(_Ui->rootButton,&QToolButton::clicked,_Dispatch,[this] { NavigateToDirectory(""); });
        QObject::connect(_Ui->upButton,&QToolButton::clicked,_Dispatch,[this] {
            const QString parent=QFileInfo(_CurrentPath).path();
            NavigateToDirectory(parent=="." ? "" : parent.toStdString());
        });
        QObject::connect(_Ui->breadcrumbCombo,qOverload<int>(&QComboBox::activated),_Dispatch,[this](int index) {
            NavigateToDirectory(_Ui->breadcrumbCombo->itemData(index).toString().toStdString());
        });
        QObject::connect(_Ui->folderTree->selectionModel(),&QItemSelectionModel::currentChanged,_Dispatch,
            [this](const QModelIndex & index,const QModelIndex &) {
                if (!_Rebuilding && index.isValid()) NavigateToDirectory(index.data(ProjectAssetsModel::PathRole).toString().toStdString());
            });
        QObject::connect(_Ui->assetTree,&QTreeView::doubleClicked,_Dispatch,[this](const QModelIndex & index) {
            const QModelIndex first=index.siblingAtColumn(0);
            if (first.data(ProjectAssetsModel::FolderRole).toBool()) NavigateToDirectory(first.data(ProjectAssetsModel::PathRole).toString().toStdString());
        });
        QObject::connect(_Ui->folderTree,&QTreeView::expanded,_Dispatch,[this](const QModelIndex & index) {
            if (!_Rebuilding) _ExpandedFolders.insert(index.data(ProjectAssetsModel::KeyRole).toString());
        });
        QObject::connect(_Ui->folderTree,&QTreeView::collapsed,_Dispatch,[this](const QModelIndex & index) {
            if (!_Rebuilding) _ExpandedFolders.erase(index.data(ProjectAssetsModel::KeyRole).toString());
        });
        QObject::connect(_Ui->assetTree,&QTreeView::expanded,_Dispatch,[this](const QModelIndex & index) {
            _AssetModel->LoadSubAssets(index,_Assets);
            if (!_Rebuilding) _ExpandedAssets.insert(index.data(ProjectAssetsModel::KeyRole).toString());
        });
        QObject::connect(_Ui->assetTree,&QTreeView::collapsed,_Dispatch,[this](const QModelIndex & index) {
            if (!_Rebuilding) _ExpandedAssets.erase(index.data(ProjectAssetsModel::KeyRole).toString());
        });
        QObject::connect(_Ui->assetTree->selectionModel(),&QItemSelectionModel::selectionChanged,_Dispatch,[this] {
            if (!_Rebuilding) UpdateStatus();
        });
    } catch (...) { Release(nullptr); throw; }
    return true;
}

bool ProjectAssetsUI::Launch(GameSprout::GameSproutEditor *) {
    if (!_Window || !g_IIDEWindow || !_Assets) return false;
    _Docker = g_IIDEWindow->CreateDocker("ProjectAssets");
    if (!_Docker || !_Docker->AttachWidget(_Window)) return false;
    _Attached = true;
    _Docker->SetTitle("Project");
    if (IIDEMenu * menu = _Docker->CreateChildMenu("刷新视图")) menu->SetClickCallback(&ProjectAssetsUI::RefreshMenu);
    _Subscribed = _Assets->RegisterAssetListener(this);
    if (!_Subscribed) return false;
    // Subscribe before the initial query. Events and snapshots merge by identity.
    Refresh();
    return true;
}

void ProjectAssetsUI::LaunchFinished(GameSprout::GameSproutEditor *) { _Dirty = true; }
void ProjectAssetsUI::Update(GameSprout::GameSproutEditor *) { if (_Dirty && _Window) Refresh(); }
void ProjectAssetsUI::Show() { if (_Window) _Window->show(); if (_Docker) _Docker->Activate(); }
void ProjectAssetsUI::Hide() { if (_Window) _Window->hide(); }
void ProjectAssetsUI::RefreshMenu() { if (g_Self) g_Self->_Dirty=true; }

void ProjectAssetsUI::Release(GameSprout::GameSproutEditor *) {
    if (_Subscribed && _Assets) _Assets->UnregisterAssetListener(this);
    _Subscribed=false;
    delete _Dispatch;_Dispatch=nullptr;
    if (!_Attached) delete _Window;
    _Window=nullptr;_Docker=nullptr;_Attached=false;
    _FolderModel=nullptr;_AssetModel=nullptr;
    delete _Ui;_Ui=nullptr;
    _Assets=nullptr;
    if (g_Self==this) g_Self=nullptr;
}
