#pragma once
#include "InspectorPanel.h"
namespace Ui {class MaterialInspectorPanelForm;}

class MaterialInspectorPanel final : public InspectorPanel {
public:
    MaterialInspectorPanel(const InspectorServices&,QWidget*);
    ~MaterialInspectorPanel() override;
    bool Load(const InspectorTarget&,bool,std::string&) override;
    InspectorPresentation Describe() const override {return _Presentation;}
    bool HasPendingChanges() const override;
    bool Validate(std::string&) const override;
    bool Apply(std::string&) override;
    bool Revert(std::string&) override;
    void Deactivate() override;
    void SetDebugMode(bool) override;
    bool TracksAsset(const std::string& guid) const override {return _AssetDependencies.contains(guid);}
private:
    void SelectSource(const std::string& guid);
    Ui::MaterialInspectorPanelForm* _Ui=nullptr;
    InspectorTarget _Target;
    InspectorPresentation _Presentation;
    std::string _BaselineSettings;
    std::string _BaselineSource;
    std::set<std::string> _AssetDependencies;
    bool _Loaded=false;
};
