#include "ConsoleFilterPopup.h"
#include "ui_ConsoleFilterPopup.h"
#include <QScreen>

ConsoleFilterPopup::ConsoleFilterPopup(QWidget* parent) : QWidget(parent, Qt::Popup) {
    try {
        _Ui = new Ui::ConsoleFilterPopup();
        _Ui->setupUi(this);
    } catch (...) {
        delete _Ui;
        throw;
    }
}

ConsoleFilterPopup::~ConsoleFilterPopup() {
    delete _Ui;
}

QCheckBox* ConsoleFilterPopup::LevelCheckBox(eConsoleLog level) const {
    switch (level) {
    case eConsoleLog::Debug: return _Ui->debugCheckBox;
    case eConsoleLog::Trace: return _Ui->traceCheckBox;
    case eConsoleLog::Warning: return _Ui->warningCheckBox;
    case eConsoleLog::Error: return _Ui->errorCheckBox;
    }
    return nullptr;
}

void ConsoleFilterPopup::ShowBelow(QWidget* anchor) {
    adjustSize();
    QPoint position = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height()));
    const QRect available = anchor->screen()->availableGeometry();
    if (position.y() + height() > available.bottom() + 1) {
        position.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height());
    }
    position.setX(qBound(available.left(), position.x(), qMax(available.left(), available.right() - width() + 1)));
    position.setY(qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - height() + 1)));
    move(position);
    show();
    _Ui->debugCheckBox->setFocus(Qt::PopupFocusReason);
}
