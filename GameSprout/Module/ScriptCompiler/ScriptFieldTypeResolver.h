#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Resolve imported/aliased type spellings through C++ type identity, including
// private/protected fields authorized by GAMESPROUT_SERIALIZE_FIELD.
class ScriptFieldTypeResolver final {
public:
    using fReadAst=std::function<bool(const QString&,std::vector<QJsonObject>&,std::string&)>;
    static bool Resolve(const QString& declarationProbe,const QString& type,
        const QStringList& members,const fReadAst& readAst,
        std::map<QString,QString>& resolved,std::string& error);
};
