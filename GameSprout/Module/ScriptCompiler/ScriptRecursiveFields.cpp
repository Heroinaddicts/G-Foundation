#include "ScriptRecursiveFields.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString Kind(const QJsonObject& node) {return node.value("kind").toString();}
QString TypeName(const QJsonObject& node) {
    const auto data=node.value("type").toObject();
    QString type=data.value("desugaredQualType").toString(data.value("qualType").toString()).simplified();
    type.replace("std::__1::","std::");type.replace("std::__cxx11::","std::");
    if(type.startsWith("struct "))type=type.mid(7);
    if(type.startsWith("class "))type=type.mid(6);
    if(type.startsWith("enum "))type=type.mid(5);
    if(type.startsWith("::"))type=type.mid(2);
    return type;
}
QString Literal(const QString& value) {
    const QByteArray array=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1,array.size()-2));
}
QJsonObject Initializer(const QJsonObject& field) {
    for(const auto& child:field.value("inner").toArray())if(!Kind(child.toObject()).endsWith("Attr"))return child.toObject();
    return {};
}
QJsonObject Unwrap(QJsonObject value) {
    static const std::set<QString> wrappers={"ExprWithCleanups","MaterializeTemporaryExpr","CXXBindTemporaryExpr",
        "CXXFunctionalCastExpr","ImplicitCastExpr","ParenExpr","ConstantExpr"};
    while(wrappers.contains(Kind(value))&&value.value("inner").toArray().size()==1)value=value.value("inner").toArray().first().toObject();
    return value;
}
QJsonArray Parts(const QJsonObject& value) {
    auto node=Unwrap(value);
    if(Kind(node)=="CXXStdInitializerListExpr") {
        const auto inner=node.value("inner").toArray();return inner.isEmpty()?QJsonArray():Parts(inner.first().toObject());
    }
    return node.value("inner").toArray();
}
bool EmptyDefault(const QJsonObject& value) {
    const auto node=Unwrap(value);const auto kind=Kind(node);
    if(node.isEmpty()||kind=="ImplicitValueInitExpr"||kind=="CXXScalarValueInitExpr")return true;
    if(kind=="InitListExpr"&&node.value("inner").toArray().isEmpty())return true;
    if(kind=="CXXConstructExpr"||kind=="CXXTemporaryObjectExpr") {
        const auto children=node.value("inner").toArray();
        return std::all_of(children.begin(),children.end(),[](const QJsonValue& child){return Kind(child.toObject())=="CXXDefaultArgExpr";});
    }
    return false;
}
QJsonObject WithInitializer(const QString& type,const QJsonObject& initializer) {
    QJsonObject field{{"type",QJsonObject{{"desugaredQualType",type}}}};
    if(!initializer.isEmpty()) {field["hasInClassInitializer"]=true;field["inner"]=QJsonArray{initializer};}
    return field;
}
bool TemplateArguments(const QString& type,QString& name,QStringList& arguments) {
    const auto start=type.indexOf('<');if(start<0||!type.endsWith('>'))return false;
    name=type.left(start).trimmed();int depth=0;qsizetype begin=start+1;
    for(qsizetype i=begin;i+1<type.size();++i) {
        if(type[i]=='<'||type[i]=='['||type[i]=='(')++depth;
        else if(type[i]=='>'||type[i]==']'||type[i]==')')--depth;
        else if(type[i]==','&&depth==0) {arguments.push_back(type.mid(begin,i-begin).trimmed());begin=i+1;}
    }
    arguments.push_back(type.mid(begin,type.size()-begin-1).trimmed());return depth==0;
}
QString ComponentType(const QString& type) {
    static const std::map<QString,QString> components={
        {"ITransformComponent","Transform"},{"IRectTransformComponent","RectTransform"},{"ICanvasComponent","Canvas"},
        {"IImageComponent","Image"},{"ITextComponent","Text"},{"IButtonComponent","Button"},
        {"ICameraComponent","Camera"},{"ILightComponent","Light"},{"IMeshComponent","Mesh"},
        {"IMeshRendererComponent","MeshRenderer"},{"ISkinnedMeshRendererComponent","SkinnedMeshRenderer"},
        {"IAnimatorComponent","Animator"},{"IRigidbodyComponent","Rigidbody"},{"IBoxColliderComponent","BoxCollider"},
        {"ISphereColliderComponent","SphereCollider"},{"ICapsuleColliderComponent","CapsuleCollider"},
        {"IMeshColliderComponent","MeshCollider"},{"IColliderComponent","Collider"},{"IComponent","Component"},
        {"IScriptComponent","ScriptComponent"},{"IScript","Script"},{"ScriptBehaviour","Script"}};
    QString clean=type;clean.remove(' ');
    if(!clean.startsWith("GameSproutRuntime::")||!clean.endsWith('*'))return {};
    const auto found=components.find(clean.mid(19,clean.size()-20));
    return found==components.end()?QString():"GameSproutRuntime."+found->second;
}
QString FloatCode(float value) {return "static_cast<float>("+QString::number(value,'g',9)+")";}
QString SignedCode(std::int64_t value) {
    return value==std::numeric_limits<std::int64_t>::min()?QString("(-9223372036854775807LL-1)"):QString::number(value)+"LL";
}
QString ValueCode(const GameSproutRuntime::ScriptValue& value) {
    using GameSproutRuntime::ScriptValueKind;
    QString code="([] { ::GameSproutRuntime::ScriptValue value; value.Kind=static_cast<::GameSproutRuntime::ScriptValueKind>("+QString::number(int(value.Kind))+"); ";
    switch(value.Kind) {
    case ScriptValueKind::Boolean:code+="value.Boolean="+QString(value.Boolean?"true":"false")+"; ";break;
    case ScriptValueKind::Integer:code+="value.Integer="+SignedCode(value.Integer)+"; ";break;
    case ScriptValueKind::Unsigned:code+="value.Unsigned="+QString::number(value.Unsigned)+"ULL; ";break;
    case ScriptValueKind::Number:code+="value.Number="+QString::number(value.Number,'g',17)+"; ";break;
    case ScriptValueKind::String:code+="value.String="+Literal(QString::fromStdString(value.String))+"; ";break;
    case ScriptValueKind::Vector2:code+="value.Vector2={"+FloatCode(value.Vector2.X)+","+FloatCode(value.Vector2.Y)+"}; ";break;
    case ScriptValueKind::Vector3:code+="value.Vector={"+FloatCode(value.Vector.X)+","+FloatCode(value.Vector.Y)+","+FloatCode(value.Vector.Z)+"}; ";break;
    case ScriptValueKind::Vector4:code+="value.Vector4={"+FloatCode(value.Vector4.X)+","+FloatCode(value.Vector4.Y)+","+FloatCode(value.Vector4.Z)+","+FloatCode(value.Vector4.W)+"}; ";break;
    case ScriptValueKind::Quaternion:code+="value.Rotation={"+FloatCode(value.Rotation.X)+","+FloatCode(value.Rotation.Y)+","+FloatCode(value.Rotation.Z)+","+FloatCode(value.Rotation.W)+"}; ";break;
    case ScriptValueKind::Color:code+="value.Tint={"+FloatCode(value.Tint.R)+","+FloatCode(value.Tint.G)+","+FloatCode(value.Tint.B)+","+FloatCode(value.Tint.A)+"}; ";break;
    case ScriptValueKind::Resource:code+="value.Resource={"+Literal(QString::fromStdString(value.Resource.Guid))+","+QString::number(value.Resource.LocalID)+"ULL}; ";break;
    case ScriptValueKind::SceneObject:case ScriptValueKind::Component:break; // Offline defaults are null references.
    case ScriptValueKind::Structure:
        for(const auto& [id,child]:value.Fields)code+="value.Fields.emplace("+Literal(QString::fromStdString(id))+","+ValueCode(child)+"); ";break;
    case ScriptValueKind::Sequence:
        for(const auto& child:value.Elements)code+="value.Elements.push_back("+ValueCode(child)+"); ";break;
    case ScriptValueKind::Dictionary:
        for(const auto& entry:value.Entries)code+="value.Entries.emplace_back("+ValueCode(entry.first)+","+ValueCode(entry.second)+"); ";break;
    }
    return code+"return value; }())";
}
QString InfoCode(const GameSproutRuntime::ScriptFieldInfo& info) {
    QString code="([] { ::GameSproutRuntime::ScriptFieldInfo field; field.ID="+Literal(QString::fromStdString(info.ID))+"; field.Name="+Literal(QString::fromStdString(info.Name))+"; field.DeclaringType="+Literal(QString::fromStdString(info.DeclaringType))+"; field.Default="+ValueCode(info.Default)+"; field.ResourceKind=static_cast<::GameSproutRuntime::ScriptResourceKind>("+QString::number(int(info.ResourceKind))+"); field.TypeName="+Literal(QString::fromStdString(info.TypeName))+"; field.ComponentType="+Literal(QString::fromStdString(info.ComponentType))+"; field.FixedLength="+QString::number(info.FixedLength)+"LL; ";
    for(const auto& item:info.EnumValues)code+="field.EnumValues.push_back({"+Literal(QString::fromStdString(item.Name))+","+SignedCode(item.Value)+","+QString(item.IsUnsigned?"true":"false")+","+QString::number(item.UnsignedValue)+"ULL}); ";
    for(const auto& child:info.Children)code+="field.Children.push_back("+InfoCode(child)+"); ";
    return code+"return field; }())";
}
bool NullInitializer(const QJsonObject& initializer) {
    if(EmptyDefault(initializer))return true;
    const auto value=Unwrap(initializer);return Kind(value)=="CXXNullPtrLiteralExpr";
}
bool NullRecordConstructor(const QJsonObject& record) {
    for(const auto& entry:record.value("inner").toArray()) {
        const auto constructor=entry.toObject();
        if(Kind(constructor)=="CXXConstructorDecl"&&!constructor.value("isImplicit").toBool()&&
           constructor.value("explicitlyDefaulted").toString().isEmpty())return false;
    }
    return true;
}
bool IntegerConstant(const QJsonObject& node,__int128& result) {
    const auto kind=Kind(node);
    if((kind=="ConstantExpr"||kind=="IntegerLiteral")&&node.value("value").isString()) {
        const auto value=node.value("value").toString();bool valid=false;
        if(value.startsWith('-'))result=value.toLongLong(&valid);else result=value.toULongLong(&valid);
        if(valid)return true;
    }
    for(const auto& child:node.value("inner").toArray())if(IntegerConstant(child.toObject(),result))return true;
    return false;
}
}

ScriptRecursiveFields::ScriptRecursiveFields(fLoadDeclaration load,fResolveFields resolve,fDefaultValue scalar,fMarked marked,
    std::map<QString,QString> scriptTypes)
    :_Load(std::move(load)),_Resolve(std::move(resolve)),_Scalar(std::move(scalar)),_Marked(std::move(marked)),_ScriptTypes(std::move(scriptTypes)) {}

bool ScriptRecursiveFields::Record(const QString& type,QJsonObject& record,std::string& error) {
    const auto previous=_Records.find(type);if(previous!=_Records.end()){record=previous->second;return true;}
    if(!_Load(type,record,error))return false;
    if(Kind(record)=="CXXRecordDecl"&&!_Resolve(record,type,error))return false;
    _Records.emplace(type,record);return true;
}

bool ScriptRecursiveFields::Members(const QString& type,const QJsonObject& record,std::vector<Member>& members,std::string& error) {
    for(const auto& entry:record.value("bases").toArray()) {
        const auto base=entry.toObject();const QString baseType=TypeName(base);
        if(base.value("access").toString()!="public") {error="Serialized record bases must use public inheritance: "+baseType.toStdString();return false;}
        QJsonObject parent;if(!Record(baseType,parent,error)||!Members(baseType,parent,members,error))return false;
    }
    QString access=record.value("tagUsed").toString()=="struct"?"public":"private";
    for(const auto& entry:record.value("inner").toArray()) {
        const auto field=entry.toObject();if(Kind(field)=="AccessSpecDecl"){access=field.value("access").toString();continue;}
        if(Kind(field)!="FieldDecl"||(access!="public"&&!_Marked(record,field)))continue;
        if(field.value("isBitfield").toBool()||field.value("name").toString().isEmpty()) {error="Unnamed fields and bitfields cannot be serialized in "+type.toStdString();return false;}
        members.push_back({type,field});
    }
    return true;
}

bool ScriptRecursiveFields::EnumValue(const QString& type,const QJsonObject& record,const QJsonObject& initializer,
    GameSproutRuntime::ScriptFieldInfo& info,std::string& error,bool schemaOnly) {
    using GameSproutRuntime::ScriptValueKind;
    const bool unsignedType=TypeName(QJsonObject{{"type",record.value("fixedUnderlyingType")}}).startsWith("unsigned");
    info.Default.Kind=unsignedType?ScriptValueKind::Unsigned:ScriptValueKind::Integer;info.TypeName=type.toStdString();
    std::map<QString,__int128> constants;__int128 current=0;
    for(const auto& entry:record.value("inner").toArray()) {
        const auto constant=entry.toObject();if(Kind(constant)!="EnumConstantDecl")continue;
        if(!constant.value("inner").toArray().isEmpty()&&!IntegerConstant(constant,current)) {error="Cannot evaluate enum constant: "+type.toStdString();return false;}
        GameSproutRuntime::ScriptEnumValue value;value.Name=constant.value("name").toString().toStdString();value.IsUnsigned=unsignedType;
        if(unsignedType){if(current<0||current>std::numeric_limits<std::uint64_t>::max())return false;value.UnsignedValue=static_cast<std::uint64_t>(current);}
        else {if(current<std::numeric_limits<std::int64_t>::min()||current>std::numeric_limits<std::int64_t>::max())return false;value.Value=static_cast<std::int64_t>(current);}
        constants[constant.value("id").toString()]=current;
        // AST addresses are process-local; defaults and enum declarations are
        // queried independently, so use the enumerator name across those dumps.
        constants[constant.value("name").toString()]=current;
        info.EnumValues.push_back(value);++current;
    }
    __int128 value=0;
    if(!EmptyDefault(initializer)) {
        const auto expression=Unwrap(initializer);
        if(Kind(expression)=="DeclRefExpr") {
            const auto declaration=expression.value("referencedDecl").toObject();
            auto found=constants.find(declaration.value("id").toString());
            if(found==constants.end())found=constants.find(declaration.value("name").toString());
            if(found==constants.end()){error="Enum initializer does not refer to a reflected constant: "+type.toStdString();return false;}value=found->second;
        } else if(!IntegerConstant(expression,value)){error="Enum default must be a literal or enum constant: "+type.toStdString();return false;}
    }
    if(unsignedType){if(value<0||value>std::numeric_limits<std::uint64_t>::max())return false;info.Default.Unsigned=static_cast<std::uint64_t>(value);}
    else {if(value<std::numeric_limits<std::int64_t>::min()||value>std::numeric_limits<std::int64_t>::max())return false;info.Default.Integer=static_cast<std::int64_t>(value);}
    if(std::none_of(info.EnumValues.begin(),info.EnumValues.end(),[&](const auto& entry){return unsignedType?entry.UnsignedValue==info.Default.Unsigned:entry.Value==info.Default.Integer;})) {
        if(schemaOnly&&EmptyDefault(initializer)&&!info.EnumValues.empty()) {
            info.Default.Integer=info.EnumValues.front().Value;info.Default.Unsigned=info.EnumValues.front().UnsignedValue;return true;
        }
        error="Enum default is not one of its declared values: "+type.toStdString();return false;
    }
    return true;
}

bool ScriptRecursiveFields::Reflect(const QString& original,const QJsonObject& initializer,
    GameSproutRuntime::ScriptFieldInfo& info,QByteArray& generated,std::string& error,int depth,bool schemaOnly) {
    using GameSproutRuntime::ScriptValueKind;
    if(depth>32){error="Serialized field nesting exceeds 32 levels";return false;}
    const QString type=TypeName(QJsonObject{{"type",QJsonObject{{"desugaredQualType",original}}}});
    info.TypeName=type.toStdString();
    if(type.contains('&')||type.startsWith("const ")||type.startsWith("volatile ")||type.contains(" const")||type.contains(" volatile")) {error="References and const/volatile fields are not writable serialized fields: "+type.toStdString();return false;}
    if(type.endsWith('*')) {
        QString plain=type;plain.remove(' ');
        if(plain=="GameSproutRuntime::IGameObject*")info.Default.Kind=ScriptValueKind::SceneObject;
        else {
            info.ComponentType=ComponentType(type).toStdString();
            if(info.ComponentType.empty()) {
                const auto script=_ScriptTypes.find(type.left(type.lastIndexOf('*')).trimmed());
                if(script!=_ScriptTypes.end())info.ComponentType=script->second.toStdString();
            }
            if(info.ComponentType.empty()){error="Only Runtime object/component pointers can be serialized: "+type.toStdString();return false;}
            info.Default.Kind=ScriptValueKind::Component;
        }
        if(!NullInitializer(initializer)){error="Serialized reference defaults must be null";return false;}return true;
    }
    static const std::set<QString> scalar={"bool","char","signed char","unsigned char","short","short int","unsigned short","unsigned short int","int","unsigned int","unsigned","long","long int","unsigned long","unsigned long int","long long","long long int","unsigned long long","unsigned long long int","float","double","std::string","std::basic_string<char>","std::basic_string<char, std::char_traits<char>, std::allocator<char>>","GameSproutRuntime::Vector2","GameSproutRuntime::Vector3","GameSproutRuntime::Vector4","GameSproutRuntime::Quaternion","GameSproutRuntime::Color","GameSproutRuntime::ResourceReference"};
    if(scalar.contains(type)) {
        if(!_Scalar(WithInitializer(type,initializer),info.Default)){error="Default must be a supported constant initializer: "+type.toStdString();return false;}return true;
    }
    const auto array=QRegularExpression("^(.+?)\\[([0-9]+)\\](.*)$").match(type);
    QString container;QStringList arguments;const bool templated=TemplateArguments(type,container,arguments);
    const bool sequence=array.hasMatch()||(templated&&(container=="std::array"||container=="std::vector"||container=="std::list"));
    if(sequence) {
        const QString element=array.hasMatch()?array.captured(1)+array.captured(3):arguments.value(0);
        info.FixedLength=-1;
        if(array.hasMatch()||container=="std::array") {
            bool valid=false;const auto count=(array.hasMatch()?array.captured(2):arguments.value(1)).toLongLong(&valid);
            if(!valid||count<0||count>100000){error="Invalid or excessive fixed array length: "+type.toStdString();return false;}info.FixedLength=count;
        }
        GameSproutRuntime::ScriptFieldInfo child;child.ID="Element";child.Name="Element";
        if(!Reflect(element,{},child,generated,error,depth+1,true))return false;
        info.Children={child};info.Default.Kind=ScriptValueKind::Sequence;
        QJsonArray elements;
        if(!EmptyDefault(initializer)) {
            auto expression=Unwrap(initializer);
            if(Kind(expression)=="CXXConstructExpr") {
                const auto parts=expression.value("inner").toArray();
                if(parts.isEmpty()||Kind(Unwrap(parts.first().toObject()))!="CXXStdInitializerListExpr"){error="Container defaults must use an initializer list: "+type.toStdString();return false;}
                expression=Unwrap(parts.first().toObject());
            }
            elements=Parts(expression);
            if(container=="std::array"&&elements.size()==1&&Kind(Unwrap(elements.first().toObject()))=="InitListExpr")elements=Parts(elements.first().toObject());
        }
        if(info.FixedLength>=0&&elements.size()>info.FixedLength){error="Array default exceeds its fixed length";return false;}
        const auto count=info.FixedLength>=0?info.FixedLength:elements.size();
        for(qsizetype i=0;i<count;++i) {
            GameSproutRuntime::ScriptFieldInfo item;
            if(i>=elements.size()){if(!Reflect(element,{},item,generated,error,depth+1,schemaOnly))return false;}
            else if(!Reflect(element,elements[i].toObject(),item,generated,error,depth+1))return false;
            info.Default.Elements.push_back(std::move(item.Default));
        }
        return true;
    }
    if(templated&&(container=="std::map"||container=="std::unordered_map")) {
        GameSproutRuntime::ScriptFieldInfo key,value;key.ID="Key";key.Name="Key";value.ID="Value";value.Name="Value";
        if(!Reflect(arguments.value(0),{},key,generated,error,depth+1,true)||!Reflect(arguments.value(1),{},value,generated,error,depth+1,true))return false;
        if(key.Default.Kind!=ScriptValueKind::Boolean&&key.Default.Kind!=ScriptValueKind::Integer&&key.Default.Kind!=ScriptValueKind::Unsigned&&key.Default.Kind!=ScriptValueKind::String){error="Dictionary keys must be bool, integer, enum or string";return false;}
        info.Children={key,value};info.Default.Kind=ScriptValueKind::Dictionary;
        if(!EmptyDefault(initializer)) {
            auto expression=Unwrap(initializer);
            if(Kind(expression)=="CXXConstructExpr") {
                const auto parts=expression.value("inner").toArray();
                if(parts.isEmpty()||Kind(Unwrap(parts.first().toObject()))!="CXXStdInitializerListExpr"){error="Dictionary defaults must use an initializer list";return false;}expression=Unwrap(parts.first().toObject());
            }
            for(const auto& entry:Parts(expression)) {
                const auto pair=Parts(entry.toObject());if(pair.size()!=2){error="Dictionary defaults need key/value pairs";return false;}
                GameSproutRuntime::ScriptFieldInfo first,second;
                if(!Reflect(arguments[0],pair[0].toObject(),first,generated,error,depth+1)||!Reflect(arguments[1],pair[1].toObject(),second,generated,error,depth+1))return false;
                if(std::any_of(info.Default.Entries.begin(),info.Default.Entries.end(),[&](const auto& old){return old.first==first.Default;})){error="Duplicate dictionary default key";return false;}
                info.Default.Entries.emplace_back(std::move(first.Default),std::move(second.Default));
            }
        }
        return true;
    }
    if(templated||type.contains('[')||type.contains('(')){error="Unsupported serialized type: "+type.toStdString();return false;}
    if(_ScriptTypes.contains(type)){error="Script components must be referenced by pointer, not embedded by value: "+type.toStdString();return false;}
    QJsonObject declaration;if(!Record(type,declaration,error))return false;
    if(Kind(declaration)=="EnumDecl")return EnumValue(type,declaration,initializer,info,error,schemaOnly);
    return RecordValue(type,initializer,info,generated,error,depth,schemaOnly);
}

bool ScriptRecursiveFields::RecordValue(const QString& type,const QJsonObject& initializer,
    GameSproutRuntime::ScriptFieldInfo& info,QByteArray& generated,std::string& error,int depth,bool schemaOnly) {
    using GameSproutRuntime::ScriptValueKind;
    if(!_Visiting.insert(type).second){error="Recursive by-value/container schema is unsupported: "+type.toStdString();return false;}
    struct VisitingScope {
        std::set<QString>& Types;QString Type;
        ~VisitingScope(){Types.erase(Type);}
    } visitingScope{_Visiting,type};
    QJsonObject record;if(!Record(type,record,error))return false;
    if(record.value("tagUsed").toString()=="union"){error="Union fields cannot be serialized safely: "+type.toStdString();return false;}
    if(!NullRecordConstructor(record)){error="Serialized records must use implicit/defaulted constructors; cannot execute constructor to infer defaults: "+type.toStdString();return false;}
    std::vector<Member> members;if(!Members(type,record,members,error))return false;
    if(members.empty()){error="Serialized record has no accessible fields: "+type.toStdString();return false;}
    auto expression=Unwrap(initializer);QJsonArray parts;
    if(!EmptyDefault(expression)) {
        if(Kind(expression)!="InitListExpr"){error="Record default must use an aggregate initializer: "+type.toStdString();return false;}parts=Parts(expression);
        if(parts.size()>members.size()){error="Record initializer cannot be matched to reflected members: "+type.toStdString();return false;}
    }
    info.Default.Kind=ScriptValueKind::Structure;
    QStringList read,write,validate;
    for(std::size_t index=0;index<members.size();++index) {
        const auto& member=members[index];const QString name=member.Field.value("name").toString();const QString id=member.Owner+"::"+name;
        auto initial=Initializer(member.Field);
        if(index<static_cast<std::size_t>(parts.size())&&Kind(parts[qsizetype(index)].toObject())!="CXXDefaultInitExpr")initial=parts[qsizetype(index)].toObject();
        GameSproutRuntime::ScriptFieldInfo child;child.ID=id.toStdString();child.Name=name.toStdString();child.DeclaringType=member.Owner.toStdString();
        if(!Reflect(TypeName(member.Field),initial,child,generated,error,depth+1,schemaOnly))return false;
        info.Default.Fields.emplace(child.ID,child.Default);info.Children.push_back(child);
        const QString readMember="static_cast<const ::"+member.Owner+"&>(object)."+name;
        const QString writeMember="static_cast<::"+member.Owner+"&>(candidate)."+name;
        read.push_back("{ ScriptValue child; if(!ReadNativeValue("+readMember+",child,context,error)) return false; value.Fields.emplace("+Literal(id)+",std::move(child)); }");
        validate.push_back("{ auto child=value.Fields.find("+Literal(id)+"); if(child==value.Fields.end()) { error=\"Missing structure field\"; return false; } if(!ValidateNativeValue<decltype(::"+member.Owner+"::"+name+")>(child->second,context,error)) return false; }");
        write.push_back("if(!WriteNativeValue("+writeMember+",value.Fields.at("+Literal(id)+"),context,error)) return false;");
    }
    if(_Generated.insert(type).second) {
        QString code="namespace GameSproutRuntime::Internal { template<> struct ScriptFieldAccess<::"+type+"> { ";
        code+="static bool Read(const ::"+type+"& object,ScriptValue& value,const ScriptReferenceContext& context,std::string& error) { value={}; value.Kind=ScriptValueKind::Structure; "+read.join(' ')+" error.clear(); return true; } ";
        code+="static bool Validate(const ScriptValue& value,const ScriptReferenceContext& context,std::string& error) { if(value.Kind!=ScriptValueKind::Structure || value.Fields.size()!="+QString::number(members.size())+") { error=\"Structure field shape mismatch\"; return false; } "+validate.join(' ')+" error.clear(); return true; } ";
        code+="static bool Write(::"+type+"& object,const ScriptValue& value,const ScriptReferenceContext& context,std::string& error) { if(!Validate(value,context,error)) return false; static_assert(std::is_copy_constructible_v<::"+type+"> && std::is_copy_assignable_v<::"+type+">,\"Serialized record must support copying\"); ::"+type+" candidate=object; "+write.join(' ')+" object=std::move(candidate); error.clear(); return true; } }; }\n";
        generated+=code.toUtf8();
    }
    return true;
}

bool ScriptRecursiveFields::Generate(const QJsonObject& record,const QString& type,const QString& source,
    QByteArray& generated,std::vector<GameSproutRuntime::ScriptFieldInfo>& fields,std::string& warnings,std::string& error) {
    QStringList bindings;QString access=record.value("tagUsed").toString()=="struct"?"public":"private";
    for(const auto& entry:record.value("inner").toArray()) {
        const auto field=entry.toObject();if(Kind(field)=="AccessSpecDecl"){access=field.value("access").toString();continue;}
        if(Kind(field)!="FieldDecl")continue;const bool marked=_Marked(record,field);
        if(access!="public"&&!marked)continue;
        const QString name=field.value("name").toString();GameSproutRuntime::ScriptFieldInfo info;
        info.ID=(type+"::"+name).toStdString();info.Name=name.toStdString();info.DeclaringType=type.toStdString();
        std::string failure;QByteArray nestedGenerated;const auto generatedBefore=_Generated;
        if(name.isEmpty()||field.value("isBitfield").toBool()||!Reflect(TypeName(field),Initializer(field),info,nestedGenerated,failure,0)) {
            _Generated=generatedBefore;
            const std::string message=source.toStdString()+": 无法序列化字段 "+type.toStdString()+"::"+name.toStdString()+"（"+TypeName(field).toStdString()+"）："+failure+"\n";
            if(marked){error=message;return false;}
            warnings+=message;continue;
        }
        generated+=nestedGenerated;
        bindings.push_back("::GameSproutRuntime::Internal::MakeField<::"+type+">("+Literal(QString::fromStdString(info.ID))+","+Literal(name)+",&::"+type+"::"+name+","+InfoCode(info)+")");
        fields.push_back(std::move(info));
    }
    generated+=("namespace GameSproutRuntime::Internal { template<> struct ScriptFieldAccess<::"+type+"> { static std::vector<ScriptFieldRegistration> Fields() { return {"+bindings.join(",\n")+"}; } }; }\n").toUtf8();
    return true;
}
