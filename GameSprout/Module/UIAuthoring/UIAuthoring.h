#pragma once
#include "Header.h"
#include "IObjectEditing.h"
class ISceneManager;
class ISelectionManager;
class IPlayMode;
class IRuntimeHost;
class QThread;

class UIAuthoring final : public IUIAuthoring {
public:
    ~UIAuthoring() override;
    bool CanCreate(std::uint64_t,std::uint64_t,std::string&) const override;
    bool CreateControl(std::uint64_t,std::uint64_t,UIControlTemplate,std::uint64_t&,std::string&) override;
    bool SetText(const ObjectEditTarget&,const UITextAuthoringValues&,std::string&) override;
    bool SetButtonInteractable(const ObjectEditTarget&,bool,std::string&) override;
    bool BeginRectEdit(const ObjectEditTarget&,std::string&) override;
    bool PreviewRectEdit(const UIRectAuthoringValues&,std::string&) override;
    bool CommitRectEdit(const UIRectAuthoringValues&,std::string&) override;
    bool CancelRectEdit(std::string&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameObject* ResolveParent(std::uint64_t) const;
    bool HasRectEdit() const;
    struct RectEdit {
        ObjectEditTarget Target;
        std::uint64_t FileID=0;
        UIRectAuthoringValues Before;
        UIRectAuthoringValues Last;
    } _RectEdit;
    ISceneManager* _Scenes=nullptr;
    ISelectionManager* _Selection=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    IObjectEditing* _Editing=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
};
