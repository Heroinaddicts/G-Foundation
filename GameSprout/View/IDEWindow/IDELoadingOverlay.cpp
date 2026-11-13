#include "IDELoadingOverlay.h"
#include "ui_IDELoadingOverlay.h"

#include <QApplication>
#include <QEvent>
#include <QMainWindow>
#include <QMenuBar>

IDELoadingOverlay::IDELoadingOverlay(QMainWindow* window)
    : QFrame(window), _Window(window) {
    try {
        _Ui = new Ui::IDELoadingOverlay();
        _Ui->setupUi(this);
        hide();
        setGeometry(_Window->rect());
        _Window->installEventFilter(this);
        _ShowTimer.setSingleShot(true);
        _ShowTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&_ShowTimer, &QTimer::timeout, this, &IDELoadingOverlay::ShowLoading);
    } catch (...) {
        delete _Ui;
        throw;
    }
}

IDELoadingOverlay::~IDELoadingOverlay() {
    _ShowTimer.stop();
    if (auto* application = QCoreApplication::instance()) {
        application->removeEventFilter(this);
    }
    QObject::disconnect(_FocusDestroyed);
    delete _Ui;
}

void IDELoadingOverlay::Begin(const QString& title) {
    if (_Active) {
        return;
    }
    _Ui->titleLabel->setText(title);
    _MenuWasEnabled = _Window->menuBar()->isEnabled();
    _Window->menuBar()->setEnabled(false); // Also blocks the native macOS menu.
    _Active = true;
    qApp->installEventFilter(this);
    _ShowTimer.start(100);
}

void IDELoadingOverlay::ShowLoading() {
    if (!_Active) {
        return;
    }
    _PreviousFocus = QApplication::focusWidget();
    if (_PreviousFocus) {
        _FocusDestroyed = QObject::connect(_PreviousFocus, &QObject::destroyed, this,
                                          [this]() { _PreviousFocus = nullptr; });
    }
    setGeometry(_Window->rect());
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

void IDELoadingOverlay::SetDescription(const QString& description) {
    _Ui->descriptionLabel->setText(description);
}

void IDELoadingOverlay::SetTitle(const QString& title) {
    _Ui->titleLabel->setText(title);
}

void IDELoadingOverlay::SetProgress(uint64_t completed, uint64_t total) {
    _Ui->progressBar->setRange(0, 100);
    int percentage = total > 0 ? int(static_cast<long double>(completed) * 100 / total) : 0;
    if (completed < total && percentage >= 100) percentage = 99;
    _Ui->progressBar->setValue(percentage);
    _Ui->completedLabel->setText(tr("已完成 %1 / %2").arg(qulonglong(completed)).arg(qulonglong(total)));
    if (!isHidden()) {
        raise();
    }
}

void IDELoadingOverlay::SetIndeterminateProgress() {
    _Ui->progressBar->setRange(0, 0);
    _Ui->completedLabel->setText(tr("正在处理…"));
}

void IDELoadingOverlay::End() {
    if (!_Active) {
        return;
    }
    _Active = false;
    _ShowTimer.stop();
    qApp->removeEventFilter(this);
    hide();
    _Window->menuBar()->setEnabled(_MenuWasEnabled);
    QObject::disconnect(_FocusDestroyed);
    QWidget* previous = _PreviousFocus;
    _PreviousFocus = nullptr;
    if (previous && previous->isVisible() && previous->isEnabled()) {
        previous->setFocus(Qt::OtherFocusReason);
    }
}

bool IDELoadingOverlay::BelongsToWindow(QObject* object) const {
    for (; object; object = object->parent()) {
        if (object == _Window) {
            return true;
        }
    }
    return false;
}

bool IDELoadingOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _Window && event->type() == QEvent::Resize) {
        setGeometry(_Window->rect());
    }
    if (!_Active || !BelongsToWindow(watched)) {
        return QFrame::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::ShortcutOverride:
    case QEvent::Shortcut:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::InputMethod:
        event->accept();
        return true;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::ContextMenu:
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
        // Include floating docks and menus belonging to this editor window.
        if (watched != this && !isAncestorOf(qobject_cast<QWidget*>(watched))) {
            event->accept();
            return true;
        }
        break;
    case QEvent::ChildAdded:
    case QEvent::Show:
    case QEvent::LayoutRequest:
        if (watched == _Window && !isHidden()) {
            raise();
        }
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}
