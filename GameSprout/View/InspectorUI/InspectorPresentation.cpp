#include "InspectorUI.h"
#include "GameObjectInspector.h"
#include "MaterialInspector.h"
#include "ui_InspectorUI.h"

void InspectorUI::SetDebugMode(bool debug) {
    _DebugMode=debug;
    UpdatePresentation();
}

void InspectorUI::UpdatePresentation() {
    if (!_Window) return;
    const bool scene=_Ui->contentStack->currentWidget()==_Ui->scenePage;
    const bool hasTarget=_Ui->contentStack->currentWidget()!=_Ui->emptyPage;
    // 节点名称和启用状态由节点 Header 编辑，公共资源标题不再重复显示。
    _Ui->assetHeader->setVisible(!scene);
    _Ui->diagnosticsHost->setVisible(_DebugMode && hasTarget);
    _Ui->infoHost->setVisible(_DebugMode && !_MaterialMode);
    if (_SceneInfo) {
        _SceneInfo->SetDebugMode(_DebugMode);
        _SceneInfo->SetLocked(_Ui->lockButton->isChecked());
    }
    if (_Material) _Material->SetDebugMode(_DebugMode);
    if (_NormalModeMenu) _NormalModeMenu->SetEnabled(_DebugMode);
    if (_DebugModeMenu) _DebugModeMenu->SetEnabled(!_DebugMode);
}
