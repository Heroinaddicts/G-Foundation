#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
QString SessionFile(IProject* project) {
    return project&&!project->GetProjectRoot().empty()
        ?QDir(QString::fromStdString(project->GetProjectRoot())).filePath("UserSettings/EditorState.json"):QString{};
}
bool ReadSession(const QString& path,Json::Value& document) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>256*1024)return false;
    const QByteArray bytes=file.readAll();
    return file.error()==QFileDevice::NoError&&bytes.size()<=256*1024
        &&AssetArtifactIO::Parse(bytes.toStdString(),document)&&document.isObject();
}
}

bool SceneManager::RestoreLastScene() {
    const QString filename=SessionFile(_Project);
    if(filename.isEmpty()||!QFileInfo::exists(filename))return false;
    Json::Value document;
    if(!ReadSession(filename,document)||!document["LastScenePath"].isString()) {
        qWarning().noquote()<<"无法读取项目的最近场景记录，将打开默认场景。";return false;
    }
    const std::string path=document["LastScenePath"].asString();
    if(path.empty())return false;
    std::string error;
    // Use the normal loader so restored paths receive exactly the same Assets
    // boundary, format and transactional validation as a manual scene open.
    if(OpenScene(path,error))return true;
    qWarning().noquote()<<"无法恢复上次场景，将打开默认场景："<<QString::fromStdString(path)<<QString::fromStdString(error);
    return false;
}

void SceneManager::RememberActiveScene() const {
    const QString filename=SessionFile(_Project);
    if(filename.isEmpty())return;
    Json::Value document(Json::objectValue);
    Json::Value existing;
    if(ReadSession(filename,existing)) {
        if(existing["LastScenePath"].isString()&&existing["LastScenePath"].asString()==_ActivePath)return;
        document=std::move(existing);
    }
    document["LastScenePath"]=_ActivePath;
    const QDir project(QString::fromStdString(_Project->GetProjectRoot()));
    if(!project.exists()||!project.mkpath("UserSettings")) {
        qWarning().noquote()<<"无法创建项目 UserSettings 目录，最近场景未记录。";return;
    }
    const QByteArray bytes=QByteArray::fromStdString(AssetArtifactIO::JsonText(document));
    QSaveFile file(filename);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)) {
        qWarning().noquote()<<"无法记录最近场景："<<file.errorString();return;
    }
    if(file.write(bytes)!=bytes.size()) {
        qWarning().noquote()<<"无法记录最近场景："<<file.errorString();file.cancelWriting();return;
    }
    if(!file.commit())qWarning().noquote()<<"无法记录最近场景："<<file.errorString();
}
