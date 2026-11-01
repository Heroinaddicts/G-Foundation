#pragma once
#include "InspectorPanel.h"
#include "Define.h"
#include <map>
class ImportSettingsInspector;
namespace Ui { class AssetImportPanelForm; }
class AssetImportPanel final : public InspectorPanel {
public:
    AssetImportPanel(const InspectorServices&, QWidget*);
    ~AssetImportPanel() override;
    bool Load(const InspectorTarget&, bool, std::string&) override;
    InspectorPresentation Describe() const override { return _Presentation; }
    bool HasPendingChanges() const override;
    bool Validate(std::string&) const override;
    bool Apply(std::string&) override;
    bool Revert(std::string&) override;
    void Deactivate() override;
private:
    ImportSettingsInspector* CreateSettings(eAssetImporterType);
    void BindModelSources(IAssetMeta&, ImportSettingsInspector*);
    Ui::AssetImportPanelForm* _Ui = nullptr;
    std::map<eAssetImporterType, ImportSettingsInspector*> _Panels;
    ImportSettingsInspector* _Active = nullptr;
    InspectorTarget _Target;
    InspectorPresentation _Presentation;
    std::string _Baseline;
};
