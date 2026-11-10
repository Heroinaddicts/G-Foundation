#include "ScriptSchemaDocument.h"
#include "IGuidCreator.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <limits>
#include <set>

namespace {
QJsonObject Value(const GameSproutRuntime::ScriptValue& value) {
    using GameSproutRuntime::ScriptValueKind;
    QJsonObject output{{"Kind",int(value.Kind)}};
    switch(value.Kind) {
    case ScriptValueKind::Boolean:output["Value"]=value.Boolean;break;
    case ScriptValueKind::Integer:output["Value"]=QString::number(value.Integer);break;
    case ScriptValueKind::Unsigned:output["Value"]=QString::number(value.Unsigned);break;
    case ScriptValueKind::Number:output["Value"]=value.Number;break;
    case ScriptValueKind::String:output["Value"]=QString::fromStdString(value.String);break;
    case ScriptValueKind::Vector2:output["Value"]=QJsonArray{value.Vector2.X,value.Vector2.Y};break;
    case ScriptValueKind::Vector3:output["Value"]=QJsonArray{value.Vector.X,value.Vector.Y,value.Vector.Z};break;
    case ScriptValueKind::Vector4:output["Value"]=QJsonArray{value.Vector4.X,value.Vector4.Y,value.Vector4.Z,value.Vector4.W};break;
    case ScriptValueKind::Quaternion:output["Value"]=QJsonArray{value.Rotation.X,value.Rotation.Y,value.Rotation.Z,value.Rotation.W};break;
    case ScriptValueKind::Color:output["Value"]=QJsonArray{value.Tint.R,value.Tint.G,value.Tint.B,value.Tint.A};break;
    case ScriptValueKind::Resource:output["Value"]=QJsonObject{{"Guid",QString::fromStdString(value.Resource.Guid)},{"LocalID",QString::number(value.Resource.LocalID)}};break;
    case ScriptValueKind::SceneObject:case ScriptValueKind::Component:output["Value"]=QJsonValue(QJsonValue::Null);break;
    case ScriptValueKind::Structure:{QJsonObject members;for(const auto& [id,child]:value.Fields)members[QString::fromStdString(id)]=Value(child);output["Value"]=members;break;}
    case ScriptValueKind::Sequence:{QJsonArray elements;for(const auto& child:value.Elements)elements.append(Value(child));output["Value"]=elements;break;}
    case ScriptValueKind::Dictionary:{QJsonArray entries;for(const auto& [key,child]:value.Entries)entries.append(QJsonObject{{"Key",Value(key)},{"Value",Value(child)}});output["Value"]=entries;break;}
    }
    return output;
}
bool ParseValue(const QJsonObject& data,GameSproutRuntime::ScriptValue& value,unsigned depth=0) {
    using GameSproutRuntime::ScriptValueKind;
    if(depth>32||!data.value("Kind").isDouble()||!data.contains("Value"))return false;
    value.Kind=static_cast<ScriptValueKind>(data.value("Kind").toInt(-1));const QJsonValue input=data.value("Value");
    switch(value.Kind) {
    case ScriptValueKind::Boolean:if(!input.isBool())return false;value.Boolean=input.toBool();break;
    case ScriptValueKind::Integer:{bool valid=false;value.Integer=input.toString().toLongLong(&valid);if(!valid)return false;break;}
    case ScriptValueKind::Unsigned:{bool valid=false;value.Unsigned=input.toString().toULongLong(&valid);if(!valid)return false;break;}
    case ScriptValueKind::Number:if(!input.isDouble()||!std::isfinite(input.toDouble()))return false;value.Number=input.toDouble();break;
    case ScriptValueKind::String:if(!input.isString())return false;value.String=input.toString().toStdString();break;
    case ScriptValueKind::Vector2:case ScriptValueKind::Vector3:case ScriptValueKind::Vector4:case ScriptValueKind::Quaternion:case ScriptValueKind::Color:{
        const auto numbers=input.toArray();const int count=value.Kind==ScriptValueKind::Vector2?2:value.Kind==ScriptValueKind::Vector3?3:4;
        if(!input.isArray()||numbers.size()!=count)return false;float items[4]{};
        for(int i=0;i<count;++i){if(!numbers[i].isDouble()||!std::isfinite(numbers[i].toDouble())||std::fabs(numbers[i].toDouble())>std::numeric_limits<float>::max())return false;items[i]=float(numbers[i].toDouble());}
        if(value.Kind==ScriptValueKind::Vector2)value.Vector2={items[0],items[1]};
        else if(value.Kind==ScriptValueKind::Vector3)value.Vector={items[0],items[1],items[2]};
        else if(value.Kind==ScriptValueKind::Vector4)value.Vector4={items[0],items[1],items[2],items[3]};
        else if(value.Kind==ScriptValueKind::Quaternion)value.Rotation={items[0],items[1],items[2],items[3]};
        else value.Tint={items[0],items[1],items[2],items[3]};break;
    }
    case ScriptValueKind::Resource:{const auto reference=input.toObject();bool valid=false;value.Resource.Guid=reference.value("Guid").toString().toStdString();value.Resource.LocalID=reference.value("LocalID").toString().toULongLong(&valid);if(!input.isObject()||!valid)return false;break;}
    case ScriptValueKind::SceneObject:case ScriptValueKind::Component:if(!input.isNull())return false;break;
    case ScriptValueKind::Structure:{
        if(!input.isObject()||input.toObject().size()>4096)return false;
        const auto members=input.toObject();for(auto i=members.begin();i!=members.end();++i){GameSproutRuntime::ScriptValue child;if(i.key().isEmpty()||!i.value().isObject()||!ParseValue(i.value().toObject(),child,depth+1))return false;value.Fields.emplace(i.key().toStdString(),std::move(child));}break;
    }
    case ScriptValueKind::Sequence:{
        if(!input.isArray()||input.toArray().size()>100000)return false;
        for(const auto& item:input.toArray()){GameSproutRuntime::ScriptValue child;if(!item.isObject()||!ParseValue(item.toObject(),child,depth+1))return false;value.Elements.push_back(std::move(child));}break;
    }
    case ScriptValueKind::Dictionary:{
        if(!input.isArray()||input.toArray().size()>100000)return false;
        for(const auto& item:input.toArray()){if(!item.isObject())return false;const auto pair=item.toObject();GameSproutRuntime::ScriptValue key,child;if(!pair.value("Key").isObject()||!pair.value("Value").isObject()||!ParseValue(pair.value("Key").toObject(),key,depth+1)||!ParseValue(pair.value("Value").toObject(),child,depth+1))return false;value.Entries.emplace_back(std::move(key),std::move(child));}break;
    }
    default:return false;
    }
    return true;
}
QJsonObject Field(const GameSproutRuntime::ScriptFieldInfo& field) {
    QJsonArray children,enums;
    for(const auto& child:field.Children)children.append(Field(child));
    for(const auto& entry:field.EnumValues)enums.append(QJsonObject{{"Name",QString::fromStdString(entry.Name)},{"Value",QString::number(entry.Value)},{"IsUnsigned",entry.IsUnsigned},{"UnsignedValue",QString::number(entry.UnsignedValue)}});
    return {{"ID",QString::fromStdString(field.ID)},{"Name",QString::fromStdString(field.Name)},
        {"DeclaringType",QString::fromStdString(field.DeclaringType)},{"ResourceKind",int(field.ResourceKind)},
        {"Default",Value(field.Default)},{"TypeName",QString::fromStdString(field.TypeName)},
        {"ComponentType",QString::fromStdString(field.ComponentType)},{"FixedLength",QString::number(field.FixedLength)},
        {"Children",children},{"EnumValues",enums}};
}
bool ParseField(const QJsonObject& data,GameSproutRuntime::ScriptFieldInfo& field,unsigned depth=0) {
    using GameSproutRuntime::ScriptValueKind;
    if(depth>32)return false;
    field.ID=data.value("ID").toString().toStdString();field.Name=data.value("Name").toString().toStdString();
    field.DeclaringType=data.value("DeclaringType").toString().toStdString();field.TypeName=data.value("TypeName").toString().toStdString();field.ComponentType=data.value("ComponentType").toString().toStdString();
    const int resource=data.value("ResourceKind").toInt(-1);bool valid=false;field.FixedLength=data.value("FixedLength").toString().toLongLong(&valid);
    if(field.ID.empty()||field.Name.empty()||field.TypeName.empty()||(!depth&&field.DeclaringType.empty())||resource<0||resource>int(GameSproutRuntime::ScriptResourceKind::Audio)||!valid||field.FixedLength < -1||!data.value("Default").isObject()||!ParseValue(data.value("Default").toObject(),field.Default)||!data.value("Children").isArray()||!data.value("EnumValues").isArray())return false;
    field.ResourceKind=static_cast<GameSproutRuntime::ScriptResourceKind>(resource);
    std::set<std::string> childIDs,names;
    for(const auto& item:data.value("Children").toArray()) {
        GameSproutRuntime::ScriptFieldInfo child;
        if(field.Children.size()>=4096||!item.isObject()||!ParseField(item.toObject(),child,depth+1)||!childIDs.insert(child.ID).second)return false;
        field.Children.push_back(std::move(child));
    }
    for(const auto& item:data.value("EnumValues").toArray()) {
        if(!item.isObject()||field.EnumValues.size()>=10000)return false;const auto value=item.toObject();GameSproutRuntime::ScriptEnumValue entry;
        entry.Name=value.value("Name").toString().toStdString();entry.Value=value.value("Value").toString().toLongLong(&valid);if(!valid||entry.Name.empty()||!names.insert(entry.Name).second||!value.value("IsUnsigned").isBool())return false;
        entry.IsUnsigned=value.value("IsUnsigned").toBool();entry.UnsignedValue=value.value("UnsignedValue").toString().toULongLong(&valid);if(!valid)return false;field.EnumValues.push_back(std::move(entry));
    }
    if(!field.EnumValues.empty()&&field.Default.Kind!=ScriptValueKind::Integer&&field.Default.Kind!=ScriptValueKind::Unsigned)return false;
    switch(field.Default.Kind) {
    case ScriptValueKind::Structure:if(field.Children.empty()||field.Default.Fields.size()!=field.Children.size())return false;break;
    case ScriptValueKind::Sequence:if(field.Children.size()!=1||(field.FixedLength>=0&&field.Default.Elements.size()!=std::size_t(field.FixedLength)))return false;break;
    case ScriptValueKind::Dictionary:if(field.Children.size()!=2)return false;break;
    case ScriptValueKind::Component:if(field.ComponentType.empty()||!field.Children.empty())return false;break;
    default:if(!field.Children.empty())return false;break;
    }
    return true;
}
}
namespace ScriptSchemaDocument {
QByteArray Serialize(const std::string& buildID,const std::vector<GameSproutRuntime::ScriptTypeInfo>& types,
                     const std::vector<GameSproutRuntime::GameModuleTypeInfo>& modules) {
    QJsonArray entries,moduleEntries;
    for(const auto& type:types) {
        QJsonArray fields;for(const auto& field:type.Fields)fields.append(Field(field));
        entries.append(QJsonObject{{"TypeID",QString::fromStdString(type.TypeID)},{"Name",QString::fromStdString(type.Name)},
            {"SourceFile",QString::fromStdString(type.SourceFile)},{"Fields",fields}});
    }
    for(const auto& module:modules)
        moduleEntries.append(QJsonObject{{"Name",QString::fromStdString(module.Name)},
            {"SourceGuid",QString::fromStdString(module.SourceGuid)},{"SourceFile",QString::fromStdString(module.SourceFile)}});
    return QJsonDocument(QJsonObject{{"Version",3},{"BuildID",QString::fromStdString(buildID)},
        {"Types",entries},{"Modules",moduleEntries}}).toJson(QJsonDocument::Compact);
}
bool Parse(const QByteArray& bytes,const std::string& buildID,std::vector<GameSproutRuntime::ScriptTypeInfo>& types,
           std::vector<GameSproutRuntime::GameModuleTypeInfo>& modules,std::string& error) {
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);const auto root=document.object();
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||root.value("Version").toInt()!=3||
       root.value("BuildID").toString()!=QString::fromStdString(buildID)||!root.value("Types").isArray()||
       !root.value("Modules").isArray()) {error="Invalid or mismatched GameLogic schema";return false;}
    std::vector<GameSproutRuntime::ScriptTypeInfo> candidate;std::set<std::string> identities;
    for(const auto& entry:root.value("Types").toArray()) {
        const auto data=entry.toObject();GameSproutRuntime::ScriptTypeInfo type;
        type.TypeID=data.value("TypeID").toString().toStdString();type.Name=data.value("Name").toString().toStdString();type.SourceFile=data.value("SourceFile").toString().toStdString();
        if(type.TypeID.empty()||type.Name.empty()||!identities.insert(type.TypeID).second||!data.value("Fields").isArray()){error="Invalid GameLogic schema type";return false;}
        std::set<std::string> fieldIDs;
        for(const auto& item:data.value("Fields").toArray()) {
            GameSproutRuntime::ScriptFieldInfo field;
            if(!item.isObject()||!ParseField(item.toObject(),field)||!fieldIDs.insert(field.ID).second){error="Invalid GameLogic schema field";return false;}
            type.Fields.push_back(std::move(field));
        }
        candidate.push_back(std::move(type));
    }
    std::vector<GameSproutRuntime::GameModuleTypeInfo> candidateModules;
    std::set<std::string> moduleNames;
    std::set<Guid> moduleGuids;
    for(const auto& entry:root.value("Modules").toArray()) {
        const auto data=entry.toObject();
        const auto name=data.value("Name").toString();
        const auto guid=data.value("SourceGuid").toString();
        const auto source=data.value("SourceFile").toString();
        const Guid sourceGuid(guid.toStdString());
        if(name.isEmpty()||name.contains(QChar(0))||!sourceGuid.IsValid()||source.isEmpty()||source.contains(QChar(0))||
           QDir::isAbsolutePath(source)||QDir::cleanPath(source)!=source||source.startsWith("../")||
           QFileInfo(source).suffix().toLower()!="hpp"||!moduleNames.insert(name.toStdString()).second||
           !moduleGuids.insert(sourceGuid).second) {
            error="Invalid GameLogic schema module";
            return false;
        }
        candidateModules.push_back({name.toStdString(),guid.toStdString(),source.toStdString()});
    }
    types.swap(candidate);
    modules.swap(candidateModules);
    error.clear();
    return true;
}
}
