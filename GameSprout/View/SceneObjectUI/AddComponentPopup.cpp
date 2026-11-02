#include "AddComponentPopup.h"
#include "IScriptEditor.h"
#include "ui_AddComponentPopup.h"
#include <QGuiApplication>
#include <QScreen>
#include <QKeyEvent>
#include <QMap>
AddComponentPopup::AddComponentPopup(const ComponentEditContext& context,QWidget* parent)
    : QDialog(parent,Qt::Popup),_Context(context),_Ui(new Ui::AddComponentPopupForm) {
    _Ui->setupUi(this);setAttribute(Qt::WA_DeleteOnClose);
    auto* object=_Context.Resolve();
    if (_Context.Runtime && object) for (const auto& type:_Context.Runtime->GetBuiltinComponentTypes()) {
        if (type.Intrinsic) continue;
        bool exists=false;
        for (std::size_t i=0;i<object->GetComponentCount();++i) {
            auto* component=object->GetComponentAt(i);
            if (component && component->GetTypeName() && type.TypeID==component->GetTypeName()) {exists=true;break;}
        }
        _Entries.push_back({type.Name,type.Category,type.TypeID,{},false,type.AllowMultiple||!exists});
    }
    if (_Context.Scripts) {
        for (const auto& type:_Context.Scripts->GetScriptTypes()) {
            bool exists=false;
            if(object)for(std::size_t i=0;i<object->GetComponentCount();++i)
                if(const auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object->GetComponentAt(i));
                   script && script->GetScriptTypeID()==type.Type.TypeID){exists=true;break;}
            _Entries.push_back({type.Type.Name,"用户脚本",type.Type.TypeID,type.Source,true,!exists});
        }
        const auto state=_Context.Scripts->GetBuildState();
        if (state.Compiling) _Ui->statusLabel->setText(tr("脚本正在编译，列表显示上次成功编译的类型。"));
        else if (!state.Error.empty()) _Ui->statusLabel->setText(tr("脚本编译失败；当前仅列出上次成功编译的类型。"));
    }
    QObject::connect(_Ui->searchEdit,&QLineEdit::textChanged,this,[this]{Filter();});
    QObject::connect(_Ui->searchEdit,&QLineEdit::returnPressed,this,[this]{Activate(_Ui->componentTree->currentItem());});
    QObject::connect(_Ui->componentTree,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem* item,int){Activate(item);});
    QObject::connect(_Ui->componentTree,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem* item,int){Activate(item);});
    Filter();
}
AddComponentPopup::~AddComponentPopup(){delete _Ui;}
void AddComponentPopup::OpenAt(QWidget* anchor) {
    const QPoint below=anchor->mapToGlobal(QPoint(0,anchor->height()));
    auto* screen=QGuiApplication::screenAt(below);if (!screen)screen=QGuiApplication::primaryScreen();
    const QRect available=screen?screen->availableGeometry():QRect(below,QSize(400,400));
    resize(qMin(qMax(300,anchor->width()),available.width()),qMin(380,available.height()));
    QPoint origin=below;
    if(origin.y()+height()>available.bottom()) origin.setY(anchor->mapToGlobal(QPoint()).y()-height());
    origin.setX(qBound(available.left(),origin.x(),available.right()-width()+1));
    origin.setY(qBound(available.top(),origin.y(),available.bottom()-height()+1));
    move(origin);show();_Ui->searchEdit->setFocus();
}
void AddComponentPopup::Filter(){
    const auto words=_Ui->searchEdit->text().simplified().split(' ',Qt::SkipEmptyParts);
    _Ui->componentTree->clear();QMap<QString,QTreeWidgetItem*> groups;
    QTreeWidgetItem* first=nullptr;
    for (std::size_t i=0;i<_Entries.size();++i) {
        const auto& entry=_Entries[i];const QString name=QString::fromStdString(entry.Name);
        const QString category=QString::fromStdString(entry.Category);
        const QString searchable=name+" "+category+" "+QString::fromStdString(entry.TypeID);
        bool matches=true;for(const auto& word:words)if(!searchable.contains(word,Qt::CaseInsensitive)){matches=false;break;}
        if(!matches)continue;
        QTreeWidgetItem* group=groups.value(category,nullptr);
        if(!group){group=new QTreeWidgetItem(_Ui->componentTree,{category});group->setFlags(Qt::ItemIsEnabled);group->setExpanded(true);groups.insert(category,group);}
        auto* item=new QTreeWidgetItem(group,{entry.Enabled?name:name+tr("（已添加）")});
        item->setData(0,Qt::UserRole,qulonglong(i));item->setToolTip(0,QString::fromStdString(entry.TypeID));
        if(!entry.Enabled)item->setDisabled(true);else if(!first)first=item;
    }
    if(first)_Ui->componentTree->setCurrentItem(first);
}
void AddComponentPopup::Activate(QTreeWidgetItem* item){
    if(!item||!item->parent()||item->isDisabled())return;
    const auto index=item->data(0,Qt::UserRole).toULongLong();if(index>=_Entries.size())return;
    const auto entry=_Entries[index];std::string error;
    auto* object=_Context.Resolve();
    if(!object||object==object->GetScene()||!_Context.Scenes){_Ui->statusLabel->setText(tr("对象已失效，关闭后重新选择。"));return;}
    if(entry.Script) {
        bool available=false;
        if(_Context.Scripts)for(const auto& current:_Context.Scripts->GetScriptTypes())
            if(current.Type.TypeID==entry.TypeID && current.Source==entry.Source){available=true;break;}
        if(!available){_Ui->statusLabel->setText(tr("脚本目录已更新，此类型已不可用。请关闭后重新选择。"));return;}
    }
    const bool ok=entry.Script?_Context.Scenes->AttachScriptComponent(_Context.ObjectID,entry.TypeID,entry.Source,error)
        :_Context.Scenes->AddBuiltinComponent(_Context.ObjectID,entry.TypeID,error);
    if(!ok){_Ui->statusLabel->setText(QString::fromStdString(error));if(_Context.Report)_Context.Report(error);return;}
    accept();
}
