#include "LayerMaskCombo.h"
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStandardItemModel>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <QStringList>

LayerMaskCombo::LayerMaskCombo(QWidget* parent):QComboBox(parent),_Items(new QStandardItemModel(this)) {
    setModel(_Items);setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);setMinimumContentsLength(1);
    view()->viewport()->installEventFilter(this);view()->installEventFilter(this);
}
void LayerMaskCombo::SetLayers(const std::array<std::string,32>& layers) {
    if(_Layers==layers&&_Items->rowCount())return;
    _Layers=layers;_Items->clear();
    _Items->appendRow(new QStandardItem(QStringLiteral("Nothing")));
    _Items->appendRow(new QStandardItem(QStringLiteral("Everything")));
    for(std::uint8_t layer=0;layer<32;++layer)if(!_Layers[layer].empty()) {
        auto* item=new QStandardItem(QString::fromStdString(_Layers[layer]));
        item->setData(layer,Qt::UserRole);item->setCheckable(true);item->setEditable(false);
        _Items->appendRow(item);
    }
    RefreshChecks();
}
void LayerMaskCombo::SetMask(std::uint32_t mask){_Mask=mask;RefreshChecks();}
void LayerMaskCombo::RefreshChecks() {
    for(int row=2;row<_Items->rowCount();++row){auto* item=_Items->item(row);item->setCheckState(_Mask&(1u<<item->data(Qt::UserRole).toUInt())?Qt::Checked:Qt::Unchecked);}
    setToolTip(Summary());update();
}
QString LayerMaskCombo::Summary() const {
    if(!_Mask)return QStringLiteral("Nothing");if(_Mask==0xFFFFFFFFu)return QStringLiteral("Everything");
    QStringList names;std::uint32_t unnamed=0;
    for(std::uint8_t layer=0;layer<32;++layer)if(_Mask&(1u<<layer)) {
        if(_Layers[layer].empty())++unnamed;else names.push_back(QString::fromStdString(_Layers[layer]));
    }
    if(unnamed)names.push_back(QStringLiteral("%1 个未命名层").arg(unnamed));
    return names.join(QStringLiteral(", "));
}
void LayerMaskCombo::Toggle(int row) {
    if(!isEnabled()||row<0||row>=_Items->rowCount())return;
    auto mask=_Mask;
    if(row==0)mask=0;else if(row==1)mask=0xFFFFFFFFu;
    else mask^=1u<<_Items->item(row)->data(Qt::UserRole).toUInt();
    if(mask==_Mask)return;SetMask(mask);if(MaskChanged)MaskChanged(mask);
}
bool LayerMaskCombo::eventFilter(QObject* object,QEvent* event) {
    if(object==view()->viewport()&&event->type()==QEvent::MouseButtonRelease) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton){Toggle(view()->indexAt(mouse->position().toPoint()).row());return true;}
    }
    if(object==view()&&event->type()==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Space||key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){Toggle(view()->currentIndex().row());return true;}
    }
    return QComboBox::eventFilter(object,event);
}
void LayerMaskCombo::paintEvent(QPaintEvent*) {
    QStylePainter painter(this);QStyleOptionComboBox option;initStyleOption(&option);
    option.currentText=fontMetrics().elidedText(Summary(),Qt::ElideRight,std::max(0,width()-30));option.currentIcon={};
    painter.drawComplexControl(QStyle::CC_ComboBox,option);painter.drawControl(QStyle::CE_ComboBoxLabel,option);
}
