#include "MaterialSlotEditor.h"
#include "ResourceField.h"
#include "ui_MaterialSlotEditor.h"
MaterialSlotEditor::MaterialSlotEditor(QWidget* parent) : QWidget(parent), _Ui(new Ui::MaterialSlotEditorForm()) {
    _Ui->setupUi(this);
    _Resource = new ResourceField(_Ui->materialResourceHost);
    _Ui->materialResourceHostLayout->addWidget(_Resource);
    _Resource->Changed = [this](const GameSproutRuntime::ResourceReference& value) { if (Changed) Changed(_Slot,value); };
    QObject::connect(_Ui->moveUpButton,&QToolButton::clicked,this,[this]{if(Move) Move(_Slot,-1);});
    QObject::connect(_Ui->moveDownButton,&QToolButton::clicked,this,[this]{if(Move) Move(_Slot,1);});
    QObject::connect(_Ui->removeMaterialButton,&QToolButton::clicked,this,[this]{if(Remove) Remove(_Slot);});
}
MaterialSlotEditor::~MaterialSlotEditor() { _Resource->Changed={}; delete _Ui; }
void MaterialSlotEditor::Load(const ComponentEditContext& context,std::size_t slot,std::size_t count,const GameSproutRuntime::ResourceReference& resource) {
    _Slot=slot;
    setObjectName(QStringLiteral("materialSlot.%1").arg(slot));
    _Ui->slotIndexLabel->setText(QString::number(slot));
    _Ui->moveUpButton->setEnabled(slot>0);
    _Ui->moveDownButton->setEnabled(slot+1<count);
    _Resource->Bind(context.Assets,context.Library,context.Selection,eAssetType::Material);
    _Resource->SetContextKey(context.Key("MeshRenderer","Material/"+std::to_string(slot)));
    _Resource->SetValue(resource);
}
void MaterialSlotEditor::Deactivate() {
    Changed={}; Move={}; Remove={}; _Resource->Changed={}; _Resource->SetContextKey("");
    // deleteLater 前也不再冒充同编号的活动槽位（删除后立即 Undo 会创建新行）。
    setObjectName(QString());
    hide();
}
