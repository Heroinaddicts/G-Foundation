#include "ScriptCompiler.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
void Write(const QString& path,const QByteArray& bytes) {
    QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write fixture source");
    Check(file.write(bytes)==bytes.size(),"Incomplete fixture source");
}
QByteArray Hash(const std::string& path) {
    QFile file(QString::fromStdString(path));Check(file.open(QIODevice::ReadOnly),"Missing successful candidate");
    return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
}
QByteArray Source(const QByteArray& base,const QByteArray& body,const QByteArray& inherited="Base") {
    return "#pragma once\n#include \"RuntimeScriptModule.h\"\n"+base+
        "\nclass Demo final : public "+inherited+" {\n"+body+"\n};\nGAMESPROUT_SCRIPT(Demo)\n";
}
struct Rejection {
    const char* Name;
    QByteArray Base;
    QByteArray Body;
    const char* Member;
    const char* BaseName;
    QByteArray Inherited="Base";
};
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create test project");
        const QString assets=temporary.path()+"/Assets";Check(QDir().mkpath(assets),"Cannot create Assets");
        const QString sourcePath=assets+"/Demo.hpp";
        ScriptCompiler compiler;
        ScriptCompileRequest request;
        request.AssetsRoot=assets.toStdString();request.BuildRoot=(temporary.path()+"/Library/ScriptBuild").toStdString();
        request.Sources={{"Demo.hpp","00112233445566778899aabbccddeeff"}};
        const QByteArray ordinaryBase=R"(
class Base : public GameSproutRuntime::ScriptBehaviour {
private:
    GAMESPROUT_SERIALIZE_FIELD
    int _BaseValue=4;
protected:
    float _BaseSpeed=1;
};
)";
        const QByteArray valid=Source(ordinaryBase,R"(
private:
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IGameObject* _Target=nullptr;
    GAMESPROUT_SERIALIZE_FIELD
    int _OwnValue=5;
public:
    // Exact virtual implementations do not require an explicit override token.
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) {}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {}
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) {}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {}
    using GameSproutRuntime::IComponent::GetGameObject;
    enum class Flags { _GameObject }; // Scoped enumerators do not hide an owner member.
    struct HelperData { int _GameObject=0; }; // Unrelated nested records have their own scope.
    int Helper(int value) const {return value;}
    int Helper(float value) const {return int(value);}
)");
        Write(sourcePath,valid);
        ScriptCompileResult baseline;Check(compiler.Compile(request,baseline,[]{return false;}),"Valid declaration failed: "+baseline.Diagnostics);
        Check(baseline.Types.size()==1&&baseline.Types[0].Fields.size()==3,"Valid inheritance lost declared fields");
        const auto goodLibrary=Hash(baseline.LibraryPath);
        const auto goodSchema=Hash(baseline.SchemaPath);

        const QByteArray emptyBase="class Base : public GameSproutRuntime::ScriptBehaviour {};\n";
        const std::vector<Rejection> rejected={
            {"marked private owner field",emptyBase,"private:\n GAMESPROUT_SERIALIZE_FIELD\n GameSproutRuntime::IGameObject* _GameObject=nullptr;","_GameObject","IComponent"},
            {"unmarked private owner field",emptyBase,"private:\n GameSproutRuntime::IGameObject* _GameObject=nullptr;","_GameObject","IComponent"},
            {"public owner field",emptyBase,"public:\n int _GameObject=0;","_GameObject","IComponent"},
            {"protected owner field",emptyBase,"protected:\n GameSproutRuntime::IGameObject* _GameObject=nullptr;","_GameObject","IComponent"},
            {"static owner field",emptyBase,"private:\n inline static GameSproutRuntime::IGameObject* _GameObject=nullptr;","_GameObject","IComponent"},
            {"alias hides owner",emptyBase,"private:\n using _GameObject=int;","_GameObject","IComponent"},
            {"enumerator hides owner",emptyBase,"private:\n enum Flags { _GameObject=1 };","_GameObject","IComponent"},
            {"nested type hides owner",emptyBase,"private:\n struct _GameObject {};","_GameObject","IComponent"},
            {"intermediate owner field","class Base : public GameSproutRuntime::ScriptBehaviour {};\nclass Middle : public Base { private: GameSproutRuntime::IGameObject* _GameObject=nullptr; };\n","public:\n int Value=1;","_GameObject","IComponent","Middle"},
            {"user public base field","class Base : public GameSproutRuntime::ScriptBehaviour { public: int Value=1; };\n","public:\n int Value=2;","Value","Base"},
            {"user protected base field","class Base : public GameSproutRuntime::ScriptBehaviour { protected: int Value=1; };\n","private:\n int Value=2;","Value","Base"},
            {"user private base field","class Base : public GameSproutRuntime::ScriptBehaviour { private: int Value=1; };\n","private:\n GAMESPROUT_SERIALIZE_FIELD\n int Value=2;","Value","Base"},
            {"owner accessor hiding",emptyBase,"public:\n GameSproutRuntime::IGameObject* GetGameObject() const {return nullptr;}","GetGameObject","IComponent"},
            {"data hides lifecycle",emptyBase,"private:\n int OnCreate=0;","OnCreate","IComponent"},
            {"wrong Initialized arity",emptyBase,"public:\n void OnInitialized() {}","OnInitialized","IComponent"},
            {"wrong Update delta type",emptyBase,"public:\n void Update(GameSproutRuntime::IGameSproutRuntime*,double) {}","Update","IComponent"},
            {"wrong EarlyUpdate arity",emptyBase,"protected:\n void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime*) {}","EarlyUpdate","IComponent"},
            {"wrong LateUpdate delta type",emptyBase,"protected:\n void LateUpdate(GameSproutRuntime::IGameSproutRuntime*,double) {}","LateUpdate","IComponent"},
            {"wrong lifecycle const qualifier",emptyBase,"public:\n void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) const {}","FixedUpdate","IComponent"},
            {"wrong Release argument",emptyBase,"public:\n void OnRelease(int) {}","OnRelease","IComponent"},
            {"using does not excuse wrong lifecycle",emptyBase,"public:\n using Base::Update;\n void Update(GameSproutRuntime::IGameSproutRuntime*) {}","Update","IComponent"},
            {"static lifecycle overload",emptyBase,"public:\n static void OnInitialized(GameSproutRuntime::IGameSproutRuntime*,int) {}","OnInitialized","IComponent"}
        };
        for(const auto& fixture:rejected) {
            Write(sourcePath,Source(fixture.Base,fixture.Body,fixture.Inherited));
            ScriptCompileResult result;
            const bool accepted=compiler.Compile(request,result,[]{return false;});
            Check(!accepted,std::string("Unsafe script declaration compiled: ")+fixture.Name);
            Check(result.Diagnostics.find("Demo.hpp:")!=std::string::npos&&
                  result.Diagnostics.find(fixture.Member)!=std::string::npos,
                  std::string("Diagnostic lacks source/member for ")+fixture.Name+": "+result.Diagnostics);
            Check(result.Diagnostics.find(fixture.BaseName)!=std::string::npos,
                  std::string("Diagnostic lacks inherited declaration for ")+fixture.Name+": "+result.Diagnostics);
            Check(Hash(baseline.LibraryPath)==goodLibrary&&Hash(baseline.SchemaPath)==goodSchema,
                  std::string("Rejected declaration overwrote last successful candidate: ")+fixture.Name);
        }
        Write(sourcePath,valid);
        ScriptCompileResult restored;
        Check(compiler.Compile(request,restored,[]{return false;}),"Valid source did not recover after diagnostics: "+restored.Diagnostics);
        Check(restored.Fingerprint==baseline.Fingerprint&&restored.Types.size()==1&&restored.Types[0].Fields.size()==3,
              "Rejected source changed the valid declaration's identity/schema");
        std::cout<<"PASS inherited member collisions, wrong lifecycle signatures, correct overrides, and failed-build preservation\n";
        return 0;
    } catch(const std::exception& failure) {std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
