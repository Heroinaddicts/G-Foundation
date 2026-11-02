#include "ScriptCompiler.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void Write(const QString& path,const QByteArray& bytes){QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Open fixture");Check(file.write(bytes)==bytes.size(),"Write fixture");}
QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),"Read fixture");return file.readAll();}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    QTemporaryDir temporary;GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    QLibrary library;
    try {
        Check(temporary.isValid(),"Temporary project");const QString assets=temporary.path()+"/Assets";Check(QDir().mkpath(assets),"Assets directory");
        const QByteArray source=R"(#pragma once
#include "RuntimeScriptModule.h"
#include <stdexcept>
using namespace GameSproutRuntime;
class Base : public GameSproutRuntime::ScriptBehaviour {
private:
    GAMESPROUT_SERIALIZE_FIELD
    int _Secret = 3;
    int Ignored = 99;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    float _Speed = 2.5f;
public:
    float BasePublic = 4;
};
class Child final : public Base {
private:
    GAMESPROUT_SERIALIZE_FIELD
    IGameObject* _Target;
    GAMESPROUT_SERIALIZE_FIELD
    int _ChildSecret = 7;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    std::string _Title = "玩家";
public:
    int Count = 6 * 7;
    std::int64_t Exact = 9223372036854775807LL;
    ResourceReference Token = {"abcdef0123456789abcdef0123456789",18446744073709551615ULL};
    Vector3 Direction = {1,2,3};
protected:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        if(!_Target || Count!=42 || _Title!="玩家")throw std::runtime_error("references/defaults not restored before OnCreate");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        auto position=_GameObject->GetTransform()->GetLocalPosition();position.Y=_Speed+_ChildSecret;_GameObject->GetTransform()->SetLocalPosition(position);
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {
        if(_Target){auto position=_Target->GetTransform()->GetLocalPosition();position.X+=1;_Target->GetTransform()->SetLocalPosition(position);}
    }
};
GAMESPROUT_SCRIPT(Child)
)";
        Write(assets+"/Child.hpp",source);ScriptCompiler compiler;ScriptCompileRequest request;
        request.AssetsRoot=assets.toStdString();request.BuildRoot=(temporary.path()+"/Library/ScriptBuild").toStdString();request.Sources={{"Child.hpp","00112233445566778899aabbccddeeff"}};
        ScriptCompileResult compiled;Check(compiler.Compile(request,compiled,[]{return false;}),compiled.Diagnostics);
        Check(compiled.Types.size()==1&&compiled.Types[0].Fields.size()==10,"Private/protected/inherited fields schema is incomplete: "+compiled.ReflectionWarnings);
        Check(!compiled.SchemaPath.empty()&&QCryptographicHash::hash(Read(QString::fromStdString(compiled.SchemaPath)),QCryptographicHash::Sha256).toHex().toStdString()==compiled.SchemaHash,"Pure metadata schema missing/hash mismatch");
        bool baseSecret=false,childSecret=false,target=false;
        for(const auto& field:compiled.Types[0].Fields) {
            baseSecret|=field.ID=="Base::_Secret"&&field.Default.Integer==3;
            childSecret|=field.ID=="Child::_ChildSecret"&&field.Default.Integer==7;
            target|=field.ID=="Child::_Target"&&field.Default.Kind==GameSproutRuntime::ScriptValueKind::SceneObject&&!field.Default.SceneObject.Assigned;
            Check(field.ID.find("Ignored")==std::string::npos,"Unmarked private field leaked");
            if(field.ID=="Child::Exact")Check(field.Default.Integer==std::numeric_limits<std::int64_t>::max(),"int64 default lost precision");
            if(field.ID=="Child::Token")Check(field.Default.Resource.LocalID==std::numeric_limits<std::uint64_t>::max(),"LocalID default lost precision");
            if(field.ID=="Child::Direction")Check(field.Default.Kind==GameSproutRuntime::ScriptValueKind::Vector3&&field.Default.Vector.Y==2,"Unqualified SDK Vector3 was not resolved");
        }
        Check(baseSecret&&childSecret&&target,"Declaring-class identities or reference kind lost");
        ScriptCompileResult cached;Check(compiler.Compile(request,cached,[]{return false;})&&cached.CacheHit&&cached.Types.size()==1&&cached.Types[0].Fields.size()==10,"Cached result lost schema");
        Write(QString::fromStdString(compiled.SchemaPath),"{\"Version\":1,\"BuildID\":\"forged\",\"Types\":[]}");
        ScriptCompileResult repaired;Check(compiler.Compile(request,repaired,[]{return false;})&&!repaired.CacheHit&&repaired.Types.size()==1,"Damaged schema was accepted by cache");
        library.setFileName(QString::fromStdString(repaired.LibraryPath));Check(library.load(),library.errorString().toStdString());
        const auto entry=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(library.resolve(GameSproutRuntime::ScriptModuleEntryName));Check(entry,"Module entry");const auto* module=entry();
        Check(module->Version==GameSproutRuntime::ScriptModuleABIVersion&&module->BuildID&&repaired.Fingerprint==module->BuildID&&module->Count==1,"Native schema/module build identity mismatch");
        const auto& type=module->Types[0];Check(type.Info.Fields.size()==10,"Generated native bindings differ from pure schema");
        for(std::size_t i=0;i<type.Info.Fields.size();++i)Check(type.Info.Fields[i].ID==repaired.Types[0].Fields[i].ID&&type.Info.Fields[i].Default==repaired.Types[0].Fields[i].Default&&type.Info.Fields[i].DeclaringType==repaired.Types[0].Fields[i].DeclaringType,"Pure schema and compiled defaults differ");
        if(argc==2) {
            runtime=Export(argv[1]);Check(runtime,"Runtime load");std::string error;Check(runtime->RegisterScriptType(type,error),error);
            auto* scene=runtime->CreateScene("SerializedFields");auto* owner=scene->CreateGameObject("Owner");auto* referenced=scene->CreateGameObject("Target");
            Check(runtime->BindSceneObjectIdentity(scene,0,error)&&runtime->BindSceneObjectIdentity(owner,1,error)&&runtime->BindSceneObjectIdentity(referenced,81,error),error);
            auto* script=runtime->AddScriptComponent(owner,type.Info.TypeID,type.Source,0,error);Check(script,error);
            auto reference=script->GetFields().at("Child::_Target");reference.SceneObject={true,81};Check(script->SetField("Child::_Target",reference,error),error);
            auto speed=script->GetFields().at("Base::_Speed");speed.Number=5.5;Check(script->SetField("Base::_Speed",speed,error),error);
            Check(runtime->BeginExecution(error),error);Check(owner->GetTransform()->GetLocalPosition().Y==12.5f,"Protected field was not restored before Initialized");
            runtime->Update(.016f);Check(referenced->GetTransform()->GetLocalPosition().X==1,"Private object reference was not restored");
            Check(scene->DestroyGameObject(referenced),"Destroy referenced target");runtime->Update(.016f);Check(script->GetScriptError().empty(),"Deleted serialized reference stayed dangling");
            Check(script->GetFields().at("Child::_Target").SceneObject.ObjectFileID==81,"Delete lost authored identity needed by Undo");
            Check(runtime->EndExecution(error),error);delete runtime;runtime=nullptr;
        }
        Check(library.unload(),"Module unload");
        QByteArray unsupported=source;unsupported.replace("int _ChildSecret = 7;","int _ChildSecret = 7;\n    GAMESPROUT_SERIALIZE_FIELD\n    void* _Unsupported = nullptr;");Write(assets+"/Child.hpp",unsupported);
        ScriptCompileResult rejected;Check(!compiler.Compile(request,rejected,[]{return false;})&&rejected.Diagnostics.find("Child::_Unsupported")!=std::string::npos,"Unsupported explicitly serialized pointer lacked a diagnostic: "+rejected.Diagnostics);
        Write(assets+"/Child.hpp",R"(#pragma once
#include "RuntimeScriptModule.h"
namespace Other {
struct IGameObject {};
class Pretender final : public GameSproutRuntime::ScriptBehaviour {
protected:
    GAMESPROUT_SERIALIZE_FIELD
    IGameObject* _Target = nullptr;
};
}
GAMESPROUT_SCRIPT(Other::Pretender)
)");
        ScriptCompileResult shadowed;Check(!compiler.Compile(request,shadowed,[]{return false;})&&shadowed.Diagnostics.find("Pretender::_Target")!=std::string::npos,
            "An unrelated class named IGameObject was accepted as a Runtime object reference: "+shadowed.Diagnostics);
        std::cout<<"PASS private/protected/base fields, distinct inherited names, pure schema/cache repair, native BuildID, object reference lifecycle and unsupported-field diagnostics\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
