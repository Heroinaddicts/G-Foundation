#include "AnimationAssetPanel.h"
#include "IAnimationUI.h"
#include "IAnimationEditor.h"
#include "IRuntimeAssets.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ui_AnimationAssetPanel.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QFileInfo>

namespace {
GameSproutRuntime::ResourceReference MainReference(ILibraryManager* library, IAssetMeta& meta, eAssetType type) {
    AssetImportManifest manifest;
    if (library && library->GetManifest(meta._Guid, manifest)) {
        for (const auto& asset : manifest.Assets) {
            if (asset.Type == type && asset.SourceKey == "main") return {meta._Guid.GetGuidString(), asset.LocalID};
        }
    }
    return {};
}
}

AnimationAssetPanel::AnimationAssetPanel(const InspectorServices& services, IAnimationUI* animation, QWidget* parent)
    : InspectorPanel(services,parent), _Ui(new Ui::AnimationAssetPanelForm()), _AnimationUI(animation) {
    _Ui->setupUi(this);
    if (services.Editor) {
        _Authoring = services.Editor->GetGameSproutModule<IAnimationEditor>("AnimationEditor");
        _Resources = services.Editor->GetGameSproutModule<IRuntimeAssets>("RuntimeAssets");
    }
    _Ui->clipImportInspector->Changed = [this] { if (Changed) Changed(); };
    QObject::connect(_Ui->editControllerButton, &QPushButton::clicked, this, [this] {
        if (_Target.Assets.size()!=1 || !_Services.Assets || !_AnimationUI) return;
        const auto selected = _Target.Assets.front();
        auto* meta = _Services.Assets->QueryAsset(Guid(selected.GuidString));
        if (!meta) return;
        const auto reference = selected.IsSubAsset ? GameSproutRuntime::ResourceReference{selected.GuidString,selected.LocalID}
            : MainReference(_Services.Library,*meta,eAssetType::AnimatorController);
        if (!reference.Guid.empty()) _AnimationUI->EditAnimatorController(reference);
    });
}
AnimationAssetPanel::~AnimationAssetPanel() {
    _Ui->clipImportInspector->Changed = {};
    _Ui->clipPreviewInspector->EditSource = {};
    _Ui->avatarInspector->Edit = {};
    _Ui->maskInspector->Edit = {};
    delete _Ui;
}

bool AnimationAssetPanel::Load(const InspectorTarget& target, bool changed, std::string& error) {
    error.clear();
    if (target.Assets.size()!=1 || !_Services.Assets || !_Services.Library) { error="请选择一个动画资源。";return false; }
    const auto selected = target.Assets.front();
    auto* meta = _Services.Assets->QueryAsset(Guid(selected.GuidString));
    if (!meta) { error="动画资源已删除或尚未完成登记。";return false; }
    const bool wasImportSettings = _ImportSettings;
    _Target = target;
    _ImportSettings = !selected.IsSubAsset && meta->_ImporterType==eAssetImporterType::AnimationClip;
    _Ui->clipImportInspector->setVisible(_ImportSettings);
    _Ui->clipPreviewInspector->hide();_Ui->avatarInspector->hide();_Ui->maskInspector->hide();_Ui->controllerPanel->hide();
    _Presentation = {};
    _Presentation.Name = QFileInfo(QString::fromStdString(meta->GetPath())).fileName();
    _Presentation.Path = "Assets/"+QString::fromStdString(meta->GetPath());
    _Presentation.Guid = QString::fromStdString(selected.GuidString);
    _Presentation.LocalID = selected.IsSubAsset ? QString::number(selected.LocalID) : QStringLiteral("源文件");
    const auto state = meta->GetImportState();
    _Presentation.State = state==eAssetImportState::Ready ? QStringLiteral("就绪")
        : state==eAssetImportState::Failed ? QStringLiteral("导入失败") : state==eAssetImportState::Pending ? QStringLiteral("待导入") : QStringLiteral("无需导入");
    if (!meta->GetImportError().empty()) _Presentation.State = QString::fromStdString(meta->GetImportError());
    if (_ImportSettings) return LoadImportSettings(*meta,changed || !wasImportSettings,error);
    _Baseline.clear();
    IAsset* asset = selected.IsSubAsset ? _Services.Library->QueryAsset(meta->_Guid,selected.LocalID) : nullptr;
    eAssetType type = asset ? asset->GetAssetType() : meta->_ImporterType==eAssetImporterType::AvatarMask ? eAssetType::AvatarMask : eAssetType::AnimatorController;
    const auto reference = selected.IsSubAsset ? GameSproutRuntime::ResourceReference{selected.GuidString,selected.LocalID}
        : MainReference(_Services.Library,*meta,type);
    if (reference.Guid.empty()) {error="动画资源尚未完成导入。";return false;}
    if (asset) {
        _Presentation.Name = QString::fromStdString(asset->GetName());
        _Presentation.Diagnostics["ArtifactPath"] = asset->GetArtifactPath();
        _Presentation.Diagnostics["DataVersion"] = asset->GetDataVersion();
        _Presentation.Diagnostics["DataSize"] = Json::UInt64(asset->GetDataSize());
    }
    if (type==eAssetType::AnimationClip) {
        auto* provider = _Resources ? _Resources->GetResourceProvider() : nullptr;
        GameSproutRuntime::AnimationClipData clip;
        if (!provider || !provider->LoadAnimationClip(reference,clip,error)) { if(error.empty())error="动画资源服务尚未准备完成。";return false; }
        _Ui->clipPreviewInspector->Load(clip);
        _Ui->clipPreviewInspector->EditSource = [this,selected] { if(RequestSelection)RequestSelection({selected.GuidString,0,false}); };
        _Ui->clipPreviewInspector->show();_Presentation.Type = QStringLiteral("AnimationClip · 已导入片段");
    } else if (type==eAssetType::Avatar) {
        _Ui->avatarInspector->Load(_Authoring,reference);
        _Ui->avatarInspector->Edit = [this,reference] { if(_AnimationUI)_AnimationUI->EditModelRig(reference); };
        _Ui->avatarInspector->show();_Presentation.Type = QStringLiteral("Avatar");
    } else if (type==eAssetType::AvatarMask) {
        _Ui->maskInspector->Load(_Authoring,_Services.Assets,_Services.Library,_Services.Selection,reference);
        _Ui->maskInspector->Edit = [this,reference] { if(_AnimationUI)_AnimationUI->EditAvatarMask(reference); };
        _Ui->maskInspector->show();_Presentation.Type = QStringLiteral("AvatarMask");
    } else if (type==eAssetType::AnimatorController) {
        GameSproutRuntime::AnimatorControllerData controller;std::string snapshot;
        const bool loaded = _Authoring && _Authoring->LoadAnimatorControllerSource(reference,controller,snapshot,error);
        _Ui->controllerSummary->setText(loaded ? tr("%1 个动画层 · %2 个参数").arg(controller.Layers.size()).arg(controller.Parameters.size()) : QString::fromStdString(error));
        _Ui->editControllerButton->setEnabled(loaded);_Ui->controllerPanel->show();_Presentation.Type = QStringLiteral("AnimatorController");
        error.clear();
    } else {error="该资源没有动画专用面板。";return false;}
    return true;
}

void AnimationAssetPanel::BindAnimationSources(IAssetMeta& meta) {
    _Ui->clipImportInspector->BindSourceAvatar(_Services.Assets,_Services.Library,_Services.Selection,"AnimationSourceAvatar:"+meta._Guid.GetGuidString());
    std::vector<std::string> paths;
    auto* provider = _Resources ? _Resources->GetResourceProvider() : nullptr;
    const auto reference = MainReference(_Services.Library,meta,eAssetType::AnimationClip);
    if (provider && !reference.Guid.empty()) {
        GameSproutRuntime::AnimationClipData clip;std::string ignored;
        if (provider->LoadAnimationClip(reference,clip,ignored)) paths=clip.Tracks;
    }
    _Ui->clipImportInspector->SetNodePaths(paths);
}

bool AnimationAssetPanel::LoadImportSettings(IAssetMeta& meta, bool changed, std::string& error) {
    std::string snapshot;Json::Value document;
    if (!meta.SerializeMeta(snapshot) || !AssetArtifactIO::Parse(snapshot,document)) {error="无法读取动画导入设置。";return false;}
    BindAnimationSources(meta);
    const auto current = AssetArtifactIO::JsonText(document["ImportSettings"]);
    if (changed || _Baseline.empty() || !HasPendingChanges()) {
        _Baseline=current;_Ui->clipImportInspector->Load(document["ImportSettings"]);
    } else if (current!=_Baseline) _Presentation.State=QStringLiteral("设置已被外部修改。草稿已保留，请恢复后重新编辑。");
    _Presentation.Type=QStringLiteral("AnimationClip");_Presentation.Editable=true;
    return true;
}
bool AnimationAssetPanel::HasPendingChanges() const {
    if (!_ImportSettings) return false;
    Json::Value settings;std::string ignored;
    return !_Ui->clipImportInspector->Read(settings,ignored) || AssetArtifactIO::JsonText(settings)!=_Baseline;
}
bool AnimationAssetPanel::Validate(std::string& error) const {
    error.clear();if (!_ImportSettings) return true;
    Json::Value settings;return _Ui->clipImportInspector->Read(settings,error);
}
bool AnimationAssetPanel::Apply(std::string& error) {
    if (!_ImportSettings || _Target.Assets.size()!=1 || !_Services.Assets) {error="当前动画资源没有待应用的导入设置。";return false;}
    Json::Value settings;
    if (!_Ui->clipImportInspector->Read(settings,error)
        || !_Services.Assets->ApplyImportSettings(Guid(_Target.Assets.front().GuidString),AssetArtifactIO::JsonText(settings),_Baseline,error)) return false;
    return Revert(error);
}
bool AnimationAssetPanel::Revert(std::string& error) {
    if (!_ImportSettings || _Target.Assets.size()!=1 || !_Services.Assets) {error.clear();return true;}
    auto* meta = _Services.Assets->QueryAsset(Guid(_Target.Assets.front().GuidString));
    return meta ? LoadImportSettings(*meta,true,error) : (error="动画资源已删除。",false);
}
