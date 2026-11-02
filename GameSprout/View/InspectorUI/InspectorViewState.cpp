#include "InspectorUI.h"
#include "ui_InspectorUI.h"
#include <QSignalBlocker>

InspectorViewState InspectorUI::CaptureViewState() const {
    if(!_Window||!_Ui)return {};
    return {_Ui->lockButton->isChecked(),_Target.Scene,_Target.Assets};
}
void InspectorUI::RestoreViewState(const InspectorViewState& state) {
    if(!_Window||!_Ui||!_Dispatch)return;
    _Target.Scene=state.Scene;_Target.Assets=state.Assets;
    _LockedSceneMissing=false;_OperationError.clear();
    {
        const QSignalBlocker blocked(_Ui->lockButton);
        _Ui->lockButton->setChecked(state.Locked);
    }
    if(_ActivePanel)_ActivePanel->SetLocked(state.Locked);
    RequestRefresh();
}
