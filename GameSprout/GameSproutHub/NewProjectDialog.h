#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>

class HubProjectManager;

namespace Ui {
class NewProjectDialog;
}

class NewProjectDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewProjectDialog(HubProjectManager& projects, QWidget* parent = nullptr);
    ~NewProjectDialog() override;

    const QString& ProjectRoot() const { return _ProjectRoot; }
    const QString& Warning() const { return _Warning; }

private:
    void ChooseLocation();
    void UpdatePreview();
    void CreateProject();

    NewProjectDialog(const NewProjectDialog&) = delete;
    NewProjectDialog& operator=(const NewProjectDialog&) = delete;

    HubProjectManager& _Projects;
    Ui::NewProjectDialog* ui;
    QString _ProjectRoot;
    QString _Warning;
};

#endif // NEWPROJECTDIALOG_H
