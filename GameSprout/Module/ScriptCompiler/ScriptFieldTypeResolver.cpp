#include "ScriptFieldTypeResolver.h"
#include "ScriptBuildStorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace {
QString Literal(QString value) {
    value.replace('\\',"\\\\");value.replace('"',"\\\"");return '"'+value+'"';
}
QString Identity(const QJsonObject& node) {
    const QString kind=node.value("kind").toString();const auto children=node.value("inner").toArray();
    if(kind=="PointerType"||kind=="LValueReferenceType"||kind=="RValueReferenceType") {
        if(children.size()!=1)return {};
        const QString base=Identity(children.first().toObject());
        return base.isEmpty()?QString():base+(kind=="PointerType"?" *":kind=="LValueReferenceType"?" &":" &&");
    }
    if(kind=="ConstantArrayType"&&children.size()==1) {
        QString element=Identity(children.first().toObject());const qsizetype index=element.indexOf('[');
        element.insert(index<0?element.size():index,'['+QString::number(node.value("size").toInteger())+']');return element;
    }
    if(kind=="QualType"&&children.size()==1)return node.value("qualifiers").toString()+' '+Identity(children.first().toObject());
    if(kind=="TemplateSpecializationType") {
        QStringList arguments;
        for(const auto& entry:children) {
            const auto argument=entry.toObject();if(argument.value("kind").toString()!="TemplateArgument")continue;
            if(argument.contains("value")) {
                const auto value=argument.value("value");arguments.push_back(value.isString()?value.toString():QString::number(value.toInteger()));
            } else if(argument.value("isExpr").toBool()) {
                const auto parts=argument.value("inner").toArray();
                if(parts.isEmpty()||!parts.first().toObject().value("value").isString())return {};
                arguments.push_back(parts.first().toObject().value("value").toString());
            } else {
                QString identity;
                for(const auto& part:argument.value("inner").toArray())if(!Identity(part.toObject()).isEmpty()){identity=Identity(part.toObject());break;}
                if(identity.isEmpty()) {const auto type=argument.value("type").toObject();identity=type.value("desugaredQualType").toString(type.value("qualType").toString());}
                if(identity.isEmpty())return {};arguments.push_back(identity);
            }
        }
        QString name=node.value("templateName").toString();
        // Clang may spell a templateName relative to its declaration namespace
        // (basic_string); the instantiated RecordType supplies its full scope.
        for(const auto& entry:children) {
            const auto record=entry.toObject();if(record.value("kind").toString()!="RecordType")continue;
            const QString qualified=record.value("type").toObject().value("qualType").toString();
            if(qualified.contains('<'))name=qualified.left(qualified.indexOf('<'));
        }
        if(!name.isEmpty()&&!arguments.isEmpty())return name+'<'+arguments.join(", ")+'>';
    }
    if(kind=="DecltypeType"||kind=="TypeAliasDecl"||kind=="ElaboratedType"||kind=="TypedefType"||kind=="UsingType"||kind=="ParenType"||kind=="AttributedType"||kind=="SubstTemplateTypeParmType") {
        for(qsizetype index=children.size();index>0;--index) {
            const auto child=children[index-1].toObject();
            if(!child.value("kind").toString().endsWith("Type"))continue;
            QString resolved=Identity(child);
            if(!resolved.isEmpty()) {
                const QString qualifier=node.value("qualifier").toString();
                const QString head=resolved.left(resolved.indexOf('<')<0?resolved.size():resolved.indexOf('<'));
                if(kind=="ElaboratedType"&&!qualifier.isEmpty()&&resolved.contains('<')&&!head.contains("::"))resolved.prepend(qualifier);
                return resolved;
            }
        }
    }
    const auto type=node.value("type").toObject();
    return type.value("desugaredQualType").toString(type.value("qualType").toString());
}
struct ProbeLifetime {
    QString Path;
    ~ProbeLifetime(){QFile::remove(Path);}
};
}

bool ScriptFieldTypeResolver::Resolve(const QString& declarationProbe,const QString& type,
    const QStringList& members,const fReadAst& readAst,
    std::map<QString,QString>& resolved,std::string& error) {
    resolved.clear();
    if(members.isEmpty())return true;
    // decltype aliases preserve complete C++ identity, including namespaces,
    // nested container arguments, arrays and private/protected marked members.
    // No constructor or field initializer is executed for reflection.
    QByteArray source=("#include "+Literal(QFileInfo(declarationProbe).absoluteFilePath())+
        "\n#include <type_traits>\nnamespace GameSproutRuntime::Internal { template<> struct ScriptFieldAccess<::"+type+"> {\n").toUtf8();
    for(qsizetype index=0;index<members.size();++index) {
        const QString field="GameSproutResolvedField_"+QString::number(index);
        source+=("using "+field+" = decltype(::"+type+"::"+members[index]+");\n").toUtf8();
    }
    source+="}; }\n";
    // One fixed scratch file per locked project compilation. Remove it before
    // source snapshots are recorded, so it never becomes a persistent input.
    const QString path=QDir(QFileInfo(declarationProbe).absolutePath()).filePath("GameLogic.fieldtypes.cpp");
    if(!ScriptBuildStorage::WriteIfChanged(path,source,error))return false;
    ProbeLifetime lifetime{path};
    std::vector<QJsonObject> declarations;
    if(!readAst||!readAst(path,declarations,error))return false;
    std::map<int,QString> identities;
    for(const auto& declaration:declarations) {
        if(declaration.value("kind").toString()!="TypeAliasDecl")continue;
        const QString name=declaration.value("name").toString();
        const QString prefix="GameSproutResolvedField_";
        if(!name.startsWith(prefix))continue;
        bool valid=false;const int index=name.mid(prefix.size()).toInt(&valid);
        const QString canonical=Identity(declaration);
        if(!valid||index<0||index>=members.size()||canonical.isEmpty()||!identities.emplace(index,canonical).second) {
            error="Clang returned an invalid script field type identity";return false;
        }
    }
    if(identities.size()!=static_cast<std::size_t>(members.size())) {
        error="Clang did not resolve every script field type";return false;
    }
    for(const auto& [index,identity]:identities)resolved.emplace(members[index],identity);
    return true;
}
