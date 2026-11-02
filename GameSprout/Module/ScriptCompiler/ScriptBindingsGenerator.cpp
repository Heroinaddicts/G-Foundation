#include "ScriptBindingsGenerator.h"
#include "ScriptToolchain.h"
#include "ScriptBuildProcess.h"
#include "ScriptDeclarationValidator.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <set>
#include <cmath>
#include <limits>
#include <map>

namespace {
QString Kind(const QJsonObject& object) {return object.value("kind").toString();}
QString CanonicalType(const QJsonObject& object) {
    const QJsonObject type=object.value("type").toObject();
    return type.value("desugaredQualType").toString(type.value("qualType").toString());
}
QString Literal(const QString& value) {
    const QByteArray array=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1,array.size()-2));
}
bool Documents(const QByteArray& bytes,std::vector<QJsonObject>& documents,std::string& error) {
    int depth=0;qsizetype start=-1;bool string=false,escaped=false;
    for(qsizetype i=0;i<bytes.size();++i) {
        const char value=bytes[i];
        if(string) {if(escaped) escaped=false;else if(value=='\\') escaped=true;else if(value=='"') string=false;continue;}
        if(value=='"') {string=true;continue;}
        if(value=='{') {if(depth++==0) start=i;}
        else if(value=='}' && --depth==0 && start>=0) {
            QJsonParseError parse;
            const QJsonDocument document=QJsonDocument::fromJson(bytes.mid(start,i-start+1),&parse);
            if(parse.error!=QJsonParseError::NoError || !document.isObject()) {error="Clang returned invalid AST JSON";return false;}
            documents.push_back(document.object());start=-1;
        }
    }
    if(depth || string) {error="Clang returned truncated AST JSON";return false;}
    return true;
}
QStringList CompilerArguments(const QString& sourceRoot) {
    QStringList result GAMESPROUT_SCRIPT_AST_FLAGS;
    result<<QStringList{"-x","c++","-std=c++20","-fsyntax-only","-fno-color-diagnostics",
        "-Werror=overloaded-virtual","-Werror=shadow-field",
        "-I"+QString::fromUtf8(GAMESPROUT_SCRIPT_SDK),"-I"+sourceRoot};
    if(QString::fromUtf8(GAMESPROUT_SCRIPT_SYSROOT).size()) result<<"-isysroot"<<QString::fromUtf8(GAMESPROUT_SCRIPT_SYSROOT);
    if(QString::fromUtf8(GAMESPROUT_SCRIPT_DEPLOYMENT).size()) result<<"-mmacosx-version-min="+QString::fromUtf8(GAMESPROUT_SCRIPT_DEPLOYMENT);
    if(QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER_TARGET).size()) result<<"-target"<<QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER_TARGET);
    const QString architecture=QString::fromUtf8(GAMESPROUT_SCRIPT_ARCH).section(';',0,0);
    if(!architecture.isEmpty()) result<<"-arch"<<architecture;
    return result;
}
bool Ast(const QString& path,const QString& filter,const QString& sourceRoot,
         std::vector<QJsonObject>& documents,std::string& error,const std::function<bool()>& cancelled) {
    QStringList arguments=CompilerArguments(sourceRoot);
    arguments<<"-Xclang"<<"-ast-dump=json"<<"-Xclang"<<"-ast-dump-filter="+filter<<path;
    ScriptBuildProcess::Result process;
    if(!ScriptBuildProcess::Run(QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER),arguments,sourceRoot,false,64*1024*1024,process,cancelled)) {
        error="Clang reflection failed:\n"+process.Error.toStdString()+"\n"+process.ErrorOutput.toStdString();return false;
    }
    return Documents(process.Output,documents,error);
}
bool SemanticOverrides(const QString& path,const QString& filter,const QString& sourceRoot,
    QJsonObject& record,std::string& error,const std::function<bool()>& cancelled) {
    bool needed=false;
    for(const auto& entry:record.value("inner").toArray()) {
        const auto member=entry.toObject();
        if(Kind(member)!="CXXMethodDecl" || member.value("isImplicit").toBool()) continue;
        bool explicitOverride=false;
        for(const auto& child:member.value("inner").toArray())
            if(Kind(child.toObject())=="OverrideAttr") explicitOverride=true;
        if(!explicitOverride && member.value("storageClass").toString()!="static") needed=true;
    }
    if(!needed) return true;
    QStringList arguments=CompilerArguments(sourceRoot);
    arguments<<"-Xclang"<<"-ast-dump"<<"-Xclang"<<"-ast-dump-filter="+filter<<path;
    ScriptBuildProcess::Result process;
    if(!ScriptBuildProcess::Run(QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER),arguments,sourceRoot,false,
        64*1024*1024,process,cancelled)) {
        error="Clang declaration analysis failed:\n"+process.Error.toStdString()+"\n"+process.ErrorOutput.toStdString();return false;
    }
    ScriptDeclarationValidator::MarkSemanticOverrides(record,process.Output);
    return true;
}
QString ClassArgument(const QJsonObject& node) {
    if(Kind(node)=="TemplateSpecializationType" && node.value("templateName").toString().endsWith("GameSproutRuntime::Internal::ScriptMarker")) {
        for(const QJsonValue& child:node.value("inner").toArray()) {
            const QJsonObject argument=child.toObject();
            if(Kind(argument)=="TemplateArgument") return CanonicalType(argument);
        }
    }
    for(const QJsonValue& child:node.value("inner").toArray()) {const QString found=ClassArgument(child.toObject());if(!found.isEmpty()) return found;}
    return {};
}
QString LocationFile(const QJsonObject& marker) {
    const QJsonObject location=marker.value("loc").toObject();
    const QString expansion=location.value("expansionLoc").toObject().value("file").toString();
    if(!expansion.isEmpty()) return expansion;
    return location.value("file").toString();
}
bool Scalar(const QJsonObject& node,QString& expression) {
    const QString kind=Kind(node);
    const QJsonArray children=node.value("inner").toArray();
    if(kind=="IntegerLiteral" || kind=="FloatingLiteral") {
        expression=node.value("value").toString();
        bool valid=false;expression.toDouble(&valid);
        if(!valid || expression.isEmpty()) return false;
        const QString type=CanonicalType(node);
        if(kind=="IntegerLiteral" && type.startsWith("unsigned ")) expression+="ULL";
        expression="static_cast<"+type+">("+expression+')';return true;
    }
    if(kind=="CharacterLiteral") {expression=QString::number(node.value("value").toInteger());return true;}
    if(kind=="CXXBoolLiteralExpr") {expression=node.value("value").toBool()?"true":"false";return true;}
    if(kind=="ImplicitValueInitExpr" || kind=="CXXScalarValueInitExpr") {expression="0";return true;}
    if(kind=="InitListExpr" && children.size()<=1) {
        if(children.isEmpty()) {expression="0";return true;}
        return Scalar(children.first().toObject(),expression);
    }
    if((kind=="ImplicitCastExpr" || kind=="ParenExpr" || kind=="ConstantExpr" || kind=="ExprWithCleanups" || kind=="CXXFunctionalCastExpr" || kind=="CStyleCastExpr" || kind=="CXXStaticCastExpr") && children.size()==1) {
        if(!Scalar(children.first().toObject(),expression)) return false;
        if(kind=="ImplicitCastExpr" || kind=="CXXFunctionalCastExpr" || kind=="CStyleCastExpr" || kind=="CXXStaticCastExpr")
            expression="static_cast<"+CanonicalType(node)+">("+expression+')';
        return true;
    }
    if(kind=="UnaryOperator" && children.size()==1) {
        const QString op=node.value("opcode").toString();
        if(op!="+" && op!="-" && op!="!" && op!="~") return false;
        QString value;if(!Scalar(children.first().toObject(),value)) return false;
        expression='('+op+'('+value+"))";return true;
    }
    if(kind=="BinaryOperator" && children.size()==2) {
        static const std::set<QString> safe={"+","-","*","/","%","<<",">>","&","|","^","&&","||","==","!=","<",">","<=",">="};
        const QString op=node.value("opcode").toString();QString a,b;
        if(!safe.contains(op) || !Scalar(children[0].toObject(),a) || !Scalar(children[1].toObject(),b)) return false;
        expression="(("+a+')'+op+'('+b+"))";return true;
    }
    return false;
}
bool String(const QJsonObject& node,QString& expression) {
    const QString kind=Kind(node);
    if(kind=="StringLiteral") {expression=node.value("value").toString();return expression.startsWith('"') && expression.endsWith('"');}
    const QJsonArray children=node.value("inner").toArray();
    if((kind=="ImplicitCastExpr" || kind=="MaterializeTemporaryExpr" || kind=="CXXBindTemporaryExpr" || kind=="ExprWithCleanups") && children.size()==1)
        return String(children.first().toObject(),expression);
    if(kind=="CXXConstructExpr") {
        if(children.isEmpty()) {expression="\"\"";return true;}
        // Only a literal string constructor is safe: never run an arbitrary call
        // or concatenate values by executing the user's constructor in Editor.
        bool allDefault=true;
        for(const QJsonValue& child:children) if(Kind(child.toObject())!="CXXDefaultArgExpr") allDefault=false;
        if(allDefault) {expression="\"\"";return true;}
        bool defaultsOnly=true;
        for(qsizetype i=1;i<children.size();++i) if(Kind(children[i].toObject())!="CXXDefaultArgExpr") defaultsOnly=false;
        if(defaultsOnly) return String(children.first().toObject(),expression);
    }
    return false;
}
enum class FieldKind {Unsupported,Scalar,String,Vector,Resource,SceneObject};
FieldKind Supported(QString type) {
    type.replace("std::__1::","std::");type.replace("std::__cxx11::","std::");
    if(type.simplified()=="GameSproutRuntime::IGameObject *" || type.simplified()=="GameSproutRuntime::IGameObject*") return FieldKind::SceneObject;
    if(type.contains('*') || type.contains('&') || type.startsWith("const ") || type.startsWith("volatile ")) return FieldKind::Unsupported;
    static const std::set<QString> numeric={"bool","signed char","short","short int","int","long","long int","long long","long long int","float","double"};
    if(numeric.contains(type)) return FieldKind::Scalar;
    if(type=="std::string" || type=="std::basic_string<char>" || type=="std::basic_string<char, std::char_traits<char>, std::allocator<char>>") return FieldKind::String;
    if(type=="GameSproutRuntime::Vector3") return FieldKind::Vector;
    if(type=="GameSproutRuntime::ResourceReference") return FieldKind::Resource;
    return FieldKind::Unsupported;
}
bool Default(const QJsonObject& field,FieldKind kind,QString& expression) {
    QJsonArray initializers;
    for(const auto& value:field.value("inner").toArray())if(!Kind(value.toObject()).endsWith("Attr"))initializers.append(value);
    if(!field.value("hasInClassInitializer").toBool()) {expression="{}";return true;}
    if(initializers.size()!=1) return false;
    QJsonObject initializer=initializers.first().toObject();
    if(kind==FieldKind::Scalar) return Scalar(initializer,expression);
    if(kind==FieldKind::String) return String(initializer,expression);
    if(kind==FieldKind::SceneObject) {
        while(Kind(initializer)=="ImplicitCastExpr" && initializer.value("inner").toArray().size()==1)initializer=initializer.value("inner").toArray().first().toObject();
        if(Kind(initializer)=="CXXNullPtrLiteralExpr" || (Kind(initializer)=="InitListExpr" && initializer.value("inner").toArray().isEmpty())) {expression="{}";return true;}
        return false;
    }
    while((Kind(initializer)=="ExprWithCleanups" || Kind(initializer)=="CXXFunctionalCastExpr" || Kind(initializer)=="MaterializeTemporaryExpr") && initializer.value("inner").toArray().size()==1)
        initializer=initializer.value("inner").toArray().first().toObject();
    const QString initializerKind=Kind(initializer);
    const QJsonArray parts=initializer.value("inner").toArray();
    if(initializerKind!="InitListExpr" && initializerKind!="CXXConstructExpr") return false;
    if(parts.isEmpty()) {expression="{}";return true;}
    QStringList values;
    if(kind==FieldKind::Vector && parts.size()<=3) {
        for(const QJsonValue& part:parts) {QString value;if(!Scalar(part.toObject(),value)) return false;values.push_back(value);}
    } else if(kind==FieldKind::Resource && parts.size()<=2) {
        QString guid;if(!String(parts.first().toObject(),guid)) return false;values.push_back(guid);
        if(parts.size()==2) {
            QString id;
            if(Kind(parts[1].toObject())=="CXXDefaultInitExpr") id="0";
            else if(!Scalar(parts[1].toObject(),id)) return false;
            values.push_back("([]() consteval {return static_cast<std::uint64_t>("+id+");}())");
        }
    } else return false;
    expression='{'+values.join(',')+'}';return true;
}
// Keep integer defaults exact: long double is only 64-bit on Apple silicon.
// The AST's integer literals and operations are evaluated without executing
// constructors or converting int64/LocalID through JSON floating-point values.
bool IntegerType(const QString& type,__int128& value) {
    int width=0;bool unsignedType=type.startsWith("unsigned ");
    if(type=="bool"){value=value!=0;return true;}
    if(type.contains("char"))width=8;
    else if(type.contains("short"))width=16;
    else if(type.contains("long long"))width=64;
    else if(type=="long"||type=="long int"||type=="unsigned long"||type=="unsigned long int")width=sizeof(long)*8;
    else if(type=="int"||type=="unsigned int"||type=="unsigned")width=32;
    if(!width)return false;
    const __int128 modulus=__int128(1)<<width;
    value%=modulus;if(value<0)value+=modulus;
    if(!unsignedType&&value>=(modulus>>1))value-=modulus;
    return true;
}
bool Integer(const QJsonObject& node,__int128& value) {
    const auto kind=Kind(node);const auto children=node.value("inner").toArray();
    if(kind=="IntegerLiteral") {bool valid=false;const auto literal=node.value("value").toString();value=literal.toULongLong(&valid);return valid;}
    if(kind=="CharacterLiteral"){value=node.value("value").toInteger();return true;}
    if(kind=="CXXBoolLiteralExpr"){value=node.value("value").toBool();return true;}
    if(kind=="ImplicitValueInitExpr"||kind=="CXXScalarValueInitExpr"||kind=="CXXDefaultInitExpr"||(kind=="InitListExpr"&&children.isEmpty())){value=0;return true;}
    if(kind=="UnaryOperator"&&children.size()==1) {
        if(!Integer(children.first().toObject(),value))return false;const auto op=node.value("opcode").toString();
        if(op=="-")value=-value;else if(op=="!")value=!value;else if(op=="~")value=~value;else if(op!="+")return false;
    } else if(kind=="BinaryOperator"&&children.size()==2) {
        __int128 a,b;if(!Integer(children[0].toObject(),a)||!Integer(children[1].toObject(),b))return false;
        const auto op=node.value("opcode").toString();
        if(op=="+")value=a+b;else if(op=="-")value=a-b;else if(op=="*"){
            if((a>std::numeric_limits<std::int64_t>::max()||b>std::numeric_limits<std::int64_t>::max())&&a&&b)return false;value=a*b;
        }else if(op=="/"){if(!b)return false;value=a/b;}else if(op=="%"){if(!b)return false;value=a%b;}
        else if(op=="&")value=a&b;else if(op=="|")value=a|b;else if(op=="^")value=a^b;
        else if(op=="<<"||op==">>"){if(b<0||b>=64||a<0)return false;value=op=="<<"?a<<int(b):a>>int(b);}
        else if(op=="==")value=a==b;else if(op=="!=")value=a!=b;else if(op=="<")value=a<b;else if(op==">")value=a>b;
        else if(op=="<=")value=a<=b;else if(op==">=")value=a>=b;else if(op=="&&")value=a&&b;else if(op=="||")value=a||b;else return false;
    } else if(children.size()==1){if(!Integer(children.first().toObject(),value))return false;}
    else return false;
    return IntegerType(CanonicalType(node),value);
}
bool Evaluate(const QJsonObject& node,long double& value) {
    __int128 integer;if(Integer(node,integer)){value=static_cast<long double>(integer);return true;}
    const QString kind=Kind(node);const QJsonArray children=node.value("inner").toArray();
    if(kind=="IntegerLiteral"||kind=="FloatingLiteral") {try {value=std::stold(node.value("value").toString().toStdString());return std::isfinite(value);}catch(...){return false;}}
    if(kind=="CXXBoolLiteralExpr") {value=node.value("value").toBool()?1:0;return true;}
    if(kind=="CharacterLiteral") {value=node.value("value").toInteger();return true;}
    if(kind=="ImplicitValueInitExpr"||kind=="CXXScalarValueInitExpr"||(kind=="InitListExpr"&&children.isEmpty())) {value=0;return true;}
    if(kind=="UnaryOperator"&&children.size()==1) {
        if(!Evaluate(children.first().toObject(),value))return false;
        const auto op=node.value("opcode").toString();
        if(op=="-")value=-value;else if(op=="!")value=!value;else if(op=="~")value=~static_cast<std::int64_t>(value);else if(op!="+")return false;
        return std::isfinite(value);
    }
    if(kind=="BinaryOperator"&&children.size()==2) {
        long double a,b;if(!Evaluate(children[0].toObject(),a)||!Evaluate(children[1].toObject(),b))return false;
        const auto op=node.value("opcode").toString();
        if(op=="+")value=a+b;else if(op=="-")value=a-b;else if(op=="*")value=a*b;else if(op=="/"){if(b==0)return false;value=a/b;}
        else if(op=="%") {if(b==0)return false;value=std::fmod(a,b);}
        else if(op=="==")value=a==b;else if(op=="!=")value=a!=b;else if(op=="<")value=a<b;else if(op==">")value=a>b;
        else if(op=="<=")value=a<=b;else if(op==">=")value=a>=b;else if(op=="&&")value=a&&b;else if(op=="||")value=a||b;
        else if(op=="&")value=static_cast<std::int64_t>(a)&static_cast<std::int64_t>(b);
        else if(op=="|")value=static_cast<std::int64_t>(a)|static_cast<std::int64_t>(b);
        else if(op=="^")value=static_cast<std::int64_t>(a)^static_cast<std::int64_t>(b);
        else if(op=="<<"||op==">>")return false;
        else return false;
    } else if(children.size()==1) {
        if(!Evaluate(children.first().toObject(),value))return false;
    } else return false;
    const QString type=CanonicalType(node);
    if(type=="bool")value=value!=0;else if(type=="float")value=static_cast<float>(value);else if(type=="double")value=static_cast<double>(value);
    else if(!type.isEmpty()&&!type.contains('*')&&!type.contains("float")&&!type.contains("double"))value=std::trunc(value);
    return std::isfinite(value);
}
bool DecodeLiteral(const QString& literal,std::string& value) {
    const QByteArray bytes=literal.toUtf8();
    if(bytes.size()<2||bytes.front()!='"'||bytes.back()!='"')return false;
    QByteArray decoded;
    for(qsizetype i=1;i+1<bytes.size();++i) {
        char next=bytes[i];if(next!='\\'){decoded+=next;continue;}
        if(++i+1>=bytes.size())return false;next=bytes[i];
        switch(next) {
        case '\\':case '"':case '\'':case '?':decoded+=next;break;
        case 'a':decoded+='\a';break;case 'b':decoded+='\b';break;case 'f':decoded+='\f';break;
        case 'n':decoded+='\n';break;case 'r':decoded+='\r';break;case 't':decoded+='\t';break;case 'v':decoded+='\v';break;
        default:{
            unsigned number=0;int count=0;
            if(next>='0'&&next<='7') {
                for(;count<3&&i+1<bytes.size()&&bytes[i]>='0'&&bytes[i]<='7';++count,++i)number=number*8+unsigned(bytes[i]-'0');
                --i;
            } else if(next=='x') {
                while(i+2<bytes.size()) {
                    const char digit=bytes[i+1];int hex=digit>='0'&&digit<='9'?digit-'0':digit>='a'&&digit<='f'?digit-'a'+10:digit>='A'&&digit<='F'?digit-'A'+10:-1;
                    if(hex<0)break;number=number*16+unsigned(hex);++i;++count;if(number>255)return false;
                }
            } else return false;
            if(!count||number>255)return false;decoded+=char(number);break;
        }
        }
    }
    if(decoded.contains('\0')||QString::fromUtf8(decoded).toUtf8()!=decoded)return false;
    value=decoded.toStdString();return true;
}
bool DefaultValue(const QJsonObject& field,FieldKind kind,GameSproutRuntime::ScriptValue& value) {
    using GameSproutRuntime::ScriptValueKind;
    if(kind==FieldKind::SceneObject){value.Kind=ScriptValueKind::SceneObject;return true;}
    QJsonObject expression;
    for(const auto& child:field.value("inner").toArray())if(!Kind(child.toObject()).endsWith("Attr")){expression=child.toObject();break;}
    const QString type=CanonicalType(field);
    if(kind==FieldKind::Scalar) {
        long double number=0;if(!expression.isEmpty()&&!Evaluate(expression,number))return false;
        if(type=="bool"){value.Kind=ScriptValueKind::Boolean;value.Boolean=number!=0;}
        else if(type=="float"||type=="double"){value.Kind=ScriptValueKind::Number;value.Number=type=="float"?double(float(number)):double(number);if(!std::isfinite(value.Number))return false;}
        else {
            __int128 exact=0;
            if(!expression.isEmpty()&&!Integer(expression,exact)) {
                if(number < -std::ldexp(1.0L,63)||number >= std::ldexp(1.0L,63))return false;
                exact=static_cast<std::int64_t>(number);
            }
            if(exact<std::numeric_limits<std::int64_t>::min()||exact>std::numeric_limits<std::int64_t>::max())return false;
            value.Kind=ScriptValueKind::Integer;value.Integer=static_cast<std::int64_t>(exact);
        }
        return true;
    }
    if(kind==FieldKind::String) {
        value.Kind=ScriptValueKind::String;if(expression.isEmpty())return true;QString literal;
        if(!String(expression,literal))return false;
        return DecodeLiteral(literal,value.String);
    }
    while((Kind(expression)=="ExprWithCleanups"||Kind(expression)=="CXXFunctionalCastExpr"||Kind(expression)=="MaterializeTemporaryExpr")&&expression.value("inner").toArray().size()==1)expression=expression.value("inner").toArray().first().toObject();
    const auto parts=expression.value("inner").toArray();
    if(kind==FieldKind::Vector) {
        value.Kind=ScriptValueKind::Vector3;float* values[]={&value.Vector.X,&value.Vector.Y,&value.Vector.Z};
        for(qsizetype i=0;i<parts.size();++i){long double number;if(i>=3||!Evaluate(parts[i].toObject(),number))return false;*values[i]=float(number);if(!std::isfinite(*values[i]))return false;}return true;
    }
    if(kind==FieldKind::Resource) {
        value.Kind=ScriptValueKind::Resource;
        if(!parts.isEmpty()){QString literal;if(!String(parts.first().toObject(),literal)||!DecodeLiteral(literal,value.Resource.Guid))return false;}
        if(parts.size()==2&&Kind(parts[1].toObject())!="CXXDefaultInitExpr"){__int128 number;if(!Integer(parts[1].toObject(),number)||number<0||number>std::numeric_limits<std::uint64_t>::max())return false;value.Resource.LocalID=static_cast<std::uint64_t>(number);}return true;
    }
    return false;
}
bool Marked(const QJsonObject& record,const QJsonObject& field) {
    for(const auto& child:field.value("inner").toArray()) {
        const auto attribute=child.toObject();if(Kind(attribute)!="AnnotateAttr")continue;
        const auto expansion=attribute.value("range").toObject().value("begin").toObject().value("expansionLoc").toObject();
        if(expansion.isEmpty())continue;
        for(const auto& entry:record.value("inner").toArray()) {
            const auto declaration=entry.toObject();if(Kind(declaration)!="FriendDecl")continue;
            if(declaration.value("loc").toObject().value("expansionLoc").toObject().value("offset")!=expansion.value("offset"))continue;
            for(const auto& part:declaration.value("inner").toArray())if(part.toObject().value("name").toString()=="ScriptFieldAccess")return true;
        }
    }
    return false;
}
struct DeclaredFields {QJsonObject Record;std::vector<GameSproutRuntime::ScriptFieldInfo> Fields;};
bool Fields(const QJsonObject& record,const QString& type,std::string& warnings,const QString& source,
            QByteArray& generated,std::vector<GameSproutRuntime::ScriptFieldInfo>& schema,std::string& error) {
    QStringList fields;QString access=record.value("tagUsed").toString()=="struct"?"public":"private";
    for(const auto& child:record.value("inner").toArray()) {
        const auto field=child.toObject();if(Kind(field)=="AccessSpecDecl"){access=field.value("access").toString();continue;}
        if(Kind(field)!="FieldDecl")continue;const bool marked=Marked(record,field);
        if(access!="public"&&!marked)continue;
        const QString name=field.value("name").toString();const auto kind=Supported(CanonicalType(field));QString value;GameSproutRuntime::ScriptFieldInfo info;
        if(name.isEmpty()||kind==FieldKind::Unsupported||field.value("isBitfield").toBool()||!Default(field,kind,value)||!DefaultValue(field,kind,info.Default)) {
            const auto message=source.toStdString()+": "+type.toStdString()+"::"+name.toStdString()+" 不支持自动持久化，字段不会显示或保存（保持运行时初始化）。\n";
            if(marked){error=message;return false;}warnings+=message;continue;
        }
        const QString member="::"+type+"::"+name;
        QString typed=value=="{}"?"decltype("+member+"){}":"decltype("+member+")"+(value.startsWith('{')?value:'('+value+')');
        if(kind==FieldKind::Scalar||kind==FieldKind::Vector)typed="([]() consteval {return "+typed+";}())";
        const QString id=type+"::"+name;
        fields.push_back("::GameSproutRuntime::Internal::MakeField<::"+type+">("+Literal(id)+','+Literal(name)+",&"+member+','+typed+",::GameSproutRuntime::ScriptResourceKind::Any,"+Literal(type)+')');
        info.ID=id.toStdString();info.Name=name.toStdString();info.DeclaringType=type.toStdString();schema.push_back(std::move(info));
    }
    generated+=("namespace GameSproutRuntime::Internal { template<> struct ScriptFieldAccess<::"+type+"> { static std::vector<ScriptFieldRegistration> Fields() { return {"+fields.join(",\n")+"}; } }; }\n").toUtf8();
    return true;
}
bool Record(const QString& type,const QString& probe,const QString& sourceRoot,QJsonObject& record,std::string& error,const std::function<bool()>& cancelled) {
    std::vector<QJsonObject> declarations;if(!Ast(probe,type,sourceRoot,declarations,error,cancelled))return false;
    for(const auto& declaration:declarations)if(Kind(declaration)=="CXXRecordDecl"&&declaration.value("completeDefinition").toBool()&&declaration.value("name").toString()==type.section("::",-1)) {
        if(!record.isEmpty()){error="Clang AST class selection is ambiguous: "+type.toStdString();return false;}record=declaration;
    }
    if(record.isEmpty()){error="Clang did not expose the complete script class: "+type.toStdString();return false;}return true;
}
bool Collect(const QString& type,const QString& probe,const QString& sourceRoot,const QString& source,
             std::map<QString,DeclaredFields>& declarations,std::set<QString>& visiting,std::set<QString>& visited,QStringList& calls,
             QByteArray& generated,std::vector<GameSproutRuntime::ScriptFieldInfo>& schema,std::string& warnings,std::string& error,const std::function<bool()>& cancelled) {
    if(type=="GameSproutRuntime::ScriptBehaviour"||type=="GameSproutRuntime::IScript"||type=="GameSproutRuntime::IComponent")return true;
    if(visited.contains(type))return true;if(!visiting.insert(type).second){error="Cyclic script field inheritance";return false;}
    auto existing=declarations.find(type);if(existing==declarations.end()) {
        DeclaredFields data;if(!Record(type,probe,sourceRoot,data.Record,error,cancelled))return false;
        existing=declarations.emplace(type,std::move(data)).first;
    }
    for(const auto& entry:existing->second.Record.value("bases").toArray()) {
        const auto base=entry.toObject();QString baseType=CanonicalType(base);if(baseType.startsWith("class "))baseType=baseType.mid(6);if(baseType.startsWith("struct "))baseType=baseType.mid(7);
        if(base.value("access").toString()!="public") {error="Serialized script bases must use public inheritance: "+baseType.toStdString();return false;}
        if(!Collect(baseType,probe,sourceRoot,source,declarations,visiting,visited,calls,generated,schema,warnings,error,cancelled))return false;
    }
    if(!existing->second.Record.value("GameSproutAccessorGenerated").toBool()) {
        if(!Fields(existing->second.Record,type,warnings,source,generated,existing->second.Fields,error))return false;
        existing->second.Record["GameSproutAccessorGenerated"]=true;
    }
    schema.insert(schema.end(),existing->second.Fields.begin(),existing->second.Fields.end());
    calls.push_back("{ auto declared = ::GameSproutRuntime::Internal::ScriptFieldAccess<::"+type+">::Fields(); fields.insert(fields.end(),std::make_move_iterator(declared.begin()),std::make_move_iterator(declared.end())); }");
    visiting.erase(type);visited.insert(type);return true;
}

}

bool ScriptBindingsGenerator::Generate(const ScriptCompileRequest& request,const QString& sourceRoot,
    const QString& probePath,QByteArray& generated,std::vector<GameSproutRuntime::ScriptTypeInfo>& types,std::string& diagnostics,std::string& warnings,
    const std::function<bool()>& cancelled) {
    const QString compiler=QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER_ID);
    if((compiler!="Clang" && compiler!="AppleClang") || QString::fromUtf8(GAMESPROUT_SCRIPT_FRONTEND)=="MSVC") {
        diagnostics="Native script field generation requires a GCC-compatible Clang/AppleClang driver with JSON AST support; current compiler: "+compiler.toStdString();return false;
    }
    std::vector<QJsonObject> markers;
    if(!Ast(probePath,"GameSproutScriptMarker_",sourceRoot,markers,diagnostics,cancelled)) return false;
    // Helper .cpp files remain independent TUs; script declarations must live in
    // .hpp entries so the generated binding TU can name the class definition.
    for(const auto& source:request.Sources) {
        const QString extension=QFileInfo(QString::fromStdString(source.Path)).suffix().toLower();
        if(extension!="cpp" && extension!="cc" && extension!="cxx") continue;
        std::vector<QJsonObject> helperMarkers;
        if(!Ast(QDir(sourceRoot).filePath(QString::fromStdString(source.Path)),"GameSproutScriptMarker_",sourceRoot,helperMarkers,diagnostics,cancelled)) return false;
        for(const auto& marker:helperMarkers) {
            const QString file=QFileInfo(LocationFile(marker)).canonicalFilePath();
            if(file==QFileInfo(QDir(sourceRoot).filePath(QString::fromStdString(source.Path))).canonicalFilePath()) {
                diagnostics="GAMESPROUT_SCRIPT must be declared in a .hpp asset, not "+source.Path;return false;
            }
        }
    }
    std::set<QString> seenFiles,seenTypes;
    std::map<QString,DeclaredFields> declarations;
    std::map<QString,QJsonObject> validatedRecords;
    QString previousFile;
    for(const QJsonObject& marker:markers) {
        if(Kind(marker)!="TypeAliasDecl") continue;
        const QString type=ClassArgument(marker);
        QString file=LocationFile(marker);if(file.isEmpty()) file=previousFile;else previousFile=file;
        file=QFileInfo(file).canonicalFilePath();
        const QString relative=QDir(QFileInfo(sourceRoot).canonicalFilePath()).relativeFilePath(file);
        const auto asset=std::find_if(request.Sources.begin(),request.Sources.end(),[&](const auto& value){return value.Path==relative.toStdString();});
        if(type.isEmpty() || type.contains("(anonymous namespace)") || asset==request.Sources.end() || QFileInfo(relative).suffix().toLower()!="hpp") {
            diagnostics="Cannot map Clang script marker to a supported .hpp asset: "+type.toStdString()+" "+file.toStdString();return false;
        }
        const QString guid=QString::fromStdString(asset->SourceGuid);
        bool validGuid=guid.size()==32;
        for(const QChar value:guid) if(!((value>='0' && value<='9') || (value>='a' && value<='f') || (value>='A' && value<='F'))) validGuid=false;
        if(!validGuid || guid==QString(32,'0')) {diagnostics="Script source has no valid .meta GUID: "+relative.toStdString();return false;}
        if(!seenFiles.insert(file).second) {diagnostics="Each .hpp asset supports one GAMESPROUT_SCRIPT class: "+relative.toStdString();return false;}
        if(!seenTypes.insert(type).second) {diagnostics="A script class was marked more than once: "+type.toStdString();return false;}
        const auto loadDeclaration=[&](const QString& declaredType,QJsonObject& record,std::string& error) {
            const auto found=validatedRecords.find(declaredType);
            if(found!=validatedRecords.end()) {record=found->second;return true;}
            if(!Record(declaredType,probePath,sourceRoot,record,error,cancelled) ||
               !SemanticOverrides(probePath,declaredType,sourceRoot,record,error,cancelled)) return false;
            validatedRecords.emplace(declaredType,record);
            return true;
        };
        if(!ScriptDeclarationValidator::Validate(type,relative,loadDeclaration,diagnostics)) return false;
        GameSproutRuntime::ScriptTypeInfo schema;schema.TypeID=asset->SourceGuid;schema.Name=type.toStdString();schema.SourceFile=file.toStdString();
        std::set<QString> visiting,visited;QStringList calls;
        if(!Collect(type,probePath,sourceRoot,relative,declarations,visiting,visited,calls,generated,schema.Fields,warnings,diagnostics,cancelled))return false;
        const QString fieldBindings="([] { std::vector<::GameSproutRuntime::ScriptFieldRegistration> fields; "+calls.join(' ')+" return fields; }())";
        types.push_back(std::move(schema));
        generated+="namespace { const bool GameSproutGeneratedScript_"+QByteArray::number(seenTypes.size())+" =\n";
        generated+=("::GameSproutRuntime::Internal::RegisterScriptClass<::"+type+">("+Literal(QString::fromStdString(asset->SourceGuid))+','+Literal(type)+','+Literal(file)+",::GameSproutRuntime::ResourceReference{"+Literal(QString::fromStdString(asset->SourceGuid))+",1},"+fieldBindings+"); }\n").toUtf8();
    }
    return true;
}
