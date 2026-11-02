#pragma once
#include "InspectorPanel.h"
#include "RuntimePhysicsData.h"
class IPhysicsAuthoring;
namespace Ui {class PhysicsMaterialPanelForm;}
class PhysicsMaterialPanel final:public InspectorPanel {
public:
    PhysicsMaterialPanel(const InspectorServices&,IPhysicsAuthoring*,QWidget*);
    ~PhysicsMaterialPanel() override;
    bool Load(const InspectorTarget&,bool,std::string&) override;
    InspectorPresentation Describe() const override {return _Presentation;}
    bool HasPendingChanges() const override;
    bool Validate(std::string&) const override;
    bool Apply(std::string&) override;
    bool Revert(std::string&) override;
    void Deactivate() override;
private:
    bool Read(GameSproutRuntime::PhysicsMaterialData&,std::string&) const;
    void Display(const GameSproutRuntime::PhysicsMaterialData&);
    Ui::PhysicsMaterialPanelForm* _Ui=nullptr;
    IPhysicsAuthoring* _Authoring=nullptr;
    InspectorTarget _Target;
    InspectorPresentation _Presentation;
    GameSproutRuntime::ResourceReference _Reference;
    GameSproutRuntime::PhysicsMaterialData _Baseline;
    std::string _SourceSnapshot;
    bool _Loaded=false,_Loading=false;
};
