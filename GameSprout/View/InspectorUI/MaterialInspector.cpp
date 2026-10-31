#include "MaterialInspector.h"
#include "MaterialPropertyRow.h"
#include "ResourceField.h"
#include "AssetInfoInspector.h"
#include "ISelectionManager.h"
#include "../../Module/Shared/MaterialDefinition.h"
#include "ui_MaterialInspector.h"
#include "ui_MaterialExtract.h"
#include <QDialog>
#include <QRegularExpression>

MaterialInspector::MaterialInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::MaterialInspectorForm()) {
    _Ui->setupUi(this);
    _Ui->debugInfo->hide();
    _Ui->debugToggle->hide();
    QObject::connect(_Ui->debugToggle,&QToolButton::toggled,this,[this](bool visible) {
        _Ui->debugInfo->setVisible(_DebugMode && visible);_Ui->debugToggle->setArrowType(visible?Qt::DownArrow:Qt::RightArrow);
    });
    QObject::connect(_Ui->shaderModeCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this] { ChangeShader(); });
    QObject::connect(_Ui->shaderModeCombo,qOverload<int>(&QComboBox::activated),this,[this] {
        if (!_Error.empty()) ChangeShader();
    });
    _Ui->shaderResource->Changed=[this](const GameSproutRuntime::ResourceReference &) { ChangeShader(); };
    QObject::connect(_Ui->extractMaterialButton,&QPushButton::clicked,this,[this] { ExtractMaterial(); });
}
MaterialInspector::~MaterialInspector() { Deactivate();delete _Ui; }
void MaterialInspector::Bind(IAssetsManager * assets,ILibraryManager * library,ISelectionManager * selection) {
    _Assets=assets;_Library=library;_Selection=selection;
    _Ui->shaderResource->Bind(assets,library,selection,eAssetType::Shader);
}
void MaterialInspector::SetContext(const std::string & key,bool embedded,const std::string & extractionPath) {
    _Context=key;_Embedded=embedded;_ExtractionPath=extractionPath;
    _Ui->shaderResource->SetContextKey(key+"/Shader");
    _Ui->shaderModeCombo->setEnabled(!embedded);
    _Ui->shaderResource->setEnabled(!embedded);
    _Ui->propertyHost->setEnabled(!embedded);
    _Ui->materialStatusLabel->setText(embedded?
        QStringLiteral("模型内嵌材质 · 参数只读。可提取为独立 .mat 后编辑。"):
        QStringLiteral("独立材质 · 修改保存在 .mat 源文件中。"));
    _Ui->extractMaterialButton->setVisible(embedded);
}
void MaterialInspector::Load(const Json::Value & document) {
    _Loading=true;_Document=document;
    const Json::Value shader=document["Shader"];
    _Ui->shaderModeCombo->setCurrentIndex(shader.isObject()?1:0);
    _Ui->shaderResource->SetValue(MaterialDefinition::Reference(shader)?
        GameSproutRuntime::ResourceReference{shader["Guid"].asString(),shader["LocalID"].asUInt64()}:
        GameSproutRuntime::ResourceReference{});
    _Ui->shaderResource->setVisible(shader.isObject());
    _Ui->extractMaterialButton->setText(document["ExternalMaterial"].isObject()?
        QStringLiteral("打开已提取的材质"):QStringLiteral("提取为独立材质…"));
    Rebuild();_Loading=false;
}
bool MaterialInspector::Capture(Json::Value & document,std::string & error) const {
    document=_Document;
    for (const auto * row:_Rows) if (!row->Write(document,error)) return false;
    return true;
}
void MaterialInspector::Rebuild() {
    for (auto * row:_Rows) { row->Changed={};delete row; }
    _Rows.clear();_Schema=Json::Value();_Error.clear();
    if (MaterialDefinition::ResolveSchema(_Library,_Document,_Schema,_Error) &&
        MaterialDefinition::NormalizeValues(_Schema,_Document,_Error)) {
        std::string previousGroup;
        for (const Json::Value & property:_Schema["Properties"]) {
            if (property.get("Hidden",false).asBool()) continue;
            auto * row=new MaterialPropertyRow(_Ui->propertyHost);
            _Ui->propertyLayout->addWidget(row);
            Json::Value display=property;
            const std::string group=property.get("Group","").asString();
            if (group==previousGroup) display["Group"]="";
            previousGroup=group;
            row->Load(display,_Document,_Assets,_Library,_Selection,_Context);
            row->Changed=[this] { if (!_Loading && Changed) Changed(); };
            _Rows.push_back(row);
            row->show();
        }
    }
    _Ui->shaderNameLabel->setText(_Schema.isObject()?QString::fromStdString(_Schema.get("Name","Shader").asString()):QString());
    _Ui->materialErrorLabel->setText(QString::fromStdString(_Error));
    _Ui->materialErrorLabel->setVisible(!_Error.empty());
}
void MaterialInspector::ChangeShader() {
    if (_Loading || _Embedded) return;
    Json::Value document;std::string error;
    if (!Capture(document,error)) { _Ui->materialErrorLabel->setText(QString::fromStdString(error));_Ui->materialErrorLabel->show();return; }
    if (_Ui->shaderModeCombo->currentIndex()==0) document["Shader"]="GameSprout/Preview";
    else {
        const auto resource=_Ui->shaderResource->GetValue();
        document["Shader"]=Json::Value(Json::objectValue);
        document["Shader"]["Guid"]=resource.Guid;document["Shader"]["LocalID"]=Json::UInt64(resource.LocalID);
    }
    _Document=std::move(document);
    // Shader 更换后保留仍然适用的值；名称相同但类型或范围已变的属性采用新默认值。
    Json::Value nextSchema;
    if (MaterialDefinition::ResolveSchema(_Library,_Document,nextSchema,error)) {
        for (const auto & property:nextSchema["Properties"]) {
            const std::string name=property["Name"].asString();
            if (_Document.isMember(name) && !MaterialDefinition::PropertyValue(property,_Document[name]))
                _Document[name]=property["Default"];
        }
    }
    _Ui->shaderResource->setVisible(_Ui->shaderModeCombo->currentIndex()==1);
    Rebuild();
    if (Changed) Changed();
}
bool MaterialInspector::Read(Json::Value & document,std::string & error) const {
    if (!_Error.empty()) { error=_Error;return false; }
    if (!Capture(document,error)) return false;
    return MaterialDefinition::ValidateValues(_Schema,document,error);
}
void MaterialInspector::SetDiagnostics(const Json::Value & diagnostics) { _Ui->debugInfo->Load(QStringLiteral("产物信息"),diagnostics); }
void MaterialInspector::SetDebugMode(bool debug) {
    _DebugMode=debug;
    _Ui->debugToggle->setVisible(debug);
    _Ui->debugInfo->setVisible(debug && _Ui->debugToggle->isChecked());
}
void MaterialInspector::ExtractMaterial() {
    if (!_Embedded) return;
    const Json::Value external=_Document["ExternalMaterial"];
    if (MaterialDefinition::Reference(external)) {
        if (OpenExternal) OpenExternal(external["Guid"].asString());
        return;
    }
    const auto extract=Extract;
    if (!extract) return;
    QDialog dialog(this);Ui::MaterialExtractDialog form;form.setupUi(&dialog);
    QString name=_Document["Name"].isString()?QString::fromStdString(_Document["Name"].asString()):QStringLiteral("Material");
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),QStringLiteral("_"));
    form.pathEdit->setText(_ExtractionPath.empty()?name+QStringLiteral(".mat"):QString::fromStdString(_ExtractionPath));
    QObject::connect(form.cancelButton,&QPushButton::clicked,&dialog,&QDialog::reject);
    QObject::connect(form.extractButton,&QPushButton::clicked,&dialog,[&] {
        std::string error;
        if (extract(form.pathEdit->text().trimmed().toStdString(),error)) dialog.accept();
        else { form.errorLabel->setText(QString::fromStdString(error));form.errorLabel->show(); }
    });
    dialog.exec();
}
void MaterialInspector::Deactivate() {
    Extract={};OpenExternal={};
    for (auto * row:_Rows) { row->Changed={};delete row; }
    _Rows.clear();_Ui->shaderResource->SetContextKey("");_Ui->shaderResource->Bind(nullptr,nullptr,nullptr,eAssetType::Shader);
    _Assets=nullptr;_Library=nullptr;_Selection=nullptr;_Document=Json::Value();_Schema=Json::Value();_Error.clear();
}
