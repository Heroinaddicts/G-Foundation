#include "PhysicsMaterialDefinition.h"
#include <iostream>
#include <limits>
#include <stdexcept>
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
int main() {
    try {
        std::string error;GameSproutRuntime::PhysicsMaterialData material;
        Json::Value source(Json::objectValue);source["Version"]=1;
        Check(PhysicsMaterialDefinition::Read(source,material,error),error);
        Check(material.Friction==0.6f&&material.Restitution==0&&material.FrictionCombine==GameSproutRuntime::PhysicsMaterialCombine::Average,"Wrong default physical surface");
        source["Friction"]=2.5;source["Restitution"]=0.8;
        source["FrictionCombine"]="Multiply";source["RestitutionCombine"]="Maximum";
        Check(PhysicsMaterialDefinition::Read(source,material,error),error);
        std::vector<std::uint8_t> bytes;
        Check(PhysicsMaterialDefinition::Encode(material,bytes,error)&&bytes.size()==16,error);
        GameSproutRuntime::PhysicsMaterialData restored;
        Check(PhysicsMaterialDefinition::Decode(bytes.data(),bytes.size(),restored,error),error);
        Check(restored.Friction==2.5f&&restored.Restitution==material.Restitution&&restored.FrictionCombine==material.FrictionCombine&&restored.RestitutionCombine==material.RestitutionCombine,"Material binary changed values");
        Check(PhysicsMaterialDefinition::Write(restored)==PhysicsMaterialDefinition::Write(material),"Material source roundtrip changed values");
        for(const char* field:{"StaticFriction","DynamicFriction","Unknown"}) {
            Json::Value bad=source;bad[field]=1;
            Check(!PhysicsMaterialDefinition::Read(bad,restored,error),"Unknown or unimplemented field was accepted");
        }
        for(double value:{-1.0,1.01}) {
            Json::Value bad=source;bad["Restitution"]=value;Check(!PhysicsMaterialDefinition::Read(bad,restored,error),"Invalid restitution was accepted");
        }
        Json::Value bad=source;bad["Friction"]=-0.01;Check(!PhysicsMaterialDefinition::Read(bad,restored,error),"Negative friction accepted");
        bad=source;bad["FrictionCombine"]="Other";Check(!PhysicsMaterialDefinition::Read(bad,restored,error),"Unknown combine accepted");
        bad=source;bad["Friction"]=true;Check(!PhysicsMaterialDefinition::Read(bad,restored,error),"Boolean friction accepted");
        auto damaged=bytes;damaged[8]=0;damaged[9]=1;Check(!PhysicsMaterialDefinition::Decode(damaged.data(),damaged.size(),restored,error),"Wide enum truncated into valid uint8 mode");
        Check(!PhysicsMaterialDefinition::Decode(bytes.data(),bytes.size()-1,restored,error),"Truncated binary accepted");
        material.Friction=std::numeric_limits<float>::infinity();Check(!PhysicsMaterialDefinition::Valid(material,error),"Infinite friction accepted");
        std::cout<<"PASS physical material schema, fixed binary payload and invalid value rejection\n";return 0;
    } catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<"\n";return 1;}
}
