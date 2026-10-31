#include "ResourceField.h"
#include "ResourcePicker.h"
#include "ui_ResourceField.h"
#include "../Shared/AssetDragPayload.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

ResourceField::ResourceField(QWidget* parent):QWidget(parent),_Ui(new Ui::ResourceFieldForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->selectResourceButton,&QToolButton::clicked,this,[this]{OpenPicker(AssetReferenceResolver::Enumerate(_Assets,_Library,_Expected));});
    QObject::connect(_Ui->clearResourceButton,&QToolButton::clicked,this,[this]{std::string error;AssignReference({},error);});
    QObject::connect(_Ui->locateResourceButton,&QToolButton::clicked,this,[this]{
        AssetReferenceCandidate candidate;std::string error;
        if(!_Selection || !AssetReferenceResolver::Resolve(_Assets,_Library,_Value,_Expected,candidate,error,false)){SetError(error);return;}
        _Selection->SetAssetSelection({{candidate.Reference.Guid,candidate.IsMain?0:candidate.Reference.LocalID,!candidate.IsMain}});
    });
    Refresh();
}
ResourceField::~ResourceField() {
    Changed={};ClosePicker();
    if(_Subscribed && _Assets)_Assets->UnregisterAssetListener(this);
    delete _Ui;
}
void ResourceField::Bind(IAssetsManager* assets,ILibraryManager* library,ISelectionManager* selection,eAssetType expected) {
    if(_Assets==assets && _Library==library && _Selection==selection && _Expected==expected)return;
    ClosePicker();
    if(_Subscribed && _Assets)_Assets->UnregisterAssetListener(this);
    _Assets=assets;_Library=library;_Selection=selection;_Expected=expected;
    _Subscribed=_Assets && _Assets->RegisterAssetListener(this);Refresh();
}
void ResourceField::SetContextKey(const std::string& key) {if(_ContextKey!=key){ClosePicker();_ContextKey=key;}}
void ResourceField::SetValue(const GameSproutRuntime::ResourceReference& value) {
    if(_Value!=value)ClosePicker();
    _Value=value;Refresh();
}
void ResourceField::SetError(const std::string& error) {
    _Ui->resourceErrorLabel->setText(QString::fromStdString(error));_Ui->resourceErrorLabel->setVisible(!error.empty());
}
void ResourceField::Refresh() {
    _Ui->resourceTypeLabel->setText(QString::fromStdString(AssetReferenceResolver::TypeName(_Expected)));
    _Ui->selectResourceButton->setEnabled(_Assets&&_Library&&_Expected!=eAssetType::Unknown);
    const bool bound=!_Value.Guid.empty()||_Value.LocalID;
    _Ui->clearResourceButton->setEnabled(bound);_Ui->locateResourceButton->setEnabled(bound&&_Selection);
    if(!bound){_Ui->resourceNameEdit->clear();_Ui->resourceNameEdit->setToolTip({});SetError({});return;}
    AssetReferenceCandidate candidate;std::string error;
    if(AssetReferenceResolver::Resolve(_Assets,_Library,_Value,_Expected,candidate,error,false)) {
        _Ui->resourceNameEdit->setText(QString::fromStdString(candidate.Name));
        _Ui->resourceNameEdit->setToolTip(QString::fromStdString(candidate.SourcePath+"\n"+_Value.Guid+":"+std::to_string(_Value.LocalID)));
        IAssetMeta* meta=_Assets->QueryAsset(Guid(_Value.Guid));
        SetError(meta&&meta->GetImportState()!=eAssetImportState::Ready?"资源正在导入或导入失败；当前保留上次成功引用。":"");
    } else {
        _Ui->resourceNameEdit->setText(QStringLiteral("Missing (%1)").arg(QString::fromStdString(AssetReferenceResolver::TypeName(_Expected))));
        _Ui->resourceNameEdit->setToolTip(QString::fromStdString(_Value.Guid+":"+std::to_string(_Value.LocalID)));SetError(error);
    }
}
bool ResourceField::AssignReference(const GameSproutRuntime::ResourceReference& reference,std::string& error) {
    error.clear();GameSproutRuntime::ResourceReference value=reference;
    if(!value.Guid.empty() || value.LocalID) {
        AssetReferenceCandidate candidate;
        if(!AssetReferenceResolver::Resolve(_Assets,_Library,value,_Expected,candidate,error)){SetError(error);return false;}
        value=candidate.Reference;
    }
    ClosePicker();
    if(_Value==value){Refresh();return true;}
    _Value=value;Refresh();
    const auto callback=Changed;if(callback)callback(value);
    return true;
}
bool ResourceField::AssignAssetSelection(const ProjectAssetSelection& selection,std::string& error) {
    std::vector<AssetReferenceCandidate> candidates;
    if(!AssetReferenceResolver::Candidates(_Assets,_Library,selection,_Expected,candidates,error)){SetError(error);return false;}
    if(candidates.size()==1)return AssignReference(candidates.front().Reference,error);
    OpenPicker(candidates,selection.GuidString);error.clear();return true;
}
void ResourceField::ClosePicker() {
    ++_Generation;
    ResourcePicker* picker=_Picker;_Picker=nullptr;_PickerSourceGuid.clear();
    if(picker){picker->Selected={};picker->close();}
}
void ResourceField::OpenPicker(const std::vector<AssetReferenceCandidate>& candidates,const std::string& sourceGuid) {
    ClosePicker();
    auto* picker=new ResourcePicker(this);picker->setAttribute(Qt::WA_DeleteOnClose);
    _Picker=picker;_PickerSourceGuid=sourceGuid;
    const auto generation=_Generation;const auto context=_ContextKey;
    picker->SetCandidates(candidates,_Expected);
    QObject::connect(picker,&QObject::destroyed,this,[this,picker]{if(_Picker==picker){_Picker=nullptr;_PickerSourceGuid.clear();}});
    picker->Selected=[this,picker,generation,context](const GameSproutRuntime::ResourceReference& reference) {
        if(_Picker!=picker || generation!=_Generation || context!=_ContextKey)return;
        // 选择器中的行可能经历重导入；赋值前按当前 Manifest 再验证一次。
        AssetReferenceCandidate candidate;std::string error;
        if(!AssetReferenceResolver::Resolve(_Assets,_Library,reference,_Expected,candidate,error)){picker->SetError(error);SetError(error);return;}
        AssignReference(candidate.Reference,error);
    };
    picker->show();picker->raise();picker->activateWindow();
}
void ResourceField::RefreshPicker() {
    if(!_Picker)return;
    std::vector<AssetReferenceCandidate> candidates;
    if(_PickerSourceGuid.empty())candidates=AssetReferenceResolver::Enumerate(_Assets,_Library,_Expected);
    else {std::string error;AssetReferenceResolver::Candidates(_Assets,_Library,{_PickerSourceGuid,0,false},_Expected,candidates,error);}
    _Picker->SetCandidates(candidates,_Expected);
}
void ResourceField::OnAssetCreate(const AssetEvent&){Refresh();RefreshPicker();}
void ResourceField::OnAssetModify(const AssetEvent&){Refresh();RefreshPicker();}
void ResourceField::OnAssetRemove(const AssetEvent&){Refresh();RefreshPicker();}
bool ResourceField::CanDrop(const QMimeData* mime,std::string& error) {
    std::vector<ProjectAssetSelection> values;
    if(!AssetDragPayload::Decode(mime,values) || values.size()!=1){error="请一次拖入一个源文件或子资源。";return false;}
    std::vector<AssetReferenceCandidate> candidates;
    return AssetReferenceResolver::Candidates(_Assets,_Library,values.front(),_Expected,candidates,error);
}
void ResourceField::dragEnterEvent(QDragEnterEvent* event) {
    std::string error;
    if(CanDrop(event->mimeData(),error)){event->setDropAction(Qt::CopyAction);event->accept();}
    else {SetError(error);event->ignore();}
}
void ResourceField::dragMoveEvent(QDragMoveEvent* event) {
    std::string error;
    if(CanDrop(event->mimeData(),error)){event->setDropAction(Qt::CopyAction);event->accept();}else event->ignore();
}
void ResourceField::dropEvent(QDropEvent* event) {
    std::vector<ProjectAssetSelection> values;std::string error;
    if(!AssetDragPayload::Decode(event->mimeData(),values) || values.size()!=1){SetError("请一次拖入一个源文件或子资源。");event->ignore();return;}
    if(!AssignAssetSelection(values.front(),error)){event->ignore();return;}
    event->setDropAction(Qt::CopyAction);event->accept();
}
