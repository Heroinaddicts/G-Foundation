#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <string>

// Project-script declarations only. This is a source contract check, not a
// replacement for C++ type checking or Runtime object-lifetime validation.
class ScriptDeclarationValidator final {
public:
    using fLoadRecord = std::function<bool(const QString&,QJsonObject&,std::string&)>;
    static bool Validate(const QString& scriptType,const QString& fallbackSource,
                         const fLoadRecord& loadRecord,std::string& error);
    // Clang JSON omits overridden-method relationships. The textual AST exposes
    // semantic Overrides entries even for implicit overrides and type aliases.
    static void MarkSemanticOverrides(QJsonObject& record,const QByteArray& textDump);
};
