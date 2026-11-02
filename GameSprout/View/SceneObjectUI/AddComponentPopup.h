#pragma once
#include "ComponentEditContext.h"
#include <QDialog>
namespace Ui {class AddComponentPopupForm;}
class QTreeWidgetItem;
class AddComponentPopup final : public QDialog {
public:
    AddComponentPopup(const ComponentEditContext&,QWidget* parent);
    ~AddComponentPopup() override;
    void OpenAt(QWidget* anchor);
private:
    void Filter();
    void Activate(QTreeWidgetItem*);
    struct Entry {std::string Name,Category,TypeID;GameSproutRuntime::ResourceReference Source;bool Script=false,Enabled=true;};
    ComponentEditContext _Context;
    std::vector<Entry> _Entries;
    Ui::AddComponentPopupForm* _Ui;
};
