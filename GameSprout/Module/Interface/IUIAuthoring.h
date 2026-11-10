#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeUIApi.h"

struct ObjectEditTarget;

enum class UIControlTemplate { Canvas, Empty, Panel, Image, Text, Button };

struct UITextAuthoringValues {
    std::string Text;
    GameSproutRuntime::ResourceReference Font;
    GameSproutRuntime::Color Color;
    float FontSize=GameSproutRuntime::DefaultUIFontSize;
};

struct UIRectAuthoringValues {
    GameSproutRuntime::Vector2 AnchoredPosition;
    GameSproutRuntime::Vector2 SizeDelta;
    GameSproutRuntime::Quaternion LocalRotation;
};

class IUIAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    ~IUIAuthoring() override = default;
    virtual bool CanCreate(std::uint64_t sceneID,std::uint64_t parentID,std::string& error) const = 0;
    virtual bool CreateControl(std::uint64_t sceneID,std::uint64_t parentID,UIControlTemplate type,
        std::uint64_t& createdObjectID,std::string& error) = 0;
    virtual bool SetText(const ObjectEditTarget&,const UITextAuthoringValues&,std::string& error) = 0;
    virtual bool SetButtonInteractable(const ObjectEditTarget&,bool,std::string& error) = 0;
    // Target identifies a child RectTransform. Positions and sizes use canvas layout units.
    virtual bool BeginRectEdit(const ObjectEditTarget&,std::string& error) = 0;
    virtual bool PreviewRectEdit(const UIRectAuthoringValues&,std::string& error) = 0;
    virtual bool CommitRectEdit(const UIRectAuthoringValues&,std::string& error) = 0;
    virtual bool CancelRectEdit(std::string& error) = 0;
};
