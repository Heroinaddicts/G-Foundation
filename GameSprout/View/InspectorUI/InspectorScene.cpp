#include "InspectorUI.h"
#include "GameObjectInspector.h"
#include "MaterialInspector.h"
#include "ui_InspectorUI.h"
#include <QStringList>

GameSproutRuntime::IGameObject * InspectorUI::ResolveSceneObject(const SceneObjectSelection & selection) const {
    auto * scene=_Scenes ? _Scenes->GetActiveScene() : nullptr;
    if (!_Runtime || !scene || scene->GetInstanceID()!=selection.SceneID || !selection.ObjectID) return nullptr;
    auto * object=_Runtime->QueryGameObject(selection.ObjectID);
    return object && object->GetScene()==scene ? object : nullptr;
}

void InspectorUI::RefreshSceneObject(GameSproutRuntime::IGameObject & object) {
    if (_MaterialMode) { _MaterialMode=false;_BaselineMaterialSource.clear();_Material->Deactivate();_Material->hide(); }
    _Editable=false;_ActivePanel=nullptr;_BaselineSettings.clear();
    _Ui->assetNameLabel->setText(QString::fromStdString(object.GetName()));
    _Ui->typeLabel->setText(object.GetScene()==&object ? QStringLiteral("Scene · 场景根") : QStringLiteral("GameObject · 场景节点"));
    _Ui->stateLabel->setText(object.IsActiveInHierarchy() ? QStringLiteral("层级中启用") : QStringLiteral("层级中禁用"));
    _Ui->pathEditLabel->setText(QStringLiteral("节点路径"));
    _Ui->guidEditLabel->setText(QStringLiteral("场景 ID"));
    _Ui->localIDEditLabel->setText(QStringLiteral("节点 ID"));
    QStringList path;
    for (auto * current=&object;current;current=current->Parent()) path.prepend(QString::fromStdString(current->GetName()));
    _Ui->pathEdit->setText(path.join(QStringLiteral(" / ")));
    _Ui->pathEdit->setToolTip(_Ui->pathEdit->text());
    _Ui->guidEdit->setText(QString::number(_InspectedScene.SceneID));
    _Ui->localIDEdit->setText(QString::number(_InspectedScene.ObjectID));
    ComponentEditContext context;
    context.Scenes=_Scenes;context.Runtime=_Runtime;context.Preview=_AnimationPreview;context.Assets=_Assets;context.Library=_Library;context.Selection=_Selection;
    context.SceneID=_InspectedScene.SceneID;context.ObjectID=_InspectedScene.ObjectID;
    context.Report=[this](const std::string& error) { _OperationError=QString::fromStdString(error);_Dirty=true;UpdateButtons(); };
    _SceneInfo->Bind(context);
    _SceneInfo->Load(object);
    _Ui->contentStack->setCurrentIndex(2);
    UpdateButtons();
}

void InspectorUI::OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) {
    if(_SceneInfo)_SceneInfo->OnAnimationEvent(event);
}
