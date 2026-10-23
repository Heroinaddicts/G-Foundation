#include "HubWindow.h"
#include "NewProjectDialog.h"
#include "./ui_HubWindow.h"

#include <QDir>
#include <QFileDialog>
#include <QTreeWidgetItem>

HubWindow::HubWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::HubWindow)
{
    ui->setupUi(this);

    connect(ui->newProjectButton, &QPushButton::clicked,
            this, &HubWindow::CreateProject);
    connect(ui->chooseProjectButton, &QPushButton::clicked,
            this, &HubWindow::ChooseProject);
    connect(ui->openProjectButton, &QPushButton::clicked,
            this, &HubWindow::OpenSelectedProject);
    connect(ui->projectList, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem*, int) { OpenSelectedProject(); });
    connect(ui->projectList, &QTreeWidget::itemSelectionChanged,
            this, [this]() {
                ui->openProjectButton->setEnabled(ui->projectList->currentItem() != nullptr);
            });
    connect(ui->projectList, &QTreeWidget::customContextMenuRequested,
            this, &HubWindow::ShowProjectContextMenu);
    connect(ui->removeProjectAction, &QAction::triggered,
            this, &HubWindow::RemoveProjectRecord);

    QString error;
    _Projects.Load(error);
    RefreshProjects();
    ui->messageLabel->setText(error);
    ui->messageLabel->setVisible(!error.isEmpty());
}

HubWindow::~HubWindow()
{
    delete ui;
}

void HubWindow::RefreshProjects() {
    ui->projectList->clear();
    for (const QString& root : _Projects.Projects()) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->projectList);
        item->setText(0, QDir(root).dirName());
        item->setText(1, root);
        item->setData(0, Qt::UserRole, root);
        item->setToolTip(0, root);
        item->setToolTip(1, root);
    }
    ui->emptyStateLabel->setVisible(_Projects.Projects().isEmpty());
    ui->projectsCountLabel->setText(tr("%1 个项目").arg(_Projects.Projects().size()));
    ui->openProjectButton->setEnabled(false);
}

void HubWindow::CreateProject() {
    NewProjectDialog dialog(_Projects, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    RefreshProjects();
    ui->statusbar->showMessage(tr("已创建项目：%1").arg(dialog.ProjectRoot()));
    OpenProject(dialog.ProjectRoot(), true);
    if (!dialog.Warning().isEmpty() && ui->messageLabel->text().isEmpty()) {
        ui->messageLabel->setText(dialog.Warning());
        ui->messageLabel->show();
    }
}

void HubWindow::ChooseProject() {
    const QTreeWidgetItem* selected = ui->projectList->currentItem();
    const QString initialDirectory = selected
        ? selected->data(0, Qt::UserRole).toString() : QDir::homePath();
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择 GameSprout 项目目录"), initialDirectory);
    if (!directory.isEmpty()) {
        OpenProject(directory);
    }
}

void HubWindow::OpenSelectedProject() {
    const QTreeWidgetItem* selected = ui->projectList->currentItem();
    if (selected) {
        OpenProject(selected->data(0, Qt::UserRole).toString());
    }
}

void HubWindow::ShowProjectContextMenu(const QPoint& position) {
    QTreeWidgetItem* item = ui->projectList->itemAt(position);
    if (!item) {
        return;
    }
    ui->projectList->setCurrentItem(item);
    // Capture the right-clicked record rather than depending on later selection.
    ui->removeProjectAction->setData(item->data(0, Qt::UserRole));
    ui->removeProjectAction->setEnabled(true);
    ui->projectContextMenu->popup(ui->projectList->viewport()->mapToGlobal(position));
}

void HubWindow::RemoveProjectRecord() {
    const QString root = ui->removeProjectAction->data().toString();
    if (root.isEmpty()) {
        return;
    }
    QString error;
    if (!_Projects.RemoveProjectRecord(root, error)) {
        ui->messageLabel->setText(error);
        ui->messageLabel->show();
        return;
    }
    ui->removeProjectAction->setEnabled(false);
    ui->removeProjectAction->setData(QVariant());
    RefreshProjects();
    ui->messageLabel->clear();
    ui->messageLabel->hide();
    ui->statusbar->showMessage(tr("已删除项目记录：%1（项目文件已保留）").arg(root));
}

void HubWindow::OpenProject(const QString& directory, bool isNewProject) {
    QString error;
    if (_Projects.OpenProject(directory, error, isNewProject)) {
        RefreshProjects();
        if (ui->projectList->topLevelItemCount() > 0) {
            ui->projectList->setCurrentItem(ui->projectList->topLevelItem(0));
        }
        ui->statusbar->showMessage(tr("已启动项目：%1").arg(directory));
    }
    ui->messageLabel->setText(error);
    ui->messageLabel->setVisible(!error.isEmpty());
}
