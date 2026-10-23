#include "IDEDockerOptionsButton.h"
#include "ui_IDEDockerOptionsButton.h"

IDEDockerOptionsButton::IDEDockerOptionsButton(QWidget* parent) : QToolButton(parent) {
    try {
        _Ui = new Ui::IDEDockerOptionsButton();
        _Ui->setupUi(this);
    } catch (...) {
        delete _Ui;
        throw;
    }
}

IDEDockerOptionsButton::~IDEDockerOptionsButton() {
    delete _Ui;
}
