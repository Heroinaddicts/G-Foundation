#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeAnimationData.h"
class IModelAnimationEditor;
class QWidget;

// 动画专用 View：其他界面通过接口打开编辑器，不依赖私有控件。
class IAnimationUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IAnimationUI() = default;
    virtual bool EditAnimatorController(const GameSproutRuntime::ResourceReference&) = 0;
    virtual bool EditModelRig(const GameSproutRuntime::ResourceReference&) = 0;
    virtual bool EditAvatarMask(const GameSproutRuntime::ResourceReference&) = 0;
    // Qt parent owns the returned editor. Only value snapshots cross this interface.
    virtual IModelAnimationEditor* CreateModelAnimationEditor(QWidget* parent) = 0;
};
