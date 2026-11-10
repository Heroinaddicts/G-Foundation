#include "ScriptEditor.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <set>

bool ScriptEditor::CreateScript(const std::string& relativeDirectory,const std::string& name,
                                std::string& relativePath,std::string& error) {
    return CreateSourcePair(relativeDirectory,name,"Script",relativePath,error);
}

bool ScriptEditor::CreateModule(const std::string& relativeDirectory,const std::string& name,
                                std::string& relativePath,std::string& error) {
    return CreateSourcePair(relativeDirectory,name,"Module",relativePath,error);
}

bool ScriptEditor::CreateSourcePair(const std::string& relativeDirectory,const std::string& name,
                                   const char* templateName,std::string& relativePath,std::string& error) {
    relativePath.clear();error.clear();
    const QString identifier=QString::fromStdString(name);
    static const QRegularExpression valid("\\A[A-Za-z][A-Za-z0-9_]*\\z");
    static const std::set<std::string> reserved={"class","struct","namespace","int","float","double","bool","void","return","if","else","for","while","switch","case","default","public","protected","private","virtual","override","final","new","delete","operator","template","typename","using","auto","const","static","enum","union","this","true","false","nullptr","try","catch","throw","export","import","module","concept","requires","co_await","co_yield","co_return","alignas","alignof","asm","break","char","char8_t","char16_t","char32_t","consteval","constexpr","constinit","continue","decltype","do","dynamic_cast","explicit","extern","friend","goto","inline","long","mutable","noexcept","register","reinterpret_cast","short","signed","sizeof","static_assert","static_cast","thread_local","typeid","typedef","unsigned","volatile","wchar_t","and","and_eq","bitand","bitor","compl","not","not_eq","or","or_eq","xor","xor_eq"};
    if(!valid.match(identifier).hasMatch() || name.find("__")!=std::string::npos || reserved.contains(name)) {error="名称必须是有效且非保留的 C++ 类名";return false;}
    const QString assets=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
    const QString root=QFileInfo(assets).canonicalFilePath();
    const QString relative=QDir::cleanPath(QString::fromStdString(relativeDirectory));
    const QString directory=QFileInfo(QDir(assets).filePath(relative)).canonicalFilePath();
    if(root.isEmpty() || directory.isEmpty() || !QFileInfo(directory).isDir() ||
       QDir::isAbsolutePath(relative) || (directory!=root && !directory.startsWith(root+'/'))) {error="创建目录必须在项目 Assets 内";return false;}
    const QString headerName=identifier+".h";
    const QString sourceName=identifier+".cpp";
    const QDir destination(directory);
    for(const QString& existing:destination.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot)) {
        if(existing.compare(headerName,Qt::CaseInsensitive)==0 || existing.compare(headerName+".meta",Qt::CaseInsensitive)==0 ||
           existing.compare(sourceName,Qt::CaseInsensitive)==0 || existing.compare(sourceName+".meta",Qt::CaseInsensitive)==0) {
            error="同名源码或 .meta 已存在，禁止覆盖："+existing.toStdString();
            return false;
        }
    }
    const QString templateBase=QStringLiteral(":/GameSprout/Script/")+QString::fromLatin1(templateName);
    const QByteArray className=identifier.toUtf8();
    const auto readTemplate=[&templateBase,&className,&error](const QString& suffix,QByteArray& contents) {
        QFile file(templateBase+suffix);
        if(file.open(QIODevice::ReadOnly)) {
            contents=file.readAll();
            if(file.error()==QFileDevice::NoError) {
                contents.replace("#CLASSNAME#",className);
                return true;
            }
        }
        error="源码模板无法读取："+file.fileName().toStdString();
        return false;
    };
    QByteArray header,source;
    if(!readTemplate(".h.template",header) || !readTemplate(".cpp.template",source)) return false;

    const auto removeCreated=[&error](const QString& path) {
        QFile file(path);
        if(!file.remove()) error+="；无法清理 "+path.toStdString()+"："+file.errorString().toStdString();
    };
    const auto writeNew=[&error,&removeCreated](QFile& file,const QByteArray& contents) {
        if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
            error=file.fileName().toStdString()+"："+file.errorString().toStdString();
            return false;
        }
        if(file.write(contents)!=contents.size() || !file.flush()) {
            error=file.fileName().toStdString()+"："+file.errorString().toStdString();
            file.close();
            removeCreated(file.fileName());
            return false;
        }
        file.close();
        return true;
    };
    QFile sourceFile(destination.filePath(sourceName));
    QFile headerFile(destination.filePath(headerName));
    // Publish the registration header once its implementation exists.
    if(!writeNew(sourceFile,source)) return false;
    if(!writeNew(headerFile,header)) {
        removeCreated(sourceFile.fileName());
        return false;
    }
    relativePath=QDir(root).relativeFilePath(headerFile.fileName()).toStdString();
    return true;
}

// Assembly identity is independent of source .meta GUIDs and remains stable across builds.
bool ScriptEditor::PrepareAssembly(std::string& error) {
    if(!_AssemblyGuid.empty()) return true;
    const QString info=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Info");
    const QString path=QDir(info).filePath("GameLogic.json");
    if(QFileInfo::exists(path)) {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)) {error="Cannot read GameLogic assembly identity";return false;}
        const QJsonDocument document=QJsonDocument::fromJson(file.readAll());
        const std::string guid=document.object().value("Guid").toString().toStdString();
        if(!document.isObject() || document.object().value("Version").toInt()!=1 || !Guid(guid).IsValid()) {
            error="Info/GameLogic.json is not the current assembly format";return false;
        }
        _AssemblyGuid=guid;return true;
    }
    const Guid guid=_Guids->CreateGuid();
    if(!guid.IsValid() || !QDir().mkpath(info)) {error="Cannot create GameLogic assembly identity";return false;}
    const QByteArray bytes=QJsonDocument(QJsonObject{{"Version",1},{"Guid",QString::fromStdString(guid.GetGuidString())}}).toJson();
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        error="Cannot save Info/GameLogic.json: "+file.errorString().toStdString();return false;
    }
    _AssemblyGuid=guid.GetGuidString();return true;
}
