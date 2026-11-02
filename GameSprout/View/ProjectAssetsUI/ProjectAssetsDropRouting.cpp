#include "ProjectAssetsUI.h"
#include "ProjectAssetsModel.h"
#include "../Shared/SceneObjectDragPayload.h"
#include "../Shared/ValueDropRouting.h"
#include "ui_ProjectAssetsUI.h"
#include <algorithm>

bool ProjectAssetsUI::RegisterDropHandler(const void* owner,const fDropProbe& probe,const fDropExecute& execute) {
    if(!owner||!probe||!execute||std::any_of(_DropHandlers.begin(),_DropHandlers.end(),[owner](const DropHandler& entry){return entry.Owner==owner;}))return false;
    _DropHandlers.push_back({++_NextExtensionID,owner,probe,execute});return true;
}
void ProjectAssetsUI::InstallDropRouting() {
    for(auto* tree:{static_cast<QTreeView*>(_Ui->folderTree),static_cast<QTreeView*>(_Ui->assetTree)}) {
        new ValueDropRouting(tree,_Dispatch,[this,tree](const QPoint& position,const QMimeData* mime,bool execute,std::string& error){
            return RouteSceneObjectDrop(tree,position,mime,execute,error);
        },[this](const std::string& error){if(_Window&&_Ui)_Ui->statusLabel->setText(QString::fromStdString(error));});
    }
}
bool ProjectAssetsUI::RouteSceneObjectDrop(QTreeView* tree,const QPoint& position,const QMimeData* mime,bool execute,std::string& error) {
    SceneObjectDragPayload::Value source;if(!SceneObjectDragPayload::Decode(mime,source))return false;
    const auto index=tree->indexAt(position).siblingAtColumn(0);
    SceneObjectDropContext context;
    context.Directory=(index.isValid()&&index.data(ProjectAssetsModel::FolderRole).toBool()?index.data(ProjectAssetsModel::PathRole).toString():_CurrentPath).toStdString();
    context.SessionID=source.SessionID;context.WorldID=source.WorldID;context.ObjectID=source.ObjectID;
    const auto handlers=_DropHandlers;
    for(const auto& handler:handlers) {
        const auto registered=[&]{return std::any_of(_DropHandlers.begin(),_DropHandlers.end(),[&](const DropHandler& entry){return entry.ID==handler.ID;});};
        if(!registered()||!handler.Probe(context)||!registered())continue;
        return !execute||handler.Execute(context,error);
    }
    return false;
}
