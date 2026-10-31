#include "ResourcePicker.h"
#include "ui_ResourcePickerDialog.h"
#include <QHeaderView>
#include <QPushButton>

ResourcePicker::ResourcePicker(QWidget* parent) : QDialog(parent),_Ui(new Ui::ResourcePickerDialog()) {
    _Ui->setupUi(this);
    _Ui->resourceTree->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    _Ui->resourceTree->header()->setSectionResizeMode(1,QHeaderView::Stretch);
    _Ui->resourceTree->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    QObject::connect(_Ui->searchEdit,&QLineEdit::textChanged,this,[this]{Filter();});
    QObject::connect(_Ui->buttonBox,&QDialogButtonBox::accepted,this,[this]{Choose();});
    QObject::connect(_Ui->buttonBox,&QDialogButtonBox::rejected,this,&QDialog::reject);
    QObject::connect(_Ui->resourceTree,&QTreeWidget::itemDoubleClicked,this,[this]{Choose();});
    QObject::connect(_Ui->resourceTree,&QTreeWidget::itemSelectionChanged,this,[this]{
        _Ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(_Ui->resourceTree->currentItem()!=nullptr);
    });
    _Ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
}
ResourcePicker::~ResourcePicker(){delete _Ui;}
void ResourcePicker::SetCandidates(const std::vector<AssetReferenceCandidate>& candidates,eAssetType type) {
    GameSproutRuntime::ResourceReference previous;
    if(auto* item=_Ui->resourceTree->currentItem())previous={item->data(0,Qt::UserRole).toString().toStdString(),item->data(0,Qt::UserRole+1).toULongLong()};
    _Ui->resourceTree->clear();
    _Ui->expectedTypeLabel->setText(QString::fromStdString(AssetReferenceResolver::TypeName(type)));
    for(const auto& value:candidates) {
        auto* item=new QTreeWidgetItem(_Ui->resourceTree);
        item->setText(0,QString::fromStdString(value.Name));item->setText(1,QString::fromStdString(value.SourcePath));item->setText(2,QString::number(value.Reference.LocalID));
        item->setData(0,Qt::UserRole,QString::fromStdString(value.Reference.Guid));item->setData(0,Qt::UserRole+1,QVariant::fromValue<qulonglong>(value.Reference.LocalID));
        item->setToolTip(0,QString::fromStdString(value.Reference.Guid));
        if(value.Reference==previous)_Ui->resourceTree->setCurrentItem(item);
    }
    Filter();
    SetError(candidates.empty()?"当前没有可用的此类资源，请先成功导入资源。":"");
}
void ResourcePicker::SetError(const std::string& error){_Ui->errorLabel->setText(QString::fromStdString(error));_Ui->errorLabel->setVisible(!error.empty());}
void ResourcePicker::Filter() {
    const QString query=_Ui->searchEdit->text().trimmed();
    for(int row=0;row<_Ui->resourceTree->topLevelItemCount();++row) {
        auto* item=_Ui->resourceTree->topLevelItem(row);
        item->setHidden(!query.isEmpty()&&!item->text(0).contains(query,Qt::CaseInsensitive)&&!item->text(1).contains(query,Qt::CaseInsensitive));
    }
    auto* current=_Ui->resourceTree->currentItem();
    _Ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(current&&!current->isHidden());
}
void ResourcePicker::Choose() {
    auto* item=_Ui->resourceTree->currentItem();if(!item || item->isHidden())return;
    const GameSproutRuntime::ResourceReference reference{item->data(0,Qt::UserRole).toString().toStdString(),item->data(0,Qt::UserRole+1).toULongLong()};
    const auto callback=Selected;if(callback)callback(reference);
}
