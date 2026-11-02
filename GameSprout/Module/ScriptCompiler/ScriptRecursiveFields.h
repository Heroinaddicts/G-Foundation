#pragma once
#include "ScriptBindingsGenerator.h"
#include <QJsonObject>
#include <QString>
#include <functional>
#include <map>
#include <set>

// Offline field reflection. It reads Clang declarations and literal initializers;
// no gameplay constructor or native module is executed to discover a default.
class ScriptRecursiveFields final {
public:
    using fLoadDeclaration=std::function<bool(const QString&,QJsonObject&,std::string&)>;
    using fResolveFields=std::function<bool(QJsonObject&,const QString&,std::string&)>;
    using fDefaultValue=std::function<bool(const QJsonObject&,GameSproutRuntime::ScriptValue&)>;
    using fMarked=std::function<bool(const QJsonObject&,const QJsonObject&)>;
    ScriptRecursiveFields(fLoadDeclaration load,fResolveFields resolve,fDefaultValue scalar,fMarked marked,
        std::map<QString,QString> scriptTypes={});
    bool Generate(const QJsonObject& record,const QString& type,const QString& source,
        QByteArray& generated,std::vector<GameSproutRuntime::ScriptFieldInfo>& fields,
        std::string& warnings,std::string& error);
private:
    struct Member {QString Owner;QJsonObject Field;};
    bool Reflect(const QString& type,const QJsonObject& initializer,
        GameSproutRuntime::ScriptFieldInfo& info,QByteArray& generated,std::string& error,int depth,bool schemaOnly=false);
    bool Record(const QString& type,QJsonObject& record,std::string& error);
    bool Members(const QString& type,const QJsonObject& record,std::vector<Member>& members,std::string& error);
    bool RecordValue(const QString& type,const QJsonObject& initializer,GameSproutRuntime::ScriptFieldInfo& info,
        QByteArray& generated,std::string& error,int depth,bool schemaOnly);
    bool EnumValue(const QString& type,const QJsonObject& record,const QJsonObject& initializer,
        GameSproutRuntime::ScriptFieldInfo& info,std::string& error,bool schemaOnly);
    fLoadDeclaration _Load;
    fResolveFields _Resolve;
    fDefaultValue _Scalar;
    fMarked _Marked;
    std::map<QString,QJsonObject> _Records;
    std::map<QString,QString> _ScriptTypes;
    std::set<QString> _Visiting,_Generated;
};
