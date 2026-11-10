#include "ScriptCompiler.h"
#include "ScriptSchemaDocument.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
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
const GameSproutRuntime::ScriptFieldInfo& Field(const std::vector<GameSproutRuntime::ScriptFieldInfo>& fields,const std::string& name) {
    for(const auto& field:fields)if(field.Name==name)return field;
    throw std::runtime_error("Missing recursive field: "+name);
}
bool Equal(const GameSproutRuntime::ScriptFieldInfo& a,const GameSproutRuntime::ScriptFieldInfo& b) {
    if(a.ID!=b.ID||a.Name!=b.Name||a.DeclaringType!=b.DeclaringType||a.Default!=b.Default||a.TypeName!=b.TypeName||a.ComponentType!=b.ComponentType||a.FixedLength!=b.FixedLength||a.Children.size()!=b.Children.size()||a.EnumValues.size()!=b.EnumValues.size())return false;
    for(std::size_t i=0;i<a.Children.size();++i)if(!Equal(a.Children[i],b.Children[i]))return false;
    for(std::size_t i=0;i<a.EnumValues.size();++i)if(a.EnumValues[i].Name!=b.EnumValues[i].Name||a.EnumValues[i].Value!=b.EnumValues[i].Value||a.EnumValues[i].IsUnsigned!=b.EnumValues[i].IsUnsigned||a.EnumValues[i].UnsignedValue!=b.EnumValues[i].UnsignedValue)return false;
    return true;
}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);QLibrary library;GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Temporary project");const QString assets=temporary.path()+"/Assets";Check(QDir().mkpath(assets),"Assets directory");
        const QByteArray source=R"(#pragma once
#include "RuntimeScriptModule.h"
#include <array>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>
namespace Demo {
class Peer;
enum class Mode : long long { Low=-7, Normal=2, High=9 };
enum class Wide : unsigned long long { None=0, Maximum=18446744073709551615ULL };
enum class Nonzero : int { First=1, Second=2 };
struct Payload {
    int Count=7;
    GameSproutRuntime::IGameObject* Target=nullptr;
    GameSproutRuntime::IRigidbodyComponent* Body=nullptr;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    std::string Caption="nested";
};
class Settings {
public:
    unsigned int Flags=4294967295U;
private:
    GAMESPROUT_SERIALIZE_FIELD
    double Gain=2.5;
};
struct Data {
    Payload Details;
    std::array<Settings,2> Options{};
    int Grid[2][3]={{1,2,3},{4,5,6}};
    std::list<GameSproutRuntime::Vector2> Points={{1,2},{3,4}};
    std::map<std::string,std::vector<Payload>> Groups;
    std::unordered_map<unsigned long long,int> Lookup={{1,2},{18446744073709551615ULL,3}};
};
class Example final : public GameSproutRuntime::ScriptBehaviour {
private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<Example>;
    explicit Example(GameSproutRuntime::IGameSproutRuntime*) {}

    GAMESPROUT_SERIALIZE_FIELD
    Data Config;
    GAMESPROUT_SERIALIZE_FIELD
    std::vector<std::map<std::string,std::array<Payload,2>>> Nested;
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IRigidbodyComponent* Body=nullptr;
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::ITextComponent* Label=nullptr;
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IButtonComponent* Button=nullptr;
    GAMESPROUT_SERIALIZE_FIELD
    unsigned long long Maximum=18446744073709551615ULL;
    GAMESPROUT_SERIALIZE_FIELD
    Mode Selected=Mode::Low;
    GAMESPROUT_SERIALIZE_FIELD
    Wide WideSelected=Wide::Maximum;
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::Vector4 Direction={1,2,3,4};
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::Quaternion Rotation;
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::Color Tint={.25f,.5f,.75f};
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::Mat4 Matrix;
    GAMESPROUT_SERIALIZE_FIELD
    Peer* Companion=nullptr;
    GAMESPROUT_SERIALIZE_FIELD
    std::vector<GameSproutRuntime::IRigidbodyComponent*> Bodies;
    GAMESPROUT_SERIALIZE_FIELD
    std::map<std::string,Peer*> Peers;
    GAMESPROUT_SERIALIZE_FIELD
    Peer* PeerArray[2]{};
    GAMESPROUT_SERIALIZE_FIELD
    std::vector<Nonzero> EmptyEnums;
    GAMESPROUT_SERIALIZE_FIELD
    std::map<Nonzero,int> EmptyEnumMap;
protected:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        if(!Companion||!Body||!Config.Details.Target||Config.Details.Body!=Body||Nested.size()!=1||Nested[0].at("entry")[1].Body!=Body||Bodies.size()!=1||Bodies[0]!=Body||Peers.at("entry")!=Companion||PeerArray[1]!=Companion)
            throw std::runtime_error("Recursive object/component/script references were not fixed up before OnCreate");
        if(!Label||!Button||!Label->SetText("Native UI"))
            throw std::runtime_error("Native UI component references were not fixed up before OnCreate");
        Button->SetInteractable(false);
        _GameObject->SetName("ReferencesResolved");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        const auto token=Button->RegisterClick([label=Label] {label->SetText("Native click");});
        if(!token||!Label->SetText("Native UI "+std::to_string(token)))
            throw std::runtime_error("Could not register native UI click callback");
    }
};
}
GAMESPROUT_SCRIPT(Demo::Example)
)";
        Write(assets+"/Example.h",source);
        Write(assets+"/Example.cpp","#include \"Example.h\"\n");
        Write(assets+"/Peer.h",R"(#pragma once
#include "RuntimeScriptModule.h"
namespace Demo {
class Peer final : public GameSproutRuntime::ScriptBehaviour {
    friend struct GameSproutRuntime::Internal::GameplayFactory<Peer>;
    explicit Peer(GameSproutRuntime::IGameSproutRuntime*) {}
};
}
GAMESPROUT_SCRIPT(Demo::Peer)
)");
        Write(assets+"/Peer.cpp","#include \"Peer.h\"\n");
        ScriptCompiler compiler;ScriptCompileRequest request;
        request.AssetsRoot=assets.toStdString();request.BuildRoot=(temporary.path()+"/Library/ScriptBuild").toStdString();request.Sources={
            {"Example.h","00112233445566778899aabbccddeeff"},{"Example.cpp","10112233445566778899aabbccddeeff"},
            {"Peer.h","ffeeddccbbaa99887766554433221100"},{"Peer.cpp","efeeddccbbaa99887766554433221100"}};
        ScriptCompileResult compiled;Check(compiler.Compile(request,compiled,[]{return false;}),compiled.Diagnostics);
        Check(compiled.Types.size()==2&&compiled.Types.front().Fields.size()==18,"Recursive script schema incomplete: "+compiled.ReflectionWarnings);
        const auto& fields=compiled.Types.front().Fields;using GameSproutRuntime::ScriptValueKind;
        const auto& config=Field(fields,"Config");Check(config.Default.Kind==ScriptValueKind::Structure&&config.Children.size()==6,"Record field schema");
        const auto& details=Field(config.Children,"Details");Check(details.Children.size()==4&&Field(details.Children,"Caption").Default.String=="nested","Protected nested record field");
        const auto& options=Field(config.Children,"Options");Check(options.FixedLength==2&&options.Default.Elements.size()==2&&Field(options.Children[0].Children,"Gain").Default.Number==2.5,"Private class fields in fixed arrays");
        const auto& grid=Field(config.Children,"Grid");Check(grid.FixedLength==2&&grid.Children[0].FixedLength==3&&grid.Default.Elements[1].Elements[2].Integer==6,"Multidimensional C array defaults");
        const auto& points=Field(config.Children,"Points");Check(points.FixedLength==-1&&points.Default.Elements.size()==2&&points.Default.Elements[1].Vector2.Y==4,"List math defaults");
        const auto& lookup=Field(config.Children,"Lookup");Check(lookup.Default.Entries.size()==2&&lookup.Default.Entries[1].first.Unsigned==std::numeric_limits<std::uint64_t>::max(),"Map key integer precision");
        Check(Field(fields,"Maximum").Default.Unsigned==std::numeric_limits<std::uint64_t>::max(),"uint64 default precision");
        Check(Field(fields,"Selected").EnumValues.size()==3&&Field(fields,"Selected").Default.Integer==-7,"Signed enum metadata/default");
        Check(Field(fields,"WideSelected").EnumValues[1].UnsignedValue==std::numeric_limits<std::uint64_t>::max()&&Field(fields,"WideSelected").Default.Unsigned==std::numeric_limits<std::uint64_t>::max(),"Unsigned enum metadata/default");
        Check(Field(fields,"Body").Default.Kind==ScriptValueKind::Component&&Field(fields,"Body").ComponentType=="GameSproutRuntime.Rigidbody","Component pointer identity");
        Check(Field(fields,"Label").ComponentType=="GameSproutRuntime.Text"&&Field(fields,"Button").ComponentType=="GameSproutRuntime.Button","UI component pointer identity");
        Check(Field(fields,"Companion").ComponentType=="ffeeddccbbaa99887766554433221100","User script pointer must use its source TypeID");
        Check(Field(fields,"EmptyEnums").Default.Elements.empty()&&Field(fields,"EmptyEnums").Children[0].Default.Integer==1&&Field(fields,"EmptyEnumMap").Children[0].Default.Integer==1,"Empty containers need a declared enum value for editor-created elements");
        Check(Field(fields,"Rotation").Default.Rotation.W==1&&Field(fields,"Tint").Default.Tint.A==1,"SDK math defaults");
        Check(Field(fields,"Matrix").Default.Fields.at("GameSproutRuntime::Mat4::Data").Elements[15].Number==1,"Matrix default identity");
        ScriptCompileResult cached;Check(compiler.Compile(request,cached,[]{return false;})&&cached.CacheHit,"Recursive schema cache");
        for(std::size_t i=0;i<fields.size();++i)Check(Equal(fields[i],cached.Types.front().Fields[i]),"Cached recursive field metadata mismatch");
        library.setFileName(QString::fromStdString(compiled.LibraryPath));Check(library.load(),library.errorString().toStdString());
        const auto entry=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(library.resolve(GameSproutRuntime::ScriptModuleEntryName));Check(entry,"Native module entry");const auto* module=entry();
        Check(module&&module->Version==GameSproutRuntime::ScriptModuleABIVersion&&module->Count==2,"Native recursive module ABI");
        for(std::size_t i=0;i<fields.size();++i)Check(Equal(fields[i],module->Types[0].Info.Fields[i]),"Offline and native recursive field metadata mismatch");
        GameSproutRuntime::ScriptReferenceContext context;std::string error;
        for(const auto& field:module->Types[0].Fields)Check(field.Validate&&field.Validate(field.Info.Default,context,error),"Native recursive validation: "+error);
        if(argc==2) {
            runtime=Export(argv[1]);Check(runtime,"Load Runtime for recursive identity restoration");
            for(std::size_t i=0;i<module->Count;++i)Check(runtime->RegisterScriptType(module->Types[i],error),error);
            auto* scene=runtime->CreateScene("RecursiveReferences");
            auto* canvas=scene->CreateGameObject("Canvas");
            Check(canvas&&canvas->AddComponent<GameSproutRuntime::ICanvasComponent>()&&
                canvas->AddComponent<GameSproutRuntime::IRectTransformComponent>(),"Create Canvas for native click callback");
            auto* owner=scene->CreateGameObject("Owner",canvas);auto* peerOwner=scene->CreateGameObject("Peer");
            Check(owner&&owner->AddComponent<GameSproutRuntime::IRectTransformComponent>(),"Create clickable native UI layout");
            auto* body=owner->AddComponent<GameSproutRuntime::IRigidbodyComponent>();Check(body,"Create referenced Rigidbody");
            auto* label=owner->AddComponent<GameSproutRuntime::ITextComponent>();
            auto* button=owner->AddComponent<GameSproutRuntime::IButtonComponent>();
            Check(label&&button,"Create referenced UI components");
            auto* script=runtime->AddScriptComponent(owner,module->Types[0].Info.TypeID,module->Types[0].Source,0,error);
            auto* peer=runtime->AddScriptComponent(peerOwner,module->Types[1].Info.TypeID,module->Types[1].Source,0,error);Check(script&&peer,error);
            Check(runtime->BindSceneObjectIdentity(scene,0,error)&&runtime->BindSceneObjectIdentity(owner,11,error)&&runtime->BindSceneObjectIdentity(peerOwner,22,error),error);
            Check(runtime->SetSceneComponentIdentities(scene,{{body,101},{peer,102},{script,103},{label,104},{button,105}},error),error);
            auto config=script->GetFields().at("Demo::Example::Config");auto& data=config.Fields.at("Demo::Data::Details");
            data.Fields.at("Demo::Payload::Target").SceneObject={true,22};
            data.Fields.at("Demo::Payload::Body").Component.Assigned=true;data.Fields.at("Demo::Payload::Body").Component.ComponentFileID=101;
            Check(script->SetField("Demo::Example::Config",config,error),error);
            auto bodyValue=script->GetFields().at("Demo::Example::Body");bodyValue.Component={true,101};Check(script->SetField("Demo::Example::Body",bodyValue,error),error);
            auto peerValue=script->GetFields().at("Demo::Example::Companion");peerValue.Component={true,102};Check(script->SetField("Demo::Example::Companion",peerValue,error),error);
            auto labelValue=script->GetFields().at("Demo::Example::Label");labelValue.Component={true,104};Check(script->SetField("Demo::Example::Label",labelValue,error),error);
            auto buttonValue=script->GetFields().at("Demo::Example::Button");buttonValue.Component={true,105};Check(script->SetField("Demo::Example::Button",buttonValue,error),error);
            auto nested=script->GetFields().at("Demo::Example::Nested");
            auto dictionary=Field(fields,"Nested").Children[0].Default;
            auto pairValue=Field(fields,"Nested").Children[0].Children[1].Default;
            pairValue.Elements[0]=data;pairValue.Elements[1]=data;
            GameSproutRuntime::ScriptValue key;key.Kind=ScriptValueKind::String;key.String="entry";
            dictionary.Entries.emplace_back(key,pairValue);nested.Elements.push_back(dictionary);
            Check(script->SetField("Demo::Example::Nested",nested,error),error);
            auto bodies=script->GetFields().at("Demo::Example::Bodies");bodies.Elements.push_back(bodyValue);
            Check(script->SetField("Demo::Example::Bodies",bodies,error),error);
            auto peers=script->GetFields().at("Demo::Example::Peers");peers.Entries.emplace_back(key,peerValue);
            Check(script->SetField("Demo::Example::Peers",peers,error),error);
            auto peerArray=script->GetFields().at("Demo::Example::PeerArray");peerArray.Elements[0]=peerValue;peerArray.Elements[1]=peerValue;
            Check(script->SetField("Demo::Example::PeerArray",peerArray,error),error);
            Check(runtime->BeginExecution(error),error);
            Check(script->GetScriptError().empty()&&owner->GetName()=="ReferencesResolved","Cross-script and nested references must resolve before lifecycle callbacks: "+script->GetScriptError());
            Check(label->GetText().starts_with("Native UI ")&&!button->GetInteractable(),"Native game code could not update UI through the public API");
            const auto clickToken=std::stoull(label->GetText().substr(10));
            Check(clickToken!=0,"Native button subscription returned an invalid token");
            button->SetInteractable(true);
            runtime->SetUIInputViewport(scene,{400,300});
            using GameSproutRuntime::InputEventType;
            using GameSproutRuntime::RuntimeInputEvent;
            RuntimeInputEvent move{InputEventType::MouseMove};move.X=200;move.Y=150;
            Check(runtime->SubmitInputEvent({InputEventType::FocusGained})&&runtime->SubmitInputEvent(move)&&
                runtime->SubmitInputEvent({InputEventType::MouseButtonDown})&&runtime->SubmitInputEvent({InputEventType::MouseButtonUp}),
                "Native UI button input was rejected");
            runtime->Update(.016f);
            Check(label->GetText()=="Native click","Native game code did not receive the UI click callback");
            std::map<std::string,GameSproutRuntime::ScriptValue> live;
            Check(script->ReadFields(live,error),error);
            Check(live.at("Demo::Example::Companion").Component.ComponentFileID==102&&live.at("Demo::Example::Nested").Elements[0].Entries[0].second.Elements[1].Fields.at("Demo::Payload::Body").Component.ComponentFileID==101,"Live nested component references must retain document identities");
            Check(runtime->EndExecution(error),error);
            Check(!button->UnregisterClick(clickToken),"Runtime retained a callback after releasing the native script instance");
            delete runtime;runtime=nullptr;
        }
        Check(library.unload(),"Unload recursive native module");
        const QByteArray invalid=R"(#pragma once
#include "RuntimeScriptModule.h"
struct Unsupported { Unsupported(){ } int Value=1; };
class Invalid final : public GameSproutRuntime::ScriptBehaviour {
    friend struct GameSproutRuntime::Internal::GameplayFactory<Invalid>;
    explicit Invalid(GameSproutRuntime::IGameSproutRuntime*) {}

    GAMESPROUT_SERIALIZE_FIELD
    Unsupported Value;
};
GAMESPROUT_SCRIPT(Invalid)
)";
        Write(assets+"/Example.h",invalid);ScriptCompileResult rejected;
        Check(!compiler.Compile(request,rejected,[]{return false;})&&rejected.Diagnostics.find("Invalid::Value")!=std::string::npos,"Explicitly marked opaque constructor was not rejected");
        Write(assets+"/Example.h",R"(#pragma once
#include "RuntimeScriptModule.h"
class Invalid final : public GameSproutRuntime::ScriptBehaviour {
    friend struct GameSproutRuntime::Internal::GameplayFactory<Invalid>;
    explicit Invalid(GameSproutRuntime::IGameSproutRuntime*) {}

    GameSproutRuntime::IRigidbodyComponent _Rigidbody;
};
GAMESPROUT_SCRIPT(Invalid)
)");
        ScriptCompileResult abstractValue;
        Check(!compiler.Compile(request,abstractValue,[]{return false;})&&abstractValue.Diagnostics.find("abstract")!=std::string::npos,
              "An abstract component declared by value must remain a C++ compilation error");
        Write(assets+"/Example.h",R"(#pragma once
#include "RuntimeScriptModule.h"
class Invalid final : public GameSproutRuntime::ScriptBehaviour {
    friend struct GameSproutRuntime::Internal::GameplayFactory<Invalid>;
    explicit Invalid(GameSproutRuntime::IGameSproutRuntime*) {}

    GAMESPROUT_SERIALIZE_FIELD
    void* Unsupported=nullptr;
};
GAMESPROUT_SCRIPT(Invalid)
)");
        ScriptCompileResult unsupported;
        Check(!compiler.Compile(request,unsupported,[]{return false;})&&unsupported.Diagnostics.find("Invalid::Unsupported")!=std::string::npos,
              "An explicitly marked unsupported pointer must fail compilation");
        Write(assets+"/Example.h",R"(#pragma once
#include "RuntimeScriptModule.h"
enum class Nonzero { First=1, Second=2 };
class Invalid final : public GameSproutRuntime::ScriptBehaviour {
    friend struct GameSproutRuntime::Internal::GameplayFactory<Invalid>;
    explicit Invalid(GameSproutRuntime::IGameSproutRuntime*) {}

    GAMESPROUT_SERIALIZE_FIELD
    Nonzero Value;
};
GAMESPROUT_SCRIPT(Invalid)
)");
        ScriptCompileResult invalidEnum;
        Check(!compiler.Compile(request,invalidEnum,[]{return false;})&&invalidEnum.Diagnostics.find("Enum default is not one of its declared values")!=std::string::npos,
              "An enum with an undeclared zero default must fail offline reflection");
        std::cout<<"PASS recursive compiler schemas, unsigned/enums, component identities, arrays/containers, math and generated validation\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
