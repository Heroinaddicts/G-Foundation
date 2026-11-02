#include "ScriptDeclarationValidator.h"
#include <QFile>
#include <QJsonArray>
#include <map>
#include <set>
#include <vector>

namespace {
QString Kind(const QJsonObject& value) {return value.value("kind").toString();}
QString CanonicalType(const QJsonObject& value) {
    const auto type=value.value("type").toObject();
    QString name=type.value("desugaredQualType").toString(type.value("qualType").toString());
    if(name.startsWith("class "))name=name.mid(6);
    if(name.startsWith("struct "))name=name.mid(7);
    if(name.startsWith("::"))name=name.mid(2);
    return name;
}
bool HasOverrideAttribute(const QJsonObject& method) {
    for(const auto& child:method.value("inner").toArray())if(Kind(child.toObject())=="OverrideAttr")return true;
    return false;
}
bool Overrides(const QJsonObject& method) {
    return method.value("GameSproutOverrides").toBool()||HasOverrideAttribute(method);
}
bool Method(const QJsonObject& value) {
    return Kind(value)=="CXXMethodDecl"&&value.value("storageClass").toString()!="static";
}
QJsonObject ExpandedLocation(const QJsonObject& value) {
    auto location=value.value("loc").toObject();
    if(location.value("expansionLoc").isObject())location=location.value("expansionLoc").toObject();
    return location;
}
QString Location(const QJsonObject& declaration,const QJsonObject& record,const QString& fallback) {
    const auto location=ExpandedLocation(declaration);
    const auto owner=ExpandedLocation(record);
    QString path=location.value("file").toString(owner.value("file").toString(fallback));
    if(path.isEmpty())path=fallback;
    int line=location.value("line").toInt();
    int column=location.value("col").toInt(1);
    if(!line&&location.value("offset").isDouble()) {
        QFile input(path);
        if(input.open(QIODevice::ReadOnly)) {
            const QByteArray text=input.read(location.value("offset").toInteger());
            line=text.count('\n')+1;
            if(column<=0)column=int(text.size()-text.lastIndexOf('\n'));
        }
    }
    if(line<=0)line=owner.value("line").toInt(1);
    if(column<=0)column=1;
    return path+':'+QString::number(line)+':'+QString::number(column);
}
struct Member {
    QString Owner,Name;
    QJsonObject Declaration,Record;
};
std::vector<Member> Members(const QString& owner,const QJsonObject& record) {
    std::vector<Member> result;
    for(const auto& item:record.value("inner").toArray()) {
        const auto declaration=item.toObject();
        if(declaration.value("isImplicit").toBool())continue;
        const auto kind=Kind(declaration);
        const auto name=declaration.value("name").toString();
        if(!name.isEmpty()&&(kind=="FieldDecl"||kind=="VarDecl"||kind=="CXXMethodDecl"||
            kind=="FunctionTemplateDecl"||kind=="TypeAliasDecl"||kind=="TypedefDecl"||
            kind=="CXXRecordDecl"||kind=="ClassTemplateDecl"||kind=="TypeAliasTemplateDecl"||kind=="EnumDecl"))
            result.push_back({owner,name,declaration,record});
        // Unscoped enumerators participate in the enclosing class's lookup;
        // scoped enum entries do not hide a member of that class.
        if(kind=="EnumDecl"&&declaration.value("scopedEnumTag").toString().isEmpty())
            for(const auto& entry:declaration.value("inner").toArray()) {
                const auto enumerator=entry.toObject();const auto enumeratorName=enumerator.value("name").toString();
                if(Kind(enumerator)=="EnumConstantDecl"&&!enumeratorName.isEmpty())
                    result.push_back({owner,enumeratorName,enumerator,record});
            }
    }
    return result;
}
struct RecordData {
    QJsonObject Record;
    std::vector<Member> Own,Inherited;
};
class Validation final {
public:
    Validation(const QString& fallback,const ScriptDeclarationValidator::fLoadRecord& load,std::string& error)
        :_Fallback(fallback),_Load(load),_Error(error) {}
    bool Record(const QString& type) {
        if(_Validated.contains(type))return true;
        if(!_Visiting.insert(type).second){_Error="Cyclic script declaration inheritance: "+type.toStdString();return false;}
        auto& data=_Records[type];
        if(!_Load(type,data.Record,_Error))return false;
        data.Own=Members(type,data.Record);
        for(const auto& item:data.Record.value("bases").toArray()) {
            const QString base=CanonicalType(item.toObject());
            if(base.isEmpty()){_Error="Cannot resolve a base of script type: "+type.toStdString();return false;}
            if(!Record(base))return false;
            const auto& ancestor=_Records.at(base);
            data.Inherited.insert(data.Inherited.end(),ancestor.Own.begin(),ancestor.Own.end());
            data.Inherited.insert(data.Inherited.end(),ancestor.Inherited.begin(),ancestor.Inherited.end());
        }
        for(const auto& member:data.Own) {
            std::vector<const Member*> collisions;
            for(const auto& ancestor:data.Inherited)if(ancestor.Name==member.Name)collisions.push_back(&ancestor);
            if(collisions.empty())continue;
            if(Method(member.Declaration)&&Overrides(member.Declaration)) {
                bool onlyVirtualMethods=true;
                for(const auto* ancestor:collisions)
                    if(!Method(ancestor->Declaration)||(!ancestor->Declaration.value("virtual").toBool()&&!Overrides(ancestor->Declaration)))
                        onlyVirtualMethods=false;
                if(onlyVirtualMethods)continue;
            }
            const auto& ancestor=*collisions.front();
            const auto declarationLocation=Location(member.Declaration,member.Record,_Fallback);
            const auto inheritedLocation=Location(ancestor.Declaration,ancestor.Record,_Fallback);
            _Error=(declarationLocation+": error: 脚本成员 '"+member.Owner+"::"+member.Name+
                "' 遮蔽了继承成员 '"+ancestor.Owner+"::"+ancestor.Name+
                "'。请重命名该成员；生命周期函数须使用正确签名覆盖基类虚函数。\n"+
                inheritedLocation+": note: 被遮蔽的基类成员声明在这里。\n").toStdString();
            return false;
        }
        _Visiting.erase(type);_Validated.insert(type);return true;
    }
private:
    QString _Fallback;
    const ScriptDeclarationValidator::fLoadRecord& _Load;
    std::string& _Error;
    std::map<QString,RecordData> _Records;
    std::set<QString> _Visiting,_Validated;
};
int NodeColumn(const QString& line) {
    int column=0;
    while(column<line.size()&&(line[column]==' '||line[column]=='|'||line[column]=='`'||line[column]=='-'))++column;
    return column;
}
}

bool ScriptDeclarationValidator::Validate(const QString& scriptType,const QString& fallbackSource,
    const fLoadRecord& loadRecord,std::string& error) {
    error.clear();
    if(!loadRecord){error="Script declaration record loader is unavailable";return false;}
    return Validation(fallbackSource,loadRecord,error).Record(scriptType);
}

void ScriptDeclarationValidator::MarkSemanticOverrides(QJsonObject& record,const QByteArray& textDump) {
    auto children=record.value("inner").toArray();
    const auto className=record.value("name").toString();
    const auto lines=QString::fromUtf8(textDump).split('\n');
    int rootColumn=-1,active=-1;
    for(const auto& line:lines) {
        const int column=NodeColumn(line);
        if(rootColumn<0) {
            if(line.mid(column).startsWith("CXXRecordDecl ")&&line.contains(' '+className+" definition"))rootColumn=column;
            continue;
        }
        if(line.isEmpty())continue;
        if(column<=rootColumn)break;
        if(column<=rootColumn+2)active=-1;
        if(column==rootColumn+2&&line.mid(column).startsWith("CXXMethodDecl ")) {
            for(int index=0;index<children.size();++index) {
                const auto method=children[index].toObject();
                if(Kind(method)!="CXXMethodDecl"||method.value("isImplicit").toBool())continue;
                const auto type=method.value("type").toObject().value("qualType").toString();
                const QString identity=' '+method.value("name").toString()+" '"+type+'\'';
                if(line.contains(identity)){active=index;break;}
            }
        } else if(active>=0&&column==rootColumn+4&&line.mid(column).startsWith("Overrides: [")) {
            auto method=children[active].toObject();method["GameSproutOverrides"]=true;children[active]=method;
        }
    }
    record["inner"]=children;
}
