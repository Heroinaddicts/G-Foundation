#ifndef __IInspectorUI_h__
#define __IInspectorUI_h__

#include "GameSproutEditorApi.h"
namespace GameSproutRuntime { struct ResourceReference; }

class IInspectorUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IInspectorUI() {}
    virtual bool HasPendingChanges() const = 0;
    virtual bool ApplyChanges() = 0;
    virtual void RevertChanges() = 0;
    virtual bool EditModelRig(const GameSproutRuntime::ResourceReference&) {return false;}
    virtual bool EditAvatarMask(const GameSproutRuntime::ResourceReference&) {return false;}
    virtual bool EditAnimatorController(const GameSproutRuntime::ResourceReference&) {return false;}
};

#endif //__IInspectorUI_h__
