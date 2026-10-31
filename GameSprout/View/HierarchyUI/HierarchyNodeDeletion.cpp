#include "HierarchyUI.h"
#include "ui_HierarchyUI.h"

void HierarchyUI::DeleteContextMenu(){if(g_Self)g_Self->DeleteNode(true);}

void HierarchyUI::DeleteNode(bool context) {
    if(!_Window||!_Ui||!_SceneManager||!BindRuntime()||!_Runtime)return;
    auto* scene=_SceneManager->GetActiveScene();
    const auto contextScene=_ContextSceneID,contextObject=_ContextObjectID;
    const bool captured=_ContextCaptured;
    _ContextCaptured=false;_ContextSceneID=0;_ContextObjectID=0;
    if(_DeleteMenu)_DeleteMenu->SetClickCalback(nullptr);
    if(!scene)return;
    if(_SceneManager->HasTransformEdit()) {_Ui->statusLabel->setText(QStringLiteral("请先完成或取消 Scene 中的变换拖拽。"));return;}
    ReadSharedSelection();
    const auto objectID=context?(captured?contextObject:0):_SelectedID;
    auto* object=_Runtime->QueryGameObject(objectID);
    if((context&&(!captured||contextScene!=scene->GetInstanceID()))||!object||object==scene||object->GetScene()!=scene) {
        _Ui->statusLabel->setText(QStringLiteral("请选择普通场景节点；场景根节点不能删除。"));return;
    }
    std::string error;
    if(!_SceneManager->DeleteObject(objectID,error)) {_Ui->statusLabel->setText(QString::fromStdString(error));return;}
    // Destroy事件在Runtime.Update派发；先清理已失效的选择，避免UI在此之前继续解引用它。
    if(_Selection) {
        const auto selection=_Selection->GetSceneObjectSelection();
        if(selection.SceneID==scene->GetInstanceID()&&!_Runtime->QueryGameObject(selection.ObjectID))_Selection->SetSceneObjectSelection({});
    }
    _RefreshPending=true;Refresh();
}
