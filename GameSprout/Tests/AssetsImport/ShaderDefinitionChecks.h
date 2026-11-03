#pragma once
#include "../../Module/Shared/ShaderDefinition.h"

inline void CheckShaderDefinitions(IShaderCompiler& compiler,ShaderCompileRequest request) {
    std::string error;Json::Value declaration,schema,layout,description;
    const QByteArray author=R"shader(/* GameSproutShader {"Name":"Tests/Contract","Passes":[{"Name":"Forward","Vertex":"VSMain","Fragment":"PSMain"}]} */
        /* GameSproutMaterial {"Name":"Contract","Properties":[
            {"Name":"Tint","DisplayName":"颜色","Type":"Color","Default":[1,1,1,1]},
            {"Name":"Gain","DisplayName":"强度","Type":"Float","Default":0.5},
            {"Name":"Map","DisplayName":"贴图","Type":"Texture","Default":null}
        ]} */)shader";
    Check(ShaderDefinition::Parse(author,declaration,error)&&declaration["Passes"][0]["Cull"]=="Back","Cannot normalize Shader Pass declaration: "+error);
    Check(!ShaderDefinition::Parse("// GameSproutShader {}",declaration,error),"Line comment was accepted as the Shader declaration");
    Check(!ShaderDefinition::Parse(author+author,declaration,error),"Duplicate Shader declarations accepted");
    QByteArray invalid=author;invalid.replace("\"PSMain\"","\"PSMain\",\"Blend\":\"Unsupported\"");
    Check(!ShaderDefinition::Parse(invalid,declaration,error),"Unsupported Shader render state accepted");
    Check(MaterialDefinition::ParseShaderSchema(author,schema,error),error);
    request.SourcePath="Layout.shader";request.Targets={{eShaderCodeFormat::SPIRV,eShaderPlatform::Vulkan}};
    request.Stage=eShaderStage::Fragment;request.EntryPoint="PSMain";
    request.Source="Texture2D<float4> Map:register(t0,space2);SamplerState MapSampler:register(s0,space2);"
        "cbuffer Material:register(b0,space3){float4 Tint;float4 MapST;float Gain;}"
        "float4 PSMain(float2 uv:TEXCOORD0):SV_Target0{return Map.Sample(MapSampler,uv*MapST.xy+MapST.zw)*Tint*Gain;}";
    ShaderCompileResult result;
    Check(compiler.Compile(request,result),"Material layout compile failed: "+result.Error);
    Check(AssetArtifactIO::Parse(result.ReflectionJson,description)&&ShaderDefinition::ProgramLayout(description["Reflection"],schema,false,layout,error),error);
    Check(layout["Uniforms"][0]["Size"].asUInt()==48&&layout["Uniforms"][0]["Members"][2]["Offset"].asUInt()==32
        &&layout["Textures"][0]["Name"]=="Map","Reflected uniform padding/member offsets or texture name lost");
    auto undeclared=schema;undeclared["Properties"][1]["Name"]="Other";
    Check(!ShaderDefinition::ProgramLayout(description["Reflection"],undeclared,false,layout,error),"Undeclared GPU material member accepted");
    const std::string frameBlock="cbuffer GameSproutFrame:register(b1,space3){float4 Time;float4 CameraPosition;float4 MainLightDirection;float4 MainLightColor;}";
    request.Source="cbuffer Material:register(b0,space3){float4 Tint;}"+frameBlock+
        "float4 PSMain():SV_Target0{return Tint+Time+CameraPosition+MainLightDirection+MainLightColor;}";
    Check(compiler.Compile(request,result)&&AssetArtifactIO::Parse(result.ReflectionJson,description),"Frame layout compile failed: "+result.Error);
    Check(ShaderDefinition::ProgramLayout(description["Reflection"],schema,false,layout,error)&&layout["Uniforms"].size()==2
        &&layout["Uniforms"][0]["Source"]=="Material"&&layout["Uniforms"][1]["Source"]=="Frame"
        &&layout["Uniforms"][1]["Size"].asUInt()==64&&layout["Uniforms"][1]["Members"].empty(),"Frame block changed fragment b0 material semantics: "+error);
    const auto reflectedFrame=description["Reflection"];
    Json::ArrayIndex frameIndex=0;
    for(Json::ArrayIndex i=0;i<reflectedFrame["ubos"].size();++i)if(reflectedFrame["ubos"][i]["binding"].asUInt()==1)frameIndex=i;
    const auto frameType=reflectedFrame["ubos"][frameIndex]["type"].asString();
    auto invalidFrame=reflectedFrame;invalidFrame["types"][frameType]["members"][1]["offset"]=32;
    Check(!ShaderDefinition::ProgramLayout(invalidFrame,schema,false,layout,error),"Invalid Frame offset accepted");
    invalidFrame=reflectedFrame;invalidFrame["types"][frameType]["members"][0]["type"]="vec3";
    Check(!ShaderDefinition::ProgramLayout(invalidFrame,schema,false,layout,error),"Invalid Frame member type accepted");
    invalidFrame=reflectedFrame;invalidFrame["ubos"][frameIndex]["block_size"]=48;
    Check(!ShaderDefinition::ProgramLayout(invalidFrame,schema,false,layout,error),"Truncated Frame block accepted");
    invalidFrame=reflectedFrame;invalidFrame["ubos"][frameIndex]["name"]="UserFrame";
    Check(!ShaderDefinition::ProgramLayout(invalidFrame,schema,false,layout,error),"Arbitrary block name was treated as engine Frame");
    invalidFrame=reflectedFrame;auto duplicate=invalidFrame["ubos"][frameIndex];duplicate["binding"]=2;invalidFrame["ubos"].append(duplicate);
    Check(!ShaderDefinition::ProgramLayout(invalidFrame,schema,false,layout,error),"Duplicate Frame block accepted");
    request.Stage=eShaderStage::Vertex;request.EntryPoint="VSMain";
    request.Source="cbuffer GameSproutDraw:register(b0,space1){column_major float4x4 Model;column_major float4x4 ViewProjection;column_major float4x4 NormalMatrix;}"
        "struct O{float4 Position:SV_Position;float3 Normal:TEXCOORD0;};"
        "O VSMain(float3 p:TEXCOORD0,float3 n:TEXCOORD1){O o;o.Position=mul(ViewProjection,mul(Model,float4(p,1)));o.Normal=mul((float3x3)NormalMatrix,n);return o;}";
    Check(compiler.Compile(request,result)&&AssetArtifactIO::Parse(result.ReflectionJson,description),"Draw layout compile failed: "+result.Error);
    Check(ShaderDefinition::ProgramLayout(description["Reflection"],schema,true,layout,error)&&layout["Uniforms"][0]["Source"]=="Draw",error);
    auto incorrect=description["Reflection"];const auto type=incorrect["ubos"][0]["type"].asString();incorrect["types"][type]["members"][1]["offset"]=32;
    Check(!ShaderDefinition::ProgramLayout(incorrect,schema,true,layout,error),"Invalid Draw matrix offset accepted");
    request.Source+="cbuffer GameSproutFrame:register(b1,space1){float4 Time;float4 CameraPosition;float4 MainLightDirection;float4 MainLightColor;}"
        "float4 VSFrame(float3 p:TEXCOORD0):SV_Position{return mul(ViewProjection,mul(Model,float4(p,1)))+Time+CameraPosition+MainLightDirection+MainLightColor;}";
    request.EntryPoint="VSFrame";
    Check(compiler.Compile(request,result)&&AssetArtifactIO::Parse(result.ReflectionJson,description),"Vertex Frame compile failed: "+result.Error);
    Check(ShaderDefinition::ProgramLayout(description["Reflection"],schema,true,layout,error)
        &&layout["Uniforms"][0]["Source"]=="Draw"&&layout["Uniforms"][1]["Source"]=="Frame",error);
    std::cout<<"PASS Shader author declaration, reflected material padding, typed bindings and engine Draw/Frame ABI\n";
}
