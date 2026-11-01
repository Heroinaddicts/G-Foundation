#include "MaterialValueStack.h"
MaterialValueStack::MaterialValueStack(QWidget * parent) : QStackedWidget(parent) {
    QObject::connect(this,&QStackedWidget::currentChanged,this,[this] { updateGeometry(); });
}
QSize MaterialValueStack::sizeHint() const { return currentWidget()?currentWidget()->sizeHint():QSize(); }
QSize MaterialValueStack::minimumSizeHint() const { return currentWidget()?currentWidget()->minimumSizeHint():QSize(); }
