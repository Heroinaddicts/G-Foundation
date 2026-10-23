#include "NewProjectDialog.h"
#include "HubProjectManager.h"
#include "ui_NewProjectDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>

NewProjectDialog::NewProjectDialog(HubProjectManager& projects, QWidget* parent)
    : QDialog(parent), _Projects(projects), ui(new Ui::NewProjectDialog) {
    ui->setupUi(this);
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    ui->locationEdit->setText(QDir(documents).exists() ? documents : QDir::homePath());

    connect(ui->browseButton, &QPushButton::clicked, this, &NewProjectDialog::ChooseLocation);
    connect(ui->createButton, &QPushButton::clicked, this, &NewProjectDialog::CreateProject);
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->projectNameEdit, &QLineEdit::textChanged, this, &NewProjectDialog::UpdatePreview);
    connect(ui->locationEdit, &QLineEdit::textChanged, this, &NewProjectDialog::UpdatePreview);
    UpdatePreview();
}

NewProjectDialog::~NewProjectDialog() {
    delete ui;
}

void NewProjectDialog::ChooseLocation() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择项目保存位置"), ui->locationEdit->text());
    if (!directory.isEmpty()) {
        ui->locationEdit->setText(directory);
    }
}

void NewProjectDialog::UpdatePreview() {
    const QString name = ui->projectNameEdit->text().trimmed();
    ui->projectPathLabel->setText(QDir::toNativeSeparators(
        QDir(ui->locationEdit->text()).filePath(name)));
    ui->createButton->setEnabled(!name.isEmpty() && !ui->locationEdit->text().isEmpty());
    ui->errorLabel->clear();
    ui->errorLabel->hide();
}

void NewProjectDialog::CreateProject() {
    QString error;
    if (!_Projects.CreateProject(ui->projectNameEdit->text(), ui->locationEdit->text(),
                                 _ProjectRoot, error)) {
        ui->errorLabel->setText(error);
        ui->errorLabel->show();
        return;
    }
    _Warning = error;
    accept();
}
