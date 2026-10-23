#include "IDEDockerTitleBar.h"
#include "ui_IDEDockerTitleBar.h"

#include <QDockWidget>
#include <QMenu>

IDEDockerTitleBar::IDEDockerTitleBar(QDockWidget* dock, QMenu* menu) : QWidget(dock) {
    try {
        _Ui = new Ui::IDEDockerTitleBar();
        _Ui->setupUi(this);
        _ExpandedMinimumSize = minimumSize();
        _ExpandedMaximumSize = maximumSize();
        _Ui->DockerTitleLabel->setText(dock->windowTitle());
        // 鼠标交回 QDockWidget，保留原生标题拖拽；这是交互行为，不是外观设置。
        _Ui->DockerTitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        QObject::connect(this, &QWidget::customContextMenuRequested, menu, [this, menu](const QPoint& pos) {
            menu->popup(mapToGlobal(pos));
        });
        auto* options = _Ui->DockerOptions;
        QObject::connect(options, &QToolButton::clicked, menu, [menu, options]() {
            menu->popup(options->mapToGlobal(QPoint(0, options->height())));
        });
        QObject::connect(dock, &QWidget::windowTitleChanged, _Ui->DockerTitleLabel, &QLabel::setText);
    } catch (...) {
        delete _Ui;
        throw;
    }
}

IDEDockerTitleBar::~IDEDockerTitleBar() {
    delete _Ui;
}

void IDEDockerTitleBar::SetTabbed(bool tabbed) {
    if (_Tabbed == tabbed) {
        return;
    }
    _Tabbed = tabbed;
    if (tabbed) {
        setMinimumSize(0, 0);
        setMaximumHeight(0);
    } else {
        setMaximumSize(_ExpandedMaximumSize);
        setMinimumSize(_ExpandedMinimumSize);
    }
    updateGeometry();
}

QSize IDEDockerTitleBar::sizeHint() const {
    return _Tabbed ? QSize(0, 0) : QWidget::sizeHint().expandedTo(_ExpandedMinimumSize);
}

QSize IDEDockerTitleBar::minimumSizeHint() const {
    return _Tabbed ? QSize(0, 0) : QWidget::minimumSizeHint().expandedTo(_ExpandedMinimumSize);
}
