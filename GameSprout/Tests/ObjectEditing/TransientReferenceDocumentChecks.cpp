#include "SceneScriptDocument.h"
#include <iostream>
#include <stdexcept>
class Script final:public GameSproutRuntime::IScriptComponent {
public:
    Script():IScriptComponent(nullptr){}
    std::map<std::string,GameSproutRuntime::ScriptValue> Fields;
    std::string Type="1234567890abcdef1234567890abcdef",Error;
    const std::type_info& GetComponentType()const override{return typeid(GameSproutRuntime::IScriptComponent);}
    const char* GetTypeName()const override{return "GameSproutRuntime.ScriptComponent";}
    std::uint64_t GetScriptInstanceID()const override{return 1;}
    const std::string& GetScriptTypeID()const override{return Type;}
    GameSproutRuntime::ResourceReference GetScriptSource()const override{return {Type,1};}
    bool IsScriptTypeAvailable()const override{return false;}
    bool IsEnabled()const override{return true;}void SetEnabled(bool)override{}
    const std::map<std::string,GameSproutRuntime::ScriptValue>& GetFields()const override{return Fields;}
    bool SetField(const std::string&,const GameSproutRuntime::ScriptValue&,std::string&)override{return false;}
    const std::string& GetScriptError()const override{return Error;}
};
int main() {
    try {
        Script script;GameSproutRuntime::ScriptValue value;value.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
        value.SceneObject={true,23};script.Fields["Target"]=value;
        const auto source=SceneScriptDocument::Serialize(script);
        if(source["Fields"]["Target"]["Value"]["ObjectFileID"].asUInt64()!=23)throw std::runtime_error("Persistent identity lost");
        for(const auto reference:{GameSproutRuntime::ScriptObjectReference{true,0,5,6},
            GameSproutRuntime::ScriptObjectReference{true,23,5,6},GameSproutRuntime::ScriptObjectReference{false,0,5,0}}) {
            script.Fields["Target"].SceneObject=reference;
            bool rejected=false;try{SceneScriptDocument::Serialize(script);}catch(const std::exception& error){rejected=std::string(error.what()).find("runtime object references")!=std::string::npos;}
            if(!rejected)throw std::runtime_error("Runtime identity reached persistent source data");
        }
        std::cout<<"PASS only persistent FileID references serialize; transient IDs reject\n";return 0;
    } catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
