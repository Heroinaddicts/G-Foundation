#include "IDEDockerDropOverlay.h"
#include "ui_IDEDockerDropOverlay.h"
#include <QVariant>

IDEDockerDropOverlay::IDEDockerDropOverlay(QWidget* owner)
    :QWidget(owner,Qt::ToolTip|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput),
      _Ui(new Ui::IDEDockerDropOverlayForm()) {
    _Ui->setupUi(this);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    hide();
}
IDEDockerDropOverlay::~IDEDockerDropOverlay() {delete _Ui;}
void IDEDockerDropOverlay::ShowPreview(const QRect& rectangle,DockerDropPosition position,bool root,bool reordering) {
    const char* key="tabsText";
    switch(position) {
    case DockerDropPosition::Tabs:key=reordering?"reorderText":"tabsText";break;
    case DockerDropPosition::Left:key="leftText";break;
    case DockerDropPosition::Right:key="rightText";break;
    case DockerDropPosition::Top:key="topText";break;
    case DockerDropPosition::Bottom:key="bottomText";break;
    }
    _Ui->dropHint->setText((root?property("rootPrefix").toString():QString())+property(key).toString());
    _Ui->dropHint->setVisible(!reordering);
    setProperty("dropPosition",static_cast<int>(position));setProperty("rootDrop",root);
    setGeometry(rectangle);show();raise();
}
