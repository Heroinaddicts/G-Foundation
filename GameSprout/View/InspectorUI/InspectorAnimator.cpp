#include "InspectorUI.h"
#include "AnimatorControllerDialog.h"
#include "AvatarMaskDialog.h"
#include "ModelRigDialog.h"
#include "IRuntimeAssets.h"

bool InspectorUI::EditAnimatorController(const GameSproutRuntime::ResourceReference& reference) {
    if(!_Window||!_RuntimeAssets)return false;
    if(_ControllerDialog&&_ControllerDialog->Reference()!=reference){if(!_ControllerDialog->close())return false;delete _ControllerDialog;_ControllerDialog=nullptr;}
    if(_ControllerDialog){_ControllerDialog->raise();_ControllerDialog->activateWindow();return true;}
    _ControllerDialog=new AnimatorControllerDialog(_RuntimeAssets,_Assets,_Library,_Selection,reference,_Window);
    _ControllerDialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(_ControllerDialog,&QObject::destroyed,_Dispatch,[this]{_ControllerDialog=nullptr;});
    _ControllerDialog->open();return true;
}

bool InspectorUI::EditAvatarMask(const GameSproutRuntime::ResourceReference& reference) {
    if(!_Window||!_RuntimeAssets)return false;
    if(_MaskDialog&&_MaskDialog->Reference()!=reference){if(!_MaskDialog->close())return false;delete _MaskDialog;_MaskDialog=nullptr;}
    if(_MaskDialog){_MaskDialog->raise();_MaskDialog->activateWindow();return true;}
    _MaskDialog=new AvatarMaskDialog(_RuntimeAssets,_Assets,_Library,_Selection,reference,_Window);
    _MaskDialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(_MaskDialog,&QObject::destroyed,_Dispatch,[this]{_MaskDialog=nullptr;});
    _MaskDialog->open();return true;
}

bool InspectorUI::EditModelRig(const GameSproutRuntime::ResourceReference& reference){
    if(!_Window||!_RuntimeAssets)return false;
    if(_RigDialog&&_RigDialog->Reference()!=reference){if(!_RigDialog->close())return false;delete _RigDialog;_RigDialog=nullptr;}
    if(_RigDialog){_RigDialog->raise();_RigDialog->activateWindow();return true;}
    _RigDialog=new ModelRigDialog(_RuntimeAssets,reference,_Window);_RigDialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(_RigDialog,&QObject::destroyed,_Dispatch,[this]{_RigDialog=nullptr;});_RigDialog->open();return true;
}
