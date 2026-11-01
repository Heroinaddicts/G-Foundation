#include "TransformComponentEditor.h"
#include "ui_TransformComponentEditor.h"
#include <QLineEdit>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr double Pi=3.14159265358979323846;
GameSproutRuntime::Quaternion FromEulerDegrees(GameSproutRuntime::Vector3 degrees) {
    // Z -> X -> Y, matching Unity's normal Transform Inspector rotation order.
    const double x=std::remainder(double(degrees.X),360.0)*Pi/360.0;
    const double y=std::remainder(double(degrees.Y),360.0)*Pi/360.0;
    const double z=std::remainder(double(degrees.Z),360.0)*Pi/360.0;
    const double sx=std::sin(x),cx=std::cos(x),sy=std::sin(y),cy=std::cos(y),sz=std::sin(z),cz=std::cos(z);
    return {float(cy*sx*cz+sy*cx*sz),float(sy*cx*cz-cy*sx*sz),
            float(cy*cx*sz-sy*sx*cz),float(cy*cx*cz+sy*sx*sz)};
}
GameSproutRuntime::Vector3 ToEulerDegrees(GameSproutRuntime::Quaternion value) {
    const double inverse=1.0/std::sqrt(double(value.X)*value.X+double(value.Y)*value.Y+double(value.Z)*value.Z+double(value.W)*value.W);
    const double x=value.X*inverse,y=value.Y*inverse,z=value.Z*inverse,w=value.W*inverse;
    const double sine=std::clamp(2.0*(w*x-y*z),-1.0,1.0);
    const double rx=std::asin(sine);
    double ry,rz;
    if(std::fabs(sine)<1.0-1e-12) {
        ry=std::atan2(2.0*(x*z+y*w),1.0-2.0*(x*x+y*y));
        rz=std::atan2(2.0*(x*y+z*w),1.0-2.0*(x*x+z*z));
    } else {
        // At the singularity, one stable representative is sufficient; preserve
        // the user's typed Euler values separately rather than canonicalizing each repaint.
        ry=std::atan2(2.0*(y*w-x*z),1.0-2.0*(y*y+z*z));rz=0;
    }
    const auto degrees=[](double radians){const double value=radians*180.0/Pi;return std::fabs(value)<1e-5?0.f:float(value);};
    return {degrees(rx),degrees(ry),degrees(rz)};
}
bool SameRotation(GameSproutRuntime::Quaternion a,GameSproutRuntime::Quaternion b) {
    GameSproutRuntime::Quaternion first,second;
    if(!a.TryNormalize(first)||!b.TryNormalize(second))return false;
    const float sign=first.X*second.X+first.Y*second.Y+first.Z*second.Z+first.W*second.W<0?-1.f:1.f;
    return std::fabs(first.X-sign*second.X)<=1e-6f&&std::fabs(first.Y-sign*second.Y)<=1e-6f
        &&std::fabs(first.Z-sign*second.Z)<=1e-6f&&std::fabs(first.W-sign*second.W)<=1e-6f;
}
}

TransformComponentEditor::TransformComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::TransformComponentEditorForm()) {
    _Ui->setupUi(this);
    auto* timer=new QTimer(this);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,this,[this]{if(isVisible())RefreshAnimationState();});timer->start();
    for (QLineEdit* edit : {_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,
        _Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit})
        QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues(false);});
    for (QLineEdit* edit : {_Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit})
        QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues(true);});
}
TransformComponentEditor::~TransformComponentEditor() { delete _Ui; }
void TransformComponentEditor::ContextChanged() {_HasDisplayedRotation=false;}
void TransformComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::ITransformComponent&>(component);
    const auto v = InspectorValues::Read(c);
    if(!_HasDisplayedRotation||!SameRotation(v.Rotation,_DisplayedRotation))_DisplayedEuler=ToEulerDegrees(v.Rotation);
    _DisplayedRotation=v.Rotation;_HasDisplayedRotation=true;
    ShowNumbers({_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,
        _Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit,
        _Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit},
        {v.Position.X,v.Position.Y,v.Position.Z,_DisplayedEuler.X,_DisplayedEuler.Y,_DisplayedEuler.Z,v.Scale.X,v.Scale.Y,v.Scale.Z});
    RefreshAnimationState();
}
void TransformComponentEditor::CommitValues(bool rotationEdited) {
    if (_Loading) return;
    auto* object=_Context.Resolve();
    if(object&&object->GetTransform()->IsAnimationDriven()) {
        Fail("此变换由动画预览驱动，请先还原动画再编辑初始值。");return;
    }
    std::vector<float> a;
    if (!ReadNumbers({_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,
        _Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit,
        _Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit},a)) { Fail("请输入有限数字，可使用科学计数法。"); return; }
    if(!object)return;
    const auto previous=InspectorValues::Read(*object->GetTransform());
    auto v=previous;v.Position={a[0],a[1],a[2]};v.Scale={a[6],a[7],a[8]};
    if(rotationEdited&&(!_HasDisplayedRotation||a[3]!=_DisplayedEuler.X||a[4]!=_DisplayedEuler.Y||a[5]!=_DisplayedEuler.Z)) {
        _DisplayedEuler={a[3],a[4],a[5]};const auto rotation=FromEulerDegrees(_DisplayedEuler);
        // Merely opening the Inspector, editing position/scale, or entering an
        // equivalent angle must not round-trip and subtly change serialized rotation.
        if(!SameRotation(rotation,v.Rotation))v.Rotation=rotation;
        _DisplayedRotation=v.Rotation;_HasDisplayedRotation=true;
    }
    std::string error;
    if(!InspectorValues::Validate(v,error)){Fail(error);return;}
    const bool position=v.Position.X!=previous.Position.X||v.Position.Y!=previous.Position.Y||v.Position.Z!=previous.Position.Z;
    const bool scale=v.Scale.X!=previous.Scale.X||v.Scale.Y!=previous.Scale.Y||v.Scale.Z!=previous.Scale.Z;
    const bool rotation=rotationEdited&&!SameRotation(v.Rotation,previous.Rotation);
    if(!position&&!scale&&!rotation){Reload();return;}
    // Preserve untouched authoring values exactly: the shared Transform writer
    // normalizes rotation even for a position-only edit, so submit field-specific
    // value closures through the same scene transaction/Undo path instead.
    const auto operation=[position,rotation,scale](InspectorValues::Transform value){
        return [value,position,rotation,scale](GameSproutRuntime::IGameObject& target,std::string& failure){
            auto* transform=target.GetTransform();
            if(!transform||transform->IsAnimationDriven()){failure="请先还原动画再编辑初始变换。";return false;}
            if(rotation&&!transform->SetLocalRotation(value.Rotation)){failure="无效的局部旋转。";return false;}
            if(position)transform->SetLocalPosition(value.Position);
            if(scale)transform->SetLocalScale(value.Scale);
            const auto current=InspectorValues::Read(*transform);
            if((position&&(current.Position.X!=value.Position.X||current.Position.Y!=value.Position.Y||current.Position.Z!=value.Position.Z))
                ||(scale&&(current.Scale.X!=value.Scale.X||current.Scale.Y!=value.Scale.Y||current.Scale.Z!=value.Scale.Z))
                ||(rotation&&!SameRotation(current.Rotation,value.Rotation))){failure="Runtime 拒绝修改变换。";return false;}
            return true;
        };
    };
    _Context.Execute("修改 Transform",operation(v),operation(previous));Reload();
}

void TransformComponentEditor::RefreshAnimationState() {
    auto* object=_Context.Resolve();if(!object)return;
    const bool driven=object->GetTransform()->IsAnimationDriven();
    const bool readOnly=driven||object->GetScene()==object;
    for(QLineEdit* edit:{_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,
        _Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit,
        _Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit})edit->setReadOnly(readOnly);
    _Ui->animationDrivenLabel->setVisible(driven);
}
