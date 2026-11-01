#include "CameraComponentEditor.h"
#include "ui_CameraComponentEditor.h"
#include <QLineEdit>
#include <limits>

CameraComponentEditor::CameraComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::CameraComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->cameraFovEdit,_Ui->cameraOrthoHeightEdit,_Ui->cameraNearEdit,_Ui->cameraFarEdit,_Ui->cameraMaskEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues();});
    QObject::connect(_Ui->cameraProjectionCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues();});
    QObject::connect(_Ui->setMainCameraButton,&QPushButton::clicked,this,[this]{SetMainCamera();});
}
CameraComponentEditor::~CameraComponentEditor() { delete _Ui; }
void CameraComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::ICameraComponent&>(component);
    const auto v = c.GetSettings();
    _Ui->cameraProjectionCombo->setCurrentIndex(static_cast<int>(v.Projection));
    ShowNumbers({_Ui->cameraFovEdit,_Ui->cameraOrthoHeightEdit,_Ui->cameraNearEdit,_Ui->cameraFarEdit},{GameSproutRuntime::RadiansToDegrees(v.VerticalFovRadians),v.OrthographicHeight,v.NearPlane,v.FarPlane});
    _Ui->cameraMaskEdit->setText("0x"+QString::number(v.CullingMask,16).rightJustified(8,'0'));
    auto* scene = c.GetGameObject()->GetScene();
    _Ui->setMainCameraButton->setEnabled(scene && scene->GetMainCamera() != &c);
}
void CameraComponentEditor::CommitValues() {
    if (_Loading) return;
    std::vector<float> a;
    if (!ReadNumbers({_Ui->cameraFovEdit,_Ui->cameraOrthoHeightEdit,_Ui->cameraNearEdit,_Ui->cameraFarEdit},a)) { Fail("请输入有限的相机参数。"); return; }
    bool valid = false;
    const auto mask = _Ui->cameraMaskEdit->text().trimmed().toULongLong(&valid,0);
    if (!valid || mask > std::numeric_limits<std::uint32_t>::max()) { Fail("可见层掩码需要 32 位无符号整数，支持 0x 十六进制。"); return; }
    auto* object = _Context.Resolve();
    auto* component = object ? object->GetComponent<GameSproutRuntime::ICameraComponent>() : nullptr;
    if (!component) { Fail("Camera 组件已失效。"); return; }
    auto next = component->GetSettings();
    next.Projection = static_cast<GameSproutRuntime::CameraProjection>(_Ui->cameraProjectionCombo->currentIndex());
    const QString oldDegrees = QString::number(GameSproutRuntime::RadiansToDegrees(next.VerticalFovRadians),'g',9);
    if (_Ui->cameraFovEdit->text().trimmed() != oldDegrees) next.VerticalFovRadians = GameSproutRuntime::DegreesToRadians(a[0]);
    next.OrthographicHeight=a[1]; next.NearPlane=a[2]; next.FarPlane=a[3]; next.CullingMask=static_cast<std::uint32_t>(mask);
    Commit<GameSproutRuntime::ICameraComponent>(next,"修改 Camera");
}
void CameraComponentEditor::SetMainCamera() {
    auto* object = _Context.Resolve();
    auto* scene = object ? object->GetScene() : nullptr;
    if (!scene || !object->GetComponent<GameSproutRuntime::ICameraComponent>()) { Fail("Camera 组件已失效。"); return; }
    auto* scenes=_Context.Scenes;
    if(!scenes) {Fail("场景服务尚未就绪。");return;}
    auto* previousCamera=scene->GetMainCamera();
    if(previousCamera==object->GetComponent<GameSproutRuntime::ICameraComponent>())return;
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
