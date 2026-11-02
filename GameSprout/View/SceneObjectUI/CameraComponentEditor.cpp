#include "CameraComponentEditor.h"
#include "ui_CameraComponentEditor.h"
#include <QLineEdit>
#include <limits>

CameraComponentEditor::CameraComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::CameraComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->cameraFovEdit,_Ui->cameraOrthoHeightEdit,_Ui->cameraNearEdit,_Ui->cameraFarEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{CommitValues(edit);});
    _Ui->cameraMaskCombo->MaskChanged=[this](std::uint32_t){if(!_Loading)CommitValues(_Ui->cameraMaskCombo);};
    QObject::connect(_Ui->cameraProjectionCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues(_Ui->cameraProjectionCombo);});
    QObject::connect(_Ui->setMainCameraButton,&QPushButton::clicked,this,[this]{SetMainCamera();});
}
CameraComponentEditor::~CameraComponentEditor() { delete _Ui; }
void CameraComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::ICameraComponent&>(component);
    const auto v = c.GetSettings();
    _Ui->cameraProjectionCombo->setCurrentIndex(static_cast<int>(v.Projection));
    ShowNumbers({_Ui->cameraFovEdit,_Ui->cameraOrthoHeightEdit,_Ui->cameraNearEdit,_Ui->cameraFarEdit},{GameSproutRuntime::RadiansToDegrees(v.VerticalFovRadians),v.OrthographicHeight,v.NearPlane,v.FarPlane});
    if(_Context.Runtime)_Ui->cameraMaskCombo->SetLayers(_Context.Runtime->GetProjectSettings().LayerNames);
    _Ui->cameraMaskCombo->SetMask(v.CullingMask);
    auto* scene = c.GetGameObject()->GetScene();
    _Ui->setMainCameraButton->setEnabled(scene && scene->GetMainCamera() != &c);
}
void CameraComponentEditor::CommitValues(QWidget* changed) {
    if (_Loading||!_PropertiesEditable) return;
    auto* camera=dynamic_cast<GameSproutRuntime::ICameraComponent*>(_Context.ResolveComponent());if(!camera){Fail("Camera 已失效。");return;}
    auto next=camera->GetSettings();
    if(changed==_Ui->cameraProjectionCombo)next.Projection=static_cast<GameSproutRuntime::CameraProjection>(_Ui->cameraProjectionCombo->currentIndex());
    if(changed==_Ui->cameraMaskCombo)next.CullingMask=_Ui->cameraMaskCombo->GetMask();
    if(changed==_Ui->cameraFovEdit){float degrees;if(!ReadNumber(_Ui->cameraFovEdit,degrees)){Fail("视野角须为有限数值。");return;}next.VerticalFovRadians=GameSproutRuntime::DegreesToRadians(degrees);}
    const std::pair<QLineEdit*,float*> fields[]={{_Ui->cameraOrthoHeightEdit,&next.OrthographicHeight},{_Ui->cameraNearEdit,&next.NearPlane},{_Ui->cameraFarEdit,&next.FarPlane}};
    for(const auto& [field,value]:fields)if(changed==field&&!ReadNumber(field,*value)){Fail("相机参数须为有限数值。");return;}
    Commit<GameSproutRuntime::ICameraComponent>(next,"修改 Camera");
}
void CameraComponentEditor::SetMainCamera() {
    auto* object = _Context.Resolve();
    auto* scene = object ? object->GetScene() : nullptr;
    if (!scene || !object->GetComponent<GameSproutRuntime::ICameraComponent>()) { Fail("Camera 组件已失效。"); return; }
    auto* scenes=_Context.Scenes;
    if(!scenes) {Fail("场景服务尚未就绪。");return;}
    if(!_PropertiesEditable)return;
    auto* previousCamera=scene->GetMainCamera();
    if(previousCamera==object->GetComponent<GameSproutRuntime::ICameraComponent>())return;
    if(scenes->IsPlaySessionActive()) {
        const auto nextID=object->GetInstanceID(),previousID=previousCamera?previousCamera->GetGameObject()->GetInstanceID():0;
        const auto operation=[runtime=_Context.Runtime](std::uint64_t id){return [runtime,id](GameSproutRuntime::IGameObject& target,std::string& error){
            auto* object=id&&runtime?runtime->QueryGameObject(id):nullptr;
            auto* camera=object&&object->GetScene()==target.GetScene()?object->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr;
            if((id&&!camera)||!target.GetScene()->SetMainCamera(camera)){error="主相机目标已失效。";return false;}return true;
        };};
        _Context.Execute("设置运行场景主相机",operation(nextID),operation(previousID));Reload();return;
    }
    const bool hadCamera=previousCamera!=nullptr;
    const std::uint64_t previous=hadCamera?scenes->GetObjectFileID(previousCamera->GetGameObject()->GetInstanceID()):0;
    const std::uint64_t next=scenes->GetObjectFileID(object->GetInstanceID());
    if((!next&&object!=scene)||(hadCamera&&!previous&&previousCamera->GetGameObject()!=scene)) {
        Fail("无法分配相机的稳定场景标识。");return;
    }
    const auto operation=[scenes](std::uint64_t fileID,bool hasCamera) {
        // 场景文件ID跨删除/恢复保持稳定，不捕获可能被销毁的Runtime节点ID或节点指针。
        return [scenes,fileID,hasCamera](GameSproutRuntime::IGameObject& target,std::string& error) {
            auto* currentScene=target.GetScene();
            auto* object=hasCamera?scenes->QueryObjectByFileID(fileID):nullptr;
            auto* camera=object&&object->GetScene()==currentScene?object->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr;
            if(!currentScene||(hasCamera&&!camera)||!currentScene->SetMainCamera(camera)) {error="主相机目标已失效。";return false;}
            return currentScene->GetMainCamera()==camera;
        };
    };
    _Context.Execute("设置场景主相机",operation(next,true),operation(previous,hadCamera));
    Reload();
}
