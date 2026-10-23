#ifndef HUBPROJECTMANAGER_H
#define HUBPROJECTMANAGER_H

#include <QStringList>

class HubProjectManager {
public:
    bool Load(QString& error);

    // Creates the directory and Info/project.json, then records the project
    // even if the editor is unavailable.
    // A nonempty error on success is a history-saving warning.
    bool CreateProject(const QString& name, const QString& parentDirectory,
                       QString& projectRoot, QString& error);

    // A nonempty error on success is a history-saving warning: the editor
    // has already started and runs independently of the Hub.
    bool OpenProject(const QString& directory, QString& error, bool isNewProject = false);
    // Removes only the stored history entry; project files are never touched.
    // Keeps the in-memory entry if saving the history fails.
    bool RemoveProjectRecord(const QString& root, QString& error);
    const QStringList& Projects() const { return _Projects; }

private:
    bool RememberProject(const QString& root, QString& error);
    bool Save(QString& error) const;
    static QString HistoryPath();
    static QString EditorExecutable();

    QStringList _Projects;
    bool _CanSaveProjects = false;
};

#endif // HUBPROJECTMANAGER_H
