#include "SceneManager.h"
#include "SceneDocument.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
bool DiskMatches(const QString& path,const std::string& expected,std::string& error) {
    QFile source(path);
    if(expected.empty() || !source.open(QIODevice::ReadOnly) || source.size()>32*1024*1024) {
        error="磁盘场景已消失或无法核对，未覆盖。请重新打开场景或另存为新文件。";return false;
    }
    const QByteArray bytes=source.readAll();
    if(source.error()!=QFileDevice::NoError || bytes.size()>32*1024*1024 || AssetArtifactIO::Hash(bytes)!=expected) {
        error="场景文件已被外部修改，未覆盖。请重新打开场景或另存为新文件。";return false;
    }
    return true;
}
bool Resolve(IProject* project,const std::string& relative,bool directory,QString& output,std::string& error,bool allowMissing=false) {
    if(!project || project->GetProjectRoot().empty()) {error="项目尚未打开。";return false;}
    QString path=QString::fromStdString(relative);
    path.replace('\\','/');
    if(QDir::isAbsolutePath(path) || path.contains(':') || path.contains(QChar(0))) {error="需要相对于 Assets 的有效路径。";return false;}
    path=QDir::cleanPath(path);
    if(path==".." || path.startsWith("../")) {error="路径不能超出 Assets。";return false;}
    if(path==".") path.clear();
    const QString assets=QDir(QString::fromStdString(project->GetProjectRoot())).filePath("Assets");
    const QFileInfo root(assets);
    if(!root.isDir() || root.isSymLink()) {error="项目 Assets 目录不存在或为符号链接。";return false;}
    QString current=root.canonicalFilePath();
    const QString canonicalRoot=current;
    for(const QString& part:path.split('/',Qt::SkipEmptyParts)) {
        current=QDir(current).filePath(part);
        if(QFileInfo(current).isSymLink()) {error="场景路径不能经过符号链接。";return false;}
    }
    const QFileInfo target(current);
    if(!target.exists() && allowMissing && !directory) {
        const QFileInfo parent(target.absolutePath());
        if(!parent.isDir() || parent.isSymLink()) {error="场景所在目录不存在。";return false;}
        const QString canonicalParent=parent.canonicalFilePath();
        if(canonicalParent!=canonicalRoot && !canonicalParent.startsWith(canonicalRoot+'/')) {error="路径不能超出 Assets。";return false;}
        output=QDir(canonicalParent).filePath(target.fileName());return true;
    }
    if(!target.exists() || (directory?!target.isDir():!target.isFile())) {error=directory?"目标文件夹不存在。":"场景文件不存在。";return false;}
    const QString canonical=target.canonicalFilePath();
    if(canonical!=canonicalRoot && !canonical.startsWith(canonicalRoot+'/')) {error="路径不能超出 Assets。";return false;}
    output=canonical;return true;
}
}

bool SceneManager::CreateSceneAsset(const std::string& directory,const std::string& name,std::string& outPath,std::string& error) {
    error.clear();outPath.clear();
    QString folder;
    if(!Resolve(_Project,directory,true,folder,error)) return false;
    QString stem=QString::fromStdString(name).trimmed();
    if(stem.endsWith(".scene",Qt::CaseInsensitive)) stem.chop(6);
    const QRegularExpression illegal(R"([<>:"/\\|?*\x00-\x1f])");
    const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))",QRegularExpression::CaseInsensitiveOption);
    if(stem.isEmpty() || stem=="." || stem==".." || stem.endsWith('.') || stem.endsWith(' ') ||
       illegal.match(stem).hasMatch() || reserved.match(stem).hasMatch()) {
        error="场景名称无效，请使用普通文件名，不要包含路径或特殊字符。";return false;
    }
    const QString filename=QDir(folder).filePath(stem+".scene");
    const QByteArray bytes=QByteArray::fromStdString(AssetArtifactIO::JsonText(SceneDocument::CreateDefault(stem.toStdString())));
    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
        error=QFileInfo::exists(filename)?"同名场景已存在，不能覆盖。":file.errorString().toStdString();return false;
    }
    if(file.write(bytes)!=bytes.size() || !file.flush()) {
        error=file.errorString().toStdString();file.close();file.remove();return false;
    }
    file.close();
    const QString assetsPath=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
    const QDir assets(QFileInfo(assetsPath).canonicalFilePath());
    outPath=assets.relativeFilePath(filename).toStdString();
    return true;
}

bool SceneManager::OpenScene(const std::string& path,std::string& error) {
    error.clear();
    if(!_Runtime || _Switching || _ExecutingEdit || _Notifying || _TransformEdit.Active) {error="请先结束场景编辑，场景服务当前不能加载其他场景。";return false;}
    if(_Runtime->IsExecuting()) {error="Runtime 正在执行更新、渲染、遍历或事件回调，不能同步加载场景。";return false;}
    QString source;
    if(!Resolve(_Project,path,false,source,error)) return false;
    if(QFileInfo(source).suffix().compare("scene",Qt::CaseInsensitive)!=0) {error="请选择 .scene 场景文件。";return false;}
    QFile file(source);
    if(!file.open(QIODevice::ReadOnly)) {error=file.errorString().toStdString();return false;}
    if(file.size()>32*1024*1024) {error="场景文件超过当前 32 MiB 读取上限。";return false;}
    const QByteArray bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError) {error=file.errorString().toStdString();return false;}
    if(bytes.size()>32*1024*1024) {error="场景文件超过当前 32 MiB 读取上限。";return false;}
    Json::Value document;
    if(!AssetArtifactIO::Parse(bytes.toStdString(),document)) {error="场景 JSON 格式无效。";return false;}
    _Switching=true;
    GameSproutRuntime::IScene* candidate=nullptr;
    SceneDocument::ObjectIDs ids;
    bool success=false;
    try {
        success=SceneDocument::Instantiate(document,*_Runtime,candidate,ids,error);
        if(success) {
            const QString assetsPath=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
            const QDir assets(QFileInfo(assetsPath).canonicalFilePath());
            Activate(candidate,assets.relativeFilePath(source).toStdString(),std::move(ids),AssetArtifactIO::Hash(bytes));
        }
    } catch(const std::exception& exception) {error=exception.what();success=false;}
    catch(...) {error="场景加载失败。";success=false;}
    if(!success && candidate && candidate!=GetActiveScene()) _Runtime->DestroyScene(candidate);
    _Switching=false;
    return success;
}

bool SceneManager::SaveScene(std::string& error) {
    if (!Ready(error)) return false;
    if (_ActivePath.empty()) {error="尚未指定场景路径，请先另存为 .scene 文件。";return false;}
    return SaveTo(_ActivePath,false,error);
}
bool SceneManager::SaveSceneAs(const std::string& relativePath,std::string& error) {
    if (!Ready(error)) return false;
    return SaveTo(relativePath,true,error);
}
bool SceneManager::SaveTo(const std::string& path,bool allowNew,std::string& error) {
    QString target;
    if (!Resolve(_Project,path,false,target,error,true)) return false;
    const QFileInfo destination(target);
    if (destination.suffix().compare("scene",Qt::CaseInsensitive)!=0) {error="场景文件必须使用 .scene 后缀。";return false;}
    const QRegularExpression illegal(R"([<>:"/\\|?*\x00-\x1f])");
    const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))",QRegularExpression::CaseInsensitiveOption);
    if (destination.completeBaseName().isEmpty() || illegal.match(destination.fileName()).hasMatch() ||
        reserved.match(destination.fileName()).hasMatch() || destination.fileName().endsWith(' ') || destination.fileName().endsWith('.')) {
        error="场景文件名无效。";return false;
    }
    const QString assetsPath=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
    const QDir assets(QFileInfo(assetsPath).canonicalFilePath());
    const std::string normalized=assets.relativeFilePath(target).toStdString();
    const bool exists=destination.exists();
    const bool currentPath=normalized==_ActivePath;
    if (!allowNew && !exists) {error="磁盘场景已消失，未重新创建。请重新打开场景或另存为新文件。";return false;}
    if (exists && normalized!=_ActivePath) {error="目标场景已存在，不能静默覆盖另一个场景。";return false;}
    if (currentPath && !DiskMatches(target,_DiskHash,error)) return false;
    Json::Value document;std::string text;
    if (!Capture(document,text,error)) return false;
    const QByteArray bytes=QByteArray::fromStdString(text);
    if (bytes.size()>32*1024*1024) {error="场景数据超过当前 32 MiB 文件上限。";return false;}
    // 在提交文件前准备所有拥有内存的状态，提交成功后的替换不再分配字符串。
    std::string savedPath=normalized,savedBaseline=text,savedState=text,savedHash=AssetArtifactIO::Hash(bytes);
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {error=file.errorString().toStdString();return false;}
    if (file.write(bytes)!=bytes.size()) {error=file.errorString().toStdString();file.cancelWriting();return false;}
    // 另存为只允许创建新文件或覆盖当前场景，提交前再次检查外部创建/符号链接。
    QString checked;
    if (!Resolve(_Project,path,false,checked,error,true) || checked!=target ||
        (!exists && QFileInfo::exists(target))) {
        if (error.empty()) error="保存期间目标路径发生变化，未覆盖。";
        file.cancelWriting();return false;
    }
    if (currentPath && !DiskMatches(target,_DiskHash,error)) {file.cancelWriting();return false;}
    if (!file.commit()) {error=file.errorString().toStdString();return false;}
    _ActivePath.swap(savedPath);_BaselineText.swap(savedBaseline);_LastStateText.swap(savedState);
    _DiskHash.swap(savedHash);
    _Unsaved=false;_StatePending=false;
    NotifyStateChanged();
    return true;
}
