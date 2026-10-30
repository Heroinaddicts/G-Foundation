#ifndef __IInspectorUI_h__
#define __IInspectorUI_h__

#include "GameSproutEditorApi.h"

class IInspectorUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IInspectorUI() {}
    virtual bool HasPendingChanges() const = 0;
    virtual bool ApplyChanges() = 0;
    virtual void RevertChanges() = 0;
};

#endif //__IInspectorUI_h__
