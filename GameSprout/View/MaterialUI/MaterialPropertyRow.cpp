#include "MaterialPropertyRow.h"
#include "ResourceField.h"
#include "ui_MaterialPropertyRow.h"
#include <QColorDialog>
#include <QIcon>
#include <QPixmap>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace {
void ShowNumber(QDoubleSpinBox * control,double value,bool expandRange=true) {
    // 显示源数据的精度，且不让 .ui 的常用范围截断合法的导入值。
    int decimals=6;
    while (decimals<24 && QString::number(value,'f',decimals).toDouble()!=value) ++decimals;
    control->setDecimals(decimals);
    if (expandRange) control->setRange(std::min(control->minimum(),value),std::max(control->maximum(),value));
    control->setValue(value);
    const QString axis=control->accessibleName();
    control->setToolTip((axis.isEmpty()?QString():axis+"\n")+QStringLiteral("源值：")+QString::number(value,'g',17));
}
}

MaterialPropertyRow::MaterialPropertyRow(QWidget * parent) : QWidget(parent), _Ui(new Ui::MaterialPropertyRowForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->numberSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value) {
        if (_Loading) return;
        _Value=value;
        if (_Definition["Type"]=="Range") {
            QSignalBlocker blocker(_Ui->rangeSlider);
            const double minimum=_Definition["Min"].asDouble(), maximum=_Definition["Max"].asDouble();
            _Ui->rangeSlider->setValue(maximum>minimum ? int(std::round((value-minimum)/(maximum-minimum)*1000)) : 0);
        }
        Notify();
    });
    QObject::connect(_Ui->rangeSlider,&QSlider::valueChanged,this,[this](int value) {
        if (!_Loading) _Ui->numberSpin->setValue(_Definition["Min"].asDouble()+
            (_Definition["Max"].asDouble()-_Definition["Min"].asDouble())*value/1000.0);
    });
    QObject::connect(_Ui->integerEdit,&QLineEdit::textChanged,this,[this](const QString & text) {
        if (_Loading) return;
        bool valid=false;const int value=text.toInt(&valid);
        _IntegerValid=valid;
        if (valid) _Value=value;
        Notify();
    });
    const std::vector<QDoubleSpinBox *> vectorFields={_Ui->vectorXSpin,_Ui->vectorYSpin,_Ui->vectorZSpin,_Ui->vectorWSpin};
    const std::vector<QDoubleSpinBox *> colorFields={_Ui->colorRSpin,_Ui->colorGSpin,_Ui->colorBSpin,_Ui->colorASpin};
    const std::vector<QDoubleSpinBox *> textureFields={_Ui->tilingXSpin,_Ui->tilingYSpin,_Ui->offsetXSpin,_Ui->offsetYSpin};
    for (Json::ArrayIndex i=0;i<4;++i) {
        QObject::connect(vectorFields[i],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,i](double value) {
            if (!_Loading) { _Value[i]=value;Notify(); }
        });
        QObject::connect(colorFields[i],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,i](double value) {
            if (!_Loading) { _Value[i]=value;UpdateColor();Notify(); }
        });
        QObject::connect(textureFields[i],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,i](double value) {
            if (!_Loading) { _TextureST[i]=value;Notify(); }
        });
    }
    QObject::connect(_Ui->toggleCheck,&QCheckBox::toggled,this,[this](bool checked) {
        if (!_Loading) { _Value=checked;Notify(); }
    });
    QObject::connect(_Ui->enumCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index) {
        if (!_Loading) { _Value=index;Notify(); }
    });
    _Ui->textureResource->Changed=[this](const GameSproutRuntime::ResourceReference & reference) {
        if (_Loading) return;
        _Value=Json::Value();
        if (!reference.Guid.empty()) { _Value["Guid"]=reference.Guid;_Value["LocalID"]=Json::UInt64(reference.LocalID); }
        Notify();
    };
    QObject::connect(_Ui->colorButton,&QPushButton::clicked,this,[this] {
        const auto channel=[this](Json::ArrayIndex i) {return std::clamp(_Value[i].asDouble(),0.0,1.0);};
        const QColor selected=QColorDialog::getColor(QColor::fromRgbF(channel(0),channel(1),channel(2),channel(3)),this,
            _Ui->propertyLabel->text(),QColorDialog::ShowAlphaChannel);
        if (!selected.isValid()) return;
        _Loading=true;
        _Ui->colorRSpin->setValue(selected.redF());_Ui->colorGSpin->setValue(selected.greenF());
        _Ui->colorBSpin->setValue(selected.blueF());_Ui->colorASpin->setValue(selected.alphaF());
        _Value=Json::Value(Json::arrayValue);
        for (double value : {selected.redF(),selected.greenF(),selected.blueF(),selected.alphaF()}) _Value.append(value);
        _Loading=false;UpdateColor();Notify();
    });
}
MaterialPropertyRow::~MaterialPropertyRow() { Changed={};_Ui->textureResource->Changed={};delete _Ui; }
void MaterialPropertyRow::Notify() { if (!_Loading && Changed) Changed(); }
void MaterialPropertyRow::UpdateColor() {
    QPixmap swatch(20,20);
    swatch.fill(QColor::fromRgbF(std::clamp(_Value[0].asDouble(),0.0,1.0),std::clamp(_Value[1].asDouble(),0.0,1.0),
        std::clamp(_Value[2].asDouble(),0.0,1.0),std::clamp(_Value[3].asDouble(),0.0,1.0)));
    _Ui->colorButton->setIcon(QIcon(swatch));
}
void MaterialPropertyRow::Load(const Json::Value & definition,const Json::Value & document,IAssetsManager * assets,
    ILibraryManager * library,ISelectionManager * selection,const std::string & context) {
    _Loading=true;_IntegerValid=true;_Definition=definition;
    const std::string name=definition["Name"].asString(), type=definition["Type"].asString();
    _Value=document.isMember(name)?document[name]:definition["Default"];
    setObjectName(QString::fromStdString("material."+name));
    _Ui->propertyLabel->setText(QString::fromStdString(definition.get("DisplayName",name).asString()));
    _Ui->propertyLabel->setToolTip(QString::fromStdString(definition.get("Tooltip",name).asString()));
    const QString group=QString::fromStdString(definition.get("Group","").asString());
    _Ui->groupLabel->setText(group);_Ui->groupLabel->setVisible(!group.isEmpty());
    _Ui->rangeSlider->setVisible(type=="Range");
    _Ui->numberSpin->setObjectName(QString::fromStdString("material."+name+".number"));
    _Ui->integerEdit->setObjectName(QString::fromStdString("material."+name+".integer"));
    if (type=="Float" || type=="Range") {
        _Ui->valueStack->setCurrentIndex(0);
        if (type=="Range") _Ui->numberSpin->setRange(definition["Min"].asDouble(),definition["Max"].asDouble());
        ShowNumber(_Ui->numberSpin,_Value.asDouble(),type!="Range");
        if (type=="Range") {
            const double minimum=definition["Min"].asDouble(), maximum=definition["Max"].asDouble();
            _Ui->rangeSlider->setValue(maximum>minimum ? int(std::round((_Value.asDouble()-minimum)/(maximum-minimum)*1000)) : 0);
        }
    } else if (type=="Int") {
        _Ui->valueStack->setCurrentIndex(1);_Ui->integerEdit->setText(QString::number(_Value.asInt64()));
    } else if (type=="Color" || type=="Vector") {
        _Ui->valueStack->setCurrentIndex(type=="Color"?2:3);
        const std::vector<QDoubleSpinBox *> controls=type=="Color"?
            std::vector<QDoubleSpinBox *>{_Ui->colorRSpin,_Ui->colorGSpin,_Ui->colorBSpin,_Ui->colorASpin}:
            std::vector<QDoubleSpinBox *>{_Ui->vectorXSpin,_Ui->vectorYSpin,_Ui->vectorZSpin,_Ui->vectorWSpin};
        for (Json::ArrayIndex i=0;i<4;++i) {
            ShowNumber(controls[i],_Value[i].asDouble());
            controls[i]->setObjectName(QString::fromStdString("material."+name+"."+std::to_string(i)));
        }
        if (type=="Color") UpdateColor();
    } else if (type=="Texture") {
        _Ui->valueStack->setCurrentIndex(4);
        _Ui->textureResource->Bind(assets,library,selection,eAssetType::Texture);
        _Ui->textureResource->SetContextKey(context+"/"+name);
        _Ui->textureResource->SetValue(_Value.isObject()?
            GameSproutRuntime::ResourceReference{_Value["Guid"].asString(),_Value["LocalID"].asUInt64()}:GameSproutRuntime::ResourceReference{});
        _TextureST=document[name+"ST"];
        if (!_TextureST.isArray()) {
            _TextureST=Json::Value(Json::arrayValue);for(double value:{1.0,1.0,0.0,0.0})_TextureST.append(value);
        }
        const std::vector<QDoubleSpinBox *> controls={_Ui->tilingXSpin,_Ui->tilingYSpin,_Ui->offsetXSpin,_Ui->offsetYSpin};
        for (Json::ArrayIndex i=0;i<4;++i) ShowNumber(controls[i],_TextureST[i].asDouble());
    } else if (type=="Toggle") {
        _Ui->valueStack->setCurrentIndex(5);_Ui->toggleCheck->setChecked(_Value.asBool());
    } else if (type=="Enum") {
        _Ui->valueStack->setCurrentIndex(6);_Ui->enumCombo->clear();
        for(const Json::Value & option:definition["Options"])_Ui->enumCombo->addItem(QString::fromStdString(option.asString()));
        _Ui->enumCombo->setCurrentIndex(_Value.asInt());
    }
    _Loading=false;
}
bool MaterialPropertyRow::Write(Json::Value & document,std::string & error) const {
    if (!_IntegerValid) { error=_Definition["Name"].asString()+" 必须是有效的 32 位有符号整数。";return false; }
    const std::string name=_Definition["Name"].asString();
    document[name]=_Value;
    if (_Definition["Type"]=="Texture") document[name+"ST"]=_TextureST;
    return true;
}
