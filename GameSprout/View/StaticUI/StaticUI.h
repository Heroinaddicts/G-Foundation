#pragma once
#include "Header.h"
#include "InspectorPanel.h"
class IInspectorUI;
class StaticUI final : public IStaticUI,public IInspectorExtension {
public:
    ~StaticUI() override;
    void Show() override;
    void Hide() override {}
    InspectorSectionPlacement SceneObjectSectionPlacement() const override {return InspectorSectionPlacement::ObjectHeader;}
    InspectorSection* CreateSceneObjectSection(const InspectorServices&,QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    IInspectorUI* _Inspector=nullptr;
    bool _Registered=false;
};
