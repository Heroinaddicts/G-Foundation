#include "LightComponentEditor.h"
#include "ILightingAuthoring.h"
#include "ui_LightComponentEditor.h"
#include <QColorDialog>
#include <QPixmap>
#include <QIcon>
#include <cmath>
#include <algorithm>
namespace {
float ToLinear(double value){return value<=0.04045?float(value/12.92):float(std::pow((value+0.055)/1.055,2.4));}
double ToSRGB(float value){return value<=0.0031308?12.92*value:1.055*std::pow(value,1.0/2.4)-0.055;}
}
LightComponentEditor::LightComponentEditor(QWidget* parent,ILightingAuthoring* authoring)
    :ComponentEditor(parent),_Ui(new Ui::LightComponentEditorForm()),_Authoring(authoring) {
    _Ui->setupUi(this);
    for(QLineEdit* edit:{_Ui->redEdit,_Ui->greenEdit,_Ui->blueEdit,_Ui->intensityEdit})
        QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{CommitValues(edit);});
    QObject::connect(_Ui->enabledCheck,&QCheckBox::clicked,this,[this]{CommitValues(_Ui->enabledCheck);});
    QObject::connect(_Ui->colorButton,&QPushButton::clicked,this,[this]{ChooseColor();});
    for(auto* edit:{_Ui->shadowStrengthEdit,_Ui->shadowDistanceEdit,_Ui->shadowDepthBiasEdit,_Ui->shadowNormalBiasEdit})
        QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{CommitValues(edit);});
    QObject::connect(_Ui->shadowGroup,&QGroupBox::clicked,this,[this]{CommitValues(_Ui->shadowGroup);});
    QObject::connect(_Ui->softShadowsCheck,&QCheckBox::clicked,this,[this]{CommitValues(_Ui->softShadowsCheck);});
    QObject::connect(_Ui->cacheStaticCastersCheck,&QCheckBox::clicked,this,[this]{CommitValues(_Ui->cacheStaticCastersCheck);});
    for(auto* combo:{_Ui->shadowResolutionCombo,_Ui->shadowCascadesCombo})
        QObject::connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,combo](int){CommitValues(combo);});
    _Ui->maskCombo->MaskChanged=[this](std::uint32_t){if(!_Loading)CommitValues(_Ui->maskCombo);};
}
LightComponentEditor::~LightComponentEditor(){delete _Ui;}
void LightComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto& settings=static_cast<GameSproutRuntime::ILightComponent&>(component).GetSettings();
    _Ui->enabledCheck->setChecked(settings.Enabled);
    ShowNumbers({_Ui->redEdit,_Ui->greenEdit,_Ui->blueEdit,_Ui->intensityEdit},
        {settings.Color.R,settings.Color.G,settings.Color.B,settings.Intensity});
    if(_Context.Runtime)_Ui->maskCombo->SetLayers(_Context.Runtime->GetProjectSettings().LayerNames);
    _Ui->maskCombo->SetMask(settings.CullingMask);UpdateColor();
    _Ui->shadowGroup->setChecked(settings.Shadows.Enabled);_Ui->softShadowsCheck->setChecked(settings.Shadows.Soft);
    _Ui->cacheStaticCastersCheck->setChecked(settings.Shadows.CacheStaticCasters);
    ShowNumbers({_Ui->shadowStrengthEdit,_Ui->shadowDistanceEdit,_Ui->shadowDepthBiasEdit,_Ui->shadowNormalBiasEdit},
        {settings.Shadows.Strength,settings.Shadows.Distance,settings.Shadows.DepthBias,settings.Shadows.NormalBias});
    unsigned resolution=256;int resolutionIndex=0;
    while(resolution<settings.Shadows.Resolution){resolution*=2;++resolutionIndex;}
    _Ui->shadowResolutionCombo->setCurrentIndex(resolutionIndex);
    _Ui->shadowCascadesCombo->setCurrentIndex(settings.Shadows.CascadeCount==4?2:settings.Shadows.CascadeCount==2?1:0);
}
void LightComponentEditor::UpdateColor() {
    float r=0,g=0,b=0;if(!ReadNumber(_Ui->redEdit,r)||!ReadNumber(_Ui->greenEdit,g)||!ReadNumber(_Ui->blueEdit,b))return;
    QPixmap swatch(24,16);swatch.fill(QColor::fromRgbF(std::clamp(ToSRGB(r),0.0,1.0),std::clamp(ToSRGB(g),0.0,1.0),std::clamp(ToSRGB(b),0.0,1.0)));
    _Ui->colorButton->setIcon(QIcon(swatch));
}
void LightComponentEditor::CommitValues(QWidget* changed) {
    if(_Loading||!_PropertiesEditable||!_Authoring)return;
    const ObjectEditTarget target{_Context.SceneID,_Context.ObjectID,_Context.ComponentID};
    GameSproutRuntime::LightSettings value;std::string error;
    if(!_Authoring->ReadLight(target,value,error)){Fail(error);return;}
    for(const auto& [edit,number]:{std::pair<QLineEdit*,float*>{_Ui->redEdit,&value.Color.R},
        {_Ui->greenEdit,&value.Color.G},{_Ui->blueEdit,&value.Color.B},{_Ui->intensityEdit,&value.Intensity}})
        if((changed==edit||changed==_Ui->colorButton)&&!ReadNumber(edit,*number)){Fail("光照参数须为有限非负数值。");return;}
    if(changed==_Ui->enabledCheck)value.Enabled=_Ui->enabledCheck->isChecked();
    if(changed==_Ui->maskCombo)value.CullingMask=_Ui->maskCombo->GetMask();
    for(const auto& [edit,number]:{std::pair<QLineEdit*,float*>{_Ui->shadowStrengthEdit,&value.Shadows.Strength},
        {_Ui->shadowDistanceEdit,&value.Shadows.Distance},{_Ui->shadowDepthBiasEdit,&value.Shadows.DepthBias},
        {_Ui->shadowNormalBiasEdit,&value.Shadows.NormalBias}})
        if(changed==edit&&!ReadNumber(edit,*number)){Fail("阴影参数须为有限数值。");return;}
    if(changed==_Ui->shadowGroup)value.Shadows.Enabled=_Ui->shadowGroup->isChecked();
    if(changed==_Ui->softShadowsCheck)value.Shadows.Soft=_Ui->softShadowsCheck->isChecked();
    if(changed==_Ui->cacheStaticCastersCheck)value.Shadows.CacheStaticCasters=_Ui->cacheStaticCastersCheck->isChecked();
    if(changed==_Ui->shadowResolutionCombo)value.Shadows.Resolution=256u<<_Ui->shadowResolutionCombo->currentIndex();
    if(changed==_Ui->shadowCascadesCombo)value.Shadows.CascadeCount=1u<<_Ui->shadowCascadesCombo->currentIndex();
    FinishInput();if(!_Authoring->SetLight(target,value,error)){Fail(error);return;}Reload();
}
void LightComponentEditor::ChooseColor() {
    if(_Loading||!_PropertiesEditable)return;
    float r=0,g=0,b=0;if(!ReadNumber(_Ui->redEdit,r)||!ReadNumber(_Ui->greenEdit,g)||!ReadNumber(_Ui->blueEdit,b))return;
    const QColor selected=QColorDialog::getColor(QColor::fromRgbF(std::clamp(ToSRGB(r),0.0,1.0),std::clamp(ToSRGB(g),0.0,1.0),std::clamp(ToSRGB(b),0.0,1.0)),this,tr("方向光颜色"));
    if(!selected.isValid())return;
    ShowNumbers({_Ui->redEdit,_Ui->greenEdit,_Ui->blueEdit},{ToLinear(selected.redF()),ToLinear(selected.greenF()),ToLinear(selected.blueF())});
    CommitValues(_Ui->colorButton);
}
