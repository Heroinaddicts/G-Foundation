#include "HierarchyUI.h"
#include "HierarchyTreeModel.h"
#include "../Shared/AssetDragPayload.h"
#include "../Shared/ValueDropRouting.h"
#include "ui_HierarchyUI.h"
#include <algorithm>

bool HierarchyUI::RegisterAssetDropHandler(const void* owner,const fAssetDropProbe& probe,const fAssetDropExecute& execute) {
    if(!owner||!probe||!execute||std::any_of(_AssetDropHandlers.begin(),_AssetDropHandlers.end(),[owner](const AssetDropHandler& entry){return entry.Owner==owner;}))return false;
    _AssetDropHandlers.push_back({++_NextExtensionID,owner,probe,execute});return true;
}
bool HierarchyUI::RegisterContextAction(const void* owner,const std::vector<std::string>& path,fContextActionCallback callback,const fContextActionEnabled& enabled) {
    if(!owner||!callback||path.empty()||std::any_of(path.begin(),path.end(),[](const std::string& value){return value.empty();})||
       std::any_of(_ExtensionActions.begin(),_ExtensionActions.end(),[&](const ExtensionAction& value){return value.Path==path;}))return false;
    _ExtensionActions.push_back({owner,path,callback,enabled,nullptr});RegisterExtensionMenus();return true;
}
HierarchyActionContext HierarchyUI::GetActionContext() const {
    if(_ContextCaptured)return {_ContextSceneID,_ContextObjectID};
    auto* scene=_SceneManager?_SceneManager->GetActiveScene():nullptr;
    return {scene?scene->GetInstanceID():0,_SelectedID};
}
void HierarchyUI::UnregisterExtensions(const void* owner) {
    for(auto& action:_ExtensionActions)if(action.Owner==owner&&action.Menu)action.Menu->SetClickCalback(nullptr);
    std::erase_if(_ExtensionActions,[owner](const ExtensionAction& action){return action.Owner==owner;});
    std::erase_if(_AssetDropHandlers,[owner](const AssetDropHandler& handler){return handler.Owner==owner;});
}
void HierarchyUI::RegisterExtensionMenus() {
    auto* manager=_Docker?_Docker->GetContextMenuManager():nullptr;if(!manager)return;
    for(auto& action:_ExtensionActions) {
        if(action.Menu)continue;IContextMenu* menu=nullptr;
        for(const auto& part:action.Path){menu=menu?menu->CreateChildMenu(part):manager->CreateMenu(part);if(!menu)break;}
        action.Menu=menu;
    }
    RefreshContextActions();
}
void HierarchyUI::RefreshContextActions() {
    const auto context=GetActionContext();const auto actions=_ExtensionActions;
    for(const auto& action:actions) {
        if(!action.Menu)continue;const bool enabled=!action.Enabled||action.Enabled(context);
        const auto found=std::find_if(_ExtensionActions.begin(),_ExtensionActions.end(),[&](const ExtensionAction& entry){return entry.Owner==action.Owner&&entry.Path==action.Path;});
        if(found!=_ExtensionActions.end()&&found->Menu)found->Menu->SetClickCalback(enabled?found->Callback:nullptr);
    }
}
void HierarchyUI::InstallDropRouting() {
    new ValueDropRouting(_Ui->hierarchyTree,_Dispatch,[this](const QPoint& position,const QMimeData* mime,bool execute,std::string& error){return RouteAssetDrop(position,mime,execute,error);},
        [this](const std::string& error){if(_Window&&_Ui)_Ui->statusLabel->setText(QString::fromStdString(error));});
}
bool HierarchyUI::RouteAssetDrop(const QPoint& position,const QMimeData* mime,bool execute,std::string& error) {
    HierarchyAssetDropContext context;if(!AssetDragPayload::Decode(mime,context.Assets))return false;
    auto* scene=_SceneManager?_SceneManager->GetActiveScene():nullptr;if(!scene)return false;
    context.SceneID=scene->GetInstanceID();
    if(auto* content=_SceneManager->GetDocumentContentRoot();content&&content!=scene)context.ParentObjectID=content->GetInstanceID();
    const auto index=_Ui->hierarchyTree->indexAt(position);const auto id=index.isValid()?index.data(HierarchyTreeModel::NodeIDRole).toULongLong():0;
    if(id&&id!=context.SceneID) {
        auto* parent=_Runtime?_Runtime->QueryGameObject(id):nullptr;if(!parent||parent->GetScene()!=scene)return false;
        context.ParentObjectID=id;
    }
    const auto handlers=_AssetDropHandlers;
    for(const auto& handler:handlers) {
        const auto registered=[&]{return std::any_of(_AssetDropHandlers.begin(),_AssetDropHandlers.end(),[&](const AssetDropHandler& entry){return entry.ID==handler.ID;});};
        if(!registered()||!handler.Probe(context)||!registered())continue;
        return !execute||handler.Execute(context,error);
    }
    return false;
}
