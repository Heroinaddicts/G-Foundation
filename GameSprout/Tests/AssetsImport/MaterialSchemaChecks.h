#pragma once
#include "../../Module/Shared/MaterialDefinition.h"
#include <limits>

inline void CheckMaterialSchemas() {
    std::string error;
    Json::Value schema=MaterialDefinition::PreviewSchema();
    Check(MaterialDefinition::ValidateSchema(schema,error),error);
    Json::Value source;
    source["Version"]=1; source["Name"]="Unchanged source"; source["CustomData"]["Retain"]=true;
    Check(MaterialDefinition::NormalizeValues(schema,source,error),error);
    Check(source["CustomData"]["Retain"]==true && source["BaseColor"].size()==4
        && source["BaseColorTexture"].isNull() && source["BaseColorTextureST"]==MaterialDefinition::Vector(1,1,0,0),
        "Material defaults discarded authoring fields or omitted texture transforms");
    Json::Value invalid=source;
    invalid["Roughness"]=2.0;
    const auto before=AssetArtifactIO::JsonText(invalid);
    Check(!MaterialDefinition::NormalizeValues(schema,invalid,error)
        && AssetArtifactIO::JsonText(invalid)==before,"Invalid material partially changed its source document");
    invalid=source; invalid["BaseColor"][0]=std::numeric_limits<double>::infinity();
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Nonfinite material colors were accepted");
    invalid=source; invalid["BaseColor"][0]=1e300;
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Unrepresentable GPU float material value was accepted");
    invalid=source; invalid["BaseColorTextureST"]=3;
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Invalid texture transform was accepted");
    invalid=source; invalid["Shader"]="Unknown/Builtin";
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Unknown builtin material Shader was accepted");
    for (const auto & shader : {Json::Value(),Json::Value(3),Json::Value(false),Json::Value(Json::arrayValue)}) {
        invalid=source; invalid["Shader"]=shader;
        Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Material Shader accepted an invalid JSON type");
    }
    invalid=source; invalid["Shader"]=Json::Value(Json::objectValue);
    invalid["Shader"]["Guid"]=Guid(1,2,3,4).GetGuidString(); invalid["Shader"]["LocalID"]=Json::UInt64(0);
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Material Shader accepted LocalID zero");

    const QByteArray declaration=R"material(/* GameSproutMaterial {
        "Name":"测试材质",
        "Properties":[
            {"Name":"Tint","DisplayName":"颜色","Type":"Color","Default":[1,0.5,0,1]},
            {"Name":"Amount","DisplayName":"强度","Type":"Range","Default":0.5,"Min":0,"Max":2},
            {"Name":"Offset","DisplayName":"偏移","Type":"Vector","Default":[0,0,0,0]},
            {"Name":"Map","DisplayName":"贴图","Type":"Texture","Default":null},
            {"Name":"Enabled","DisplayName":"启用","Type":"Toggle","Default":true},
            {"Name":"Mode","DisplayName":"模式","Type":"Enum","Default":1,"Options":["A","B"]},
            {"Name":"Count","DisplayName":"数量","Type":"Int","Default":2},
            {"Name":"Weight","DisplayName":"权重","Type":"Float","Default":-1.25}
        ]
    } */
    float4 main() : SV_Target0 { return 1; }
    )material";
    Check(MaterialDefinition::ParseShaderSchema(declaration,schema,error),error);
    Check(schema["Name"]=="测试材质" && schema["Properties"].size()==8,"HLSL material declaration was not parsed");
    source=Json::Value(Json::objectValue); source["Version"]=1;
    Check(MaterialDefinition::NormalizeValues(schema,source,error) && source["Mode"]==1 && source["Enabled"]==true
        && source["MapST"]==MaterialDefinition::Vector(1,1,0,0),"Custom material defaults were not applied");
    invalid=source; invalid["Mode"]=2;
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Out-of-range material Enum was accepted");
    invalid=source; invalid["Count"]=2.25;
    Check(!MaterialDefinition::ValidateValues(schema,invalid,error),"Fractional material Int was accepted");
    Json::Value broken=schema;
    broken["Properties"].append(schema["Properties"][0]);
    Check(!MaterialDefinition::ValidateSchema(broken,error),"Duplicate material property names were accepted");
    broken=schema; broken["Properties"][1]["Min"]=3;
    Check(!MaterialDefinition::ValidateSchema(broken,error),"Reversed material Range was accepted");
    broken=schema; broken["Properties"][2]["Name"]="MapST";
    Check(!MaterialDefinition::ValidateSchema(broken,error),"Explicit property collided with implicit texture transform");
    Json::Value parsed;
    Check(!MaterialDefinition::ParseShaderSchema(declaration+declaration,parsed,error),"Duplicate Shader material declarations were accepted");
    Check(!MaterialDefinition::ParseShaderSchema("/* GameSproutMaterial {} */",parsed,error),"Malformed Shader material schema was accepted");
    Check(MaterialDefinition::ParseShaderSchema("// /* GameSproutMaterial invalid */\n\"/* GameSproutMaterial invalid */\"",parsed,error)
        && parsed["Properties"].empty(),"A marker outside a block comment was parsed as material metadata");
    std::cout<<"PASS Shader material schema types, defaults, source preservation and validation\n";
}
