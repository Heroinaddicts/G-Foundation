#include "EnvironmentLightingWindow.h"
#include "ui_EnvironmentLightingWindow.h"
#include <QColorDialog>
#include <QHeaderView>
#include <QIcon>
#include <QLocale>
#include <QPixmap>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
float Linear(double value){return value<=.04045?float(value/12.92):float(std::pow((value+.055)/1.055,2.4));}
double SRGB(float value){return value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;}
}
EnvironmentLightingWindow::EnvironmentLightingWindow(QWidget* parent)
    :QDialog(parent),_Ui(new Ui::EnvironmentLightingForm()) {
    _Ui->setupUi(this);
    _Colors={ColorRow{_Ui->ambientLabel,_Ui->ambientRow,{_Ui->ambientRedEdit,_Ui->ambientGreenEdit,_Ui->ambientBlueEdit},_Ui->ambientColorButton},
        ColorRow{_Ui->skyLabel,_Ui->skyRow,{_Ui->skyRedEdit,_Ui->skyGreenEdit,_Ui->skyBlueEdit},_Ui->skyColorButton},
        ColorRow{_Ui->equatorLabel,_Ui->equatorRow,{_Ui->equatorRedEdit,_Ui->equatorGreenEdit,_Ui->equatorBlueEdit},_Ui->equatorColorButton},
        ColorRow{_Ui->groundLabel,_Ui->groundRow,{_Ui->groundRedEdit,_Ui->groundGreenEdit,_Ui->groundBlueEdit},_Ui->groundColorButton}};
    for(std::size_t row=0;row<_Colors.size();++row) {
        for(std::size_t channel=0;channel<3;++channel)
            QObject::connect(_Colors[row].Fields[channel],&QLineEdit::editingFinished,this,[this,row,channel]{CommitColor(row,channel);});
        QObject::connect(_Colors[row].Button,&QPushButton::clicked,this,[this,row]{ChooseColor(row);});
    }
    QObject::connect(_Ui->modeCombo,&QComboBox::currentIndexChanged,this,[this](int mode){
        if(_Loading||mode<0||mode>3)return;
        auto value=_Snapshot.Settings;value.Mode=static_cast<GameSproutRuntime::AmbientLightMode>(mode);Commit(value);
    });
    QObject::connect(_Ui->intensityEdit,&QLineEdit::editingFinished,this,[this]{
        if(_Loading)return;
        bool valid=false;const double number=QLocale::c().toDouble(_Ui->intensityEdit->text().trimmed(),&valid);
        if(!valid||!std::isfinite(number)||number<0||number>std::numeric_limits<float>::max()) {Error("强度须为有限非负数值。");return;}
        auto value=_Snapshot.Settings;value.Intensity=float(number);Commit(value);
    });
    QObject::connect(_Ui->resetButton,&QPushButton::clicked,this,[this]{Commit({});});
    QObject::connect(_Ui->coefficientsToggle,&QToolButton::toggled,this,[this](bool expanded){
        _Ui->coefficientsToggle->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);
        _Ui->coefficientsTable->setVisible(expanded&&_Snapshot.Settings.Mode==GameSproutRuntime::AmbientLightMode::SphericalHarmonics);
    });
    _Ui->coefficientsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _Ui->coefficientsTable->verticalHeader()->setDefaultSectionSize(24);
    UpdateControls();
}
EnvironmentLightingWindow::~EnvironmentLightingWindow(){Shutdown();delete _Ui;}
bool EnvironmentLightingWindow::Bind(ILightingAuthoring* authoring) {
    Shutdown();_Authoring=authoring;
    if(!_Authoring)return false;
    _Subscribed=_Authoring->RegisterEnvironmentListener(this);
    if(!_Subscribed){_Authoring=nullptr;Error("环境照明事件订阅失败。");return false;}
    EnvironmentLightingSnapshot value;std::string error;
    if(!_Authoring->ReadEnvironment(value,error)){Error(error);return false;}
    OnEnvironmentLightingChanged(value);return true;
}
void EnvironmentLightingWindow::Shutdown() {
    if(_Subscribed&&_Authoring)_Authoring->UnregisterEnvironmentListener(this);
    _Subscribed=false;_Authoring=nullptr;_Snapshot={};
}
void EnvironmentLightingWindow::OnEnvironmentLightingChanged(const EnvironmentLightingSnapshot& value) {
    const bool switched=_Snapshot.SceneID!=value.SceneID;
    // A scene switch must not leave a partially typed value addressing the new
    // world. Do not retain Runtime pointers or replay a pending edit afterwards.
    if(switched){_Loading=true;if(auto* focused=focusWidget())focused->clearFocus();}
    _Snapshot=value;
    UpdateControls();
}
GameSproutRuntime::Color& EnvironmentLightingWindow::Color(GameSproutRuntime::EnvironmentLightingSettings& value,std::size_t row) const {
    if(row==0)return value.AmbientColor;if(row==1)return value.SkyColor;if(row==2)return value.EquatorColor;return value.GroundColor;
}
void EnvironmentLightingWindow::UpdateControls() {
    _Loading=true;
    const auto mode=_Snapshot.Settings.Mode;const bool enabled=_Snapshot.Available&&_Snapshot.Editable;
    _Ui->settingsGroup->setEnabled(enabled);_Ui->resetButton->setEnabled(enabled);
    _Ui->availabilityLabel->setVisible(!enabled);
    _Ui->modeCombo->setCurrentIndex(static_cast<int>(mode));
    if(!_Ui->intensityEdit->hasFocus()||_Committing)_Ui->intensityEdit->setText(QString::number(_Snapshot.Settings.Intensity,'g',8));
    _Ui->intensityEdit->setEnabled(mode!=GameSproutRuntime::AmbientLightMode::None);
    for(std::size_t row=0;row<_Colors.size();++row) {
        const bool visible=row==0?mode==GameSproutRuntime::AmbientLightMode::Color:mode==GameSproutRuntime::AmbientLightMode::Gradient;
        _Colors[row].Label->setVisible(visible);_Colors[row].Row->setVisible(visible);
        const auto color=Color(_Snapshot.Settings,row);const float channels[]={color.R,color.G,color.B};
        for(std::size_t channel=0;channel<3;++channel)if(!_Colors[row].Fields[channel]->hasFocus()||_Committing)
            _Colors[row].Fields[channel]->setText(QString::number(channels[channel],'g',8));
        QPixmap swatch(20,14);swatch.fill(QColor::fromRgbF(std::clamp(SRGB(color.R),0.,1.),std::clamp(SRGB(color.G),0.,1.),std::clamp(SRGB(color.B),0.,1.)));
        _Colors[row].Button->setIcon(QIcon(swatch));
    }
    const bool sh=mode==GameSproutRuntime::AmbientLightMode::SphericalHarmonics;
    _Ui->shNotice->setVisible(sh);_Ui->coefficientsToggle->setVisible(sh);
    _Ui->coefficientsTable->setVisible(sh&&_Ui->coefficientsToggle->isChecked());
    for(int row=0;row<9;++row) {
        const auto coefficient=_Snapshot.Settings.SHCoefficients[std::size_t(row)];
        const float channels[]={coefficient.X,coefficient.Y,coefficient.Z};
        for(int column=0;column<3;++column)_Ui->coefficientsTable->item(row,column)->setText(QString::number(channels[column],'g',8));
    }
    _Loading=false;
}
void EnvironmentLightingWindow::Error(const std::string& message) {_Ui->errorLabel->setText(QString::fromStdString(message));_Ui->errorLabel->setVisible(!message.empty());}
void EnvironmentLightingWindow::Commit(const GameSproutRuntime::EnvironmentLightingSettings& value) {
    if(_Loading||!_Authoring||!_Snapshot.Available||!_Snapshot.Editable)return;
    std::string error;_Committing=true;
    const bool accepted=_Authoring->SetEnvironment(_Snapshot.SceneID,value,error);
    _Committing=false;
    Error(accepted?std::string{}:error);UpdateControls();
}
void EnvironmentLightingWindow::CommitColor(std::size_t row,std::size_t channel) {
    if(_Loading||!_Snapshot.Available||!_Snapshot.Editable)return;
    bool valid=false;const double number=QLocale::c().toDouble(_Colors[row].Fields[channel]->text().trimmed(),&valid);
    if(!valid||!std::isfinite(number)||number<0||number>std::numeric_limits<float>::max()) {Error("颜色须为有限非负数值；输入框使用线性 RGB。");return;}
    auto value=_Snapshot.Settings;auto& color=Color(value,row);
    if(channel==0)color.R=float(number);else if(channel==1)color.G=float(number);else color.B=float(number);
    Commit(value);
}
void EnvironmentLightingWindow::ChooseColor(std::size_t row) {
    if(!_Authoring||!_Snapshot.Available||!_Snapshot.Editable)return;
    const auto sceneID=_Snapshot.SceneID;const auto color=Color(_Snapshot.Settings,row);
    const QColor selected=QColorDialog::getColor(QColor::fromRgbF(std::clamp(SRGB(color.R),0.,1.),std::clamp(SRGB(color.G),0.,1.),std::clamp(SRGB(color.B),0.,1.)),this,tr("环境光颜色"));
    if(!selected.isValid()||_Snapshot.SceneID!=sceneID||!_Snapshot.Available||!_Snapshot.Editable)return;
    auto value=_Snapshot.Settings;auto& target=Color(value,row);
    target.R=Linear(selected.redF());target.G=Linear(selected.greenF());target.B=Linear(selected.blueF());Commit(value);
}
