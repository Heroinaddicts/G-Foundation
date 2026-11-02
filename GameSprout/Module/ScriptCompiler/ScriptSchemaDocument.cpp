#include "ScriptSchemaDocument.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <set>

namespace {
QJsonObject Value(const GameSproutRuntime::ScriptValue& value) {
    using GameSproutRuntime::ScriptValueKind;
    QJsonObject output{{"Kind",int(value.Kind)}};
    switch(value.Kind) {
    case ScriptValueKind::Boolean:output["Value"]=value.Boolean;break;
    case ScriptValueKind::Integer:output["Value"]=QString::number(value.Integer);break;
    case ScriptValueKind::Number:output["Value"]=value.Number;break;
    case ScriptValueKind::String:output["Value"]=QString::fromStdString(value.String);break;
    case ScriptValueKind::Vector3:output["Value"]=QJsonArray{value.Vector.X,value.Vector.Y,value.Vector.Z};break;
    case ScriptValueKind::Resource:output["Value"]=QJsonObject{{"Guid",QString::fromStdString(value.Resource.Guid)},{"LocalID",QString::number(value.Resource.LocalID)}};break;
    case ScriptValueKind::SceneObject:output["Value"]=QJsonValue(QJsonValue::Null);break;
    }
    return output;
}
bool ParseValue(const QJsonObject& data,GameSproutRuntime::ScriptValue& value) {
    using GameSproutRuntime::ScriptValueKind;
    const int kind=data.value("Kind").toInt(-1);
    if(kind<int(ScriptValueKind::Boolean)||kind>int(ScriptValueKind::SceneObject))return false;
    value.Kind=static_cast<ScriptValueKind>(kind);const QJsonValue input=data.value("Value");
    switch(value.Kind) {
    case ScriptValueKind::Boolean:if(!input.isBool())return false;value.Boolean=input.toBool();break;
    case ScriptValueKind::Integer:{bool valid=false;value.Integer=input.toString().toLongLong(&valid);if(!valid)return false;break;}
    case ScriptValueKind::Number:if(!input.isDouble()||!std::isfinite(input.toDouble()))return false;value.Number=input.toDouble();break;
    case ScriptValueKind::String:if(!input.isString())return false;value.String=input.toString().toStdString();break;
    case ScriptValueKind::Vector3:{const auto numbers=input.toArray();if(numbers.size()!=3)return false;float* values[]={&value.Vector.X,&value.Vector.Y,&value.Vector.Z};for(int i=0;i<3;++i){if(!numbers[i].isDouble()||!std::isfinite(numbers[i].toDouble()))return false;*values[i]=float(numbers[i].toDouble());if(!std::isfinite(*values[i]))return false;}break;}
    case ScriptValueKind::Resource:{const auto reference=input.toObject();bool valid=false;value.Resource.Guid=reference.value("Guid").toString().toStdString();value.Resource.LocalID=reference.value("LocalID").toString().toULongLong(&valid);if(!valid)return false;break;}
    case ScriptValueKind::SceneObject:if(!input.isNull())return false;break;
    }
    return true;
}
}
namespace ScriptSchemaDocument {
QByteArray Serialize(const std::string& buildID,const std::vector<GameSproutRuntime::ScriptTypeInfo>& types) {
    QJsonArray entries;
    for(const auto& type:types) {
        QJsonArray fields;
        for(const auto& field:type.Fields)fields.append(QJsonObject{{"ID",QString::fromStdString(field.ID)},{"Name",QString::fromStdString(field.Name)},
            {"DeclaringType",QString::fromStdString(field.DeclaringType)},{"ResourceKind",int(field.ResourceKind)},{"Default",Value(field.Default)}});
        entries.append(QJsonObject{{"TypeID",QString::fromStdString(type.TypeID)},{"Name",QString::fromStdString(type.Name)},
            {"SourceFile",QString::fromStdString(type.SourceFile)},{"Fields",fields}});
    }
    return QJsonDocument(QJsonObject{{"Version",1},{"BuildID",QString::fromStdString(buildID)},{"Types",entries}}).toJson(QJsonDocument::Compact);
}
bool Parse(const QByteArray& bytes,const std::string& buildID,std::vector<GameSproutRuntime::ScriptTypeInfo>& types,std::string& error) {
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);const auto root=document.object();
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||root.value("Version").toInt()!=1||
       root.value("BuildID").toString()!=QString::fromStdString(buildID)||!root.value("Types").isArray()) {error="Invalid or mismatched GameLogic schema";return false;}
    std::vector<GameSproutRuntime::ScriptTypeInfo> candidate;std::set<std::string> identities;
    for(const auto& entry:root.value("Types").toArray()) {
        const auto data=entry.toObject();GameSproutRuntime::ScriptTypeInfo type;
        type.TypeID=data.value("TypeID").toString().toStdString();type.Name=data.value("Name").toString().toStdString();type.SourceFile=data.value("SourceFile").toString().toStdString();
        if(type.TypeID.empty()||type.Name.empty()||!identities.insert(type.TypeID).second||!data.value("Fields").isArray()){error="Invalid GameLogic schema type";return false;}
        std::set<std::string> fieldIDs;
        for(const auto& item:data.value("Fields").toArray()) {
            const auto fieldData=item.toObject();GameSproutRuntime::ScriptFieldInfo field;
            field.ID=fieldData.value("ID").toString().toStdString();field.Name=fieldData.value("Name").toString().toStdString();field.DeclaringType=fieldData.value("DeclaringType").toString().toStdString();
            const int resource=fieldData.value("ResourceKind").toInt(-1);
            if(field.ID.empty()||field.Name.empty()||field.DeclaringType.empty()||!fieldIDs.insert(field.ID).second||resource<0||resource>int(GameSproutRuntime::ScriptResourceKind::Audio)||!ParseValue(fieldData.value("Default").toObject(),field.Default)){error="Invalid GameLogic schema field";return false;}
            field.ResourceKind=static_cast<GameSproutRuntime::ScriptResourceKind>(resource);type.Fields.push_back(std::move(field));
        }
        candidate.push_back(std::move(type));
    }
    types.swap(candidate);error.clear();return true;
}
}
