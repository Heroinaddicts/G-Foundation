#include "AssetImportPanel.h"
#include "ImportSettingsInspector.h"
#include "InspectorAssetData.h"
#include "TextureInspector.h"
#include "ModelInspector.h"
#include "AudioInspector.h"
#include "TextInspector.h"
#include "FontInspector.h"
#include "VideoInspector.h"
#include "ShaderInspector.h"
#include "IAnimationUI.h"
#include "IRuntimeAssets.h"
#include "ui_AssetImportPanel.h"
#include <algorithm>
AssetImportPanel::AssetImportPanel(const InspectorServices& services, QWidget* parent)
    : InspectorPanel(services, parent), _Ui(new Ui::AssetImportPanelForm) { _Ui->setupUi(this); }
AssetImportPanel::~AssetImportPanel() { for (auto& item : _Panels) if (item.second) item.second->Changed = {}; delete _Ui; }
ImportSettingsInspector* AssetImportPanel::CreateSettings(eAssetImporterType type) {
    ImportSettingsInspector* panel = nullptr;
    switch (type) {
    case eAssetImporterType::Texture: panel = new TextureInspector(_Ui->settingsHost); break;
    case eAssetImporterType::Model: panel = new ModelInspector(_Ui->settingsHost); break;
    case eAssetImporterType::Audio: panel = new AudioInspector(_Ui->settingsHost); break;
    case eAssetImporterType::Text: panel = new TextInspector(_Ui->settingsHost); break;
    case eAssetImporterType::Font: panel = new FontInspector(_Ui->settingsHost); break;
    case eAssetImporterType::Video: panel = new VideoInspector(_Ui->settingsHost); break;
    case eAssetImporterType::Shader: panel = new ShaderInspector(_Ui->settingsHost); break;
    case eAssetImporterType::ComputeShader: panel = new ShaderInspector(_Ui->settingsHost, true); break;
    default: break;
    }
    if (panel) { _Ui->settingsLayout->addWidget(panel); panel->Changed = [this] { if (Changed) Changed(); }; panel->hide(); }
    return panel;
}
void AssetImportPanel::BindModelSources(IAssetMeta& meta, ImportSettingsInspector* panel) {
    auto* model = dynamic_cast<ModelInspector*>(panel);
    if (!model) return;
    auto* animations = _Services.Editor ? _Services.Editor->GetGameSproutView<IAnimationUI>("AnimationUI") : nullptr;
    model->BindAnimationUI(animations);
    std::vector<std::string> clips, paths;
    for (auto* asset : meta.GetSubAssets()) if (asset && asset->GetAssetType() == eAssetType::AnimationClip) clips.push_back(asset->GetName());
    auto* resources = _Services.Editor ? _Services.Editor->GetGameSproutModule<IRuntimeAssets>("RuntimeAssets") : nullptr;
    auto* provider = resources ? resources->GetResourceProvider() : nullptr;
    AssetImportManifest manifest;
    if (provider && _Services.Library && _Services.Library->GetManifest(meta._Guid, manifest)) {
        for (const auto& asset : manifest.Assets) if (asset.Type == eAssetType::Model && asset.SourceKey == "main") {
            const GameSproutRuntime::ResourceReference reference{meta._Guid.GetGuidString(), asset.LocalID};
            model->ConfigureRig = [this, animations, reference] {
                if (animations && (!ResolvePendingChanges || ResolvePendingChanges())) animations->EditModelRig(reference);
            };
            GameSproutRuntime::ModelData data; std::string error;
            if (provider->LoadModel(reference, data, error)) for (const auto& node : data.Nodes) paths.push_back(node.Path);
        }
    }
    std::sort(clips.begin(), clips.end()); clips.erase(std::unique(clips.begin(), clips.end()), clips.end());
    model->SetAnimationSources(clips, paths);
}
bool AssetImportPanel::Load(const InspectorTarget& target, bool changed, std::string& error) {
    error.clear(); auto* meta = InspectorAssetData::Meta(_Services, target);
    if (!meta) { error = "资源已删除或尚未完成登记。"; return false; }
    Json::Value value; if (!InspectorAssetData::ReadMeta(*meta, value, error)) return false;
    const auto selected = target.Assets.front();
    _Presentation = InspectorAssetData::Describe(_Services, target);
    ImportSettingsInspector* panel = nullptr;
    if (!selected.IsSubAsset) {
        const auto found = _Panels.find(meta->_ImporterType);
        panel = found == _Panels.end() ? (_Panels[meta->_ImporterType] = CreateSettings(meta->_ImporterType)) : found->second;
    } else if (!InspectorAssetData::Asset(_Services, target)) { error = "此子资源已不存在，请重新选择。"; return false; }
    BindModelSources(*meta, panel);
    const std::string current = InspectorAssetData::JsonText(value["ImportSettings"]);
    if (panel && (changed || panel != _Active || !HasPendingChanges())) { _Baseline = current; panel->Load(value["ImportSettings"]); }
    else if (panel && current != _Baseline) error = "设置已被外部修改。草稿仍保留，请点击恢复读取最新设置。";
    _Target = target; _Active = panel; _Presentation.Editable = panel != nullptr;
    for (auto& item : _Panels) if (item.second) item.second->setVisible(item.second == panel);
    _Ui->readOnlyLabel->setVisible(!panel);
    if (!panel && !selected.IsSubAsset) _Presentation.Diagnostics["ImportSettings"] = value["ImportSettings"];
    if (!meta->GetImportError().empty()) error = meta->GetImportError();
    return true;
}
bool AssetImportPanel::HasPendingChanges() const {
    Json::Value draft; std::string error;
    return _Active && (!_Active->Read(draft, error) || InspectorAssetData::JsonText(draft) != _Baseline);
}
bool AssetImportPanel::Validate(std::string& error) const { Json::Value value; return !_Active || _Active->Read(value, error); }
bool AssetImportPanel::Apply(std::string& error) {
    if (!_Active || _Target.Assets.size() != 1 || !_Services.Assets) return false;
    Json::Value draft;
    if (!_Active->Read(draft, error) || !_Services.Assets->ApplyImportSettings(Guid(_Target.Assets.front().GuidString), InspectorAssetData::JsonText(draft), _Baseline, error)) return false;
    return Load(_Target, true, error);
}
bool AssetImportPanel::Revert(std::string& error) { return Load(_Target, true, error); }
void AssetImportPanel::Deactivate() { for (auto& item : _Panels) if (item.second) item.second->hide(); _Active = nullptr; _Target = {}; _Baseline.clear(); }
