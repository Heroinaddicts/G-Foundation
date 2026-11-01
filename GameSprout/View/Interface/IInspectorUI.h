#ifndef __IInspectorUI_h__
#define __IInspectorUI_h__

#include "GameSproutEditorApi.h"
#include <typeinfo>
namespace GameSproutRuntime { struct ResourceReference; }
class IInspectorExtension;
class ComponentEditor;
class QWidget;

class IInspectorUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IInspectorUI() {}
    virtual bool HasPendingChanges() const = 0;
    virtual bool ApplyChanges() = 0;
    virtual void RevertChanges() = 0;
    virtual bool RegisterExtension(IInspectorExtension* extension) = 0;
    virtual void UnregisterExtension(IInspectorExtension* extension) = 0;
    virtual ComponentEditor* CreateComponentEditor(const std::type_info& type, QWidget* parent) = 0;
};

#endif //__IInspectorUI_h__
