#ifndef __IInspectorUI_h__
#define __IInspectorUI_h__

#include "GameSproutEditorApi.h"
#include "ISelectionManager.h"
#include <typeinfo>
namespace GameSproutRuntime { struct ResourceReference; }
class IInspectorExtension;
class ComponentEditor;
class QWidget;

// UI context only: an Inspector lock can target a different object than the
// global selection. No Runtime pointer or component data crosses this boundary.
struct InspectorViewState {
    bool Locked=false;
    SceneObjectSelection Scene;
    std::vector<ProjectAssetSelection> Assets;
};

class IInspectorUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IInspectorUI() {}
    virtual bool HasPendingChanges() const = 0;
    virtual bool ApplyChanges() = 0;
    virtual void RevertChanges() = 0;
    virtual bool RegisterExtension(IInspectorExtension* extension) = 0;
    virtual void UnregisterExtension(IInspectorExtension* extension) = 0;
    virtual ComponentEditor* CreateComponentEditor(const std::type_info& type, QWidget* parent) = 0;
    virtual InspectorViewState CaptureViewState() const {return {};}
    virtual void RestoreViewState(const InspectorViewState&) {}
};

#endif //__IInspectorUI_h__
