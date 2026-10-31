#pragma once
#include "RuntimeAnimationData.h"
#include <JsonCpp/json.hpp>
#include <cmath>
#include <limits>
#include <map>
#include <set>

// Current authoring and prepared Controller schemas. Editor source uses state names;
// the offline compiler resolves them to runtime indices. No migration or old fields.
namespace AnimationDefinition {
inline bool Keys(const Json::Value& value,std::initializer_list<const char*> allowed,std::string& error) {
    if(!value.isObject()){error="Expected animation object";return false;}
    for(const auto& name:value.getMemberNames()) {
        bool found=false;for(const char* key:allowed)if(name==key){found=true;break;}
        if(!found){error="Unsupported animation field: "+name;return false;}
    }
    return true;
}
inline bool Number(const Json::Value& value,double& output) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble()))return false;
    output=value.asDouble();return true;
}
inline bool Reference(const Json::Value& value,GameSproutRuntime::ResourceReference& ref) {
    if(!value.isObject()||!value["Guid"].isString()||!value["LocalID"].isUInt64()||!value["LocalID"].asUInt64())return false;
    for(const auto& key:value.getMemberNames())if(key!="Guid"&&key!="LocalID")return false;
    const auto text=value["Guid"].asString();
    if(text.size()!=32)return false;
    for(char character:text)if(!((character>='0'&&character<='9')||(character>='a'&&character<='f')))return false;
    ref={text,value["LocalID"].asUInt64()};return true;
}
inline Json::Value Reference(const GameSproutRuntime::ResourceReference& ref) {
    Json::Value result;result["Guid"]=ref.Guid;result["LocalID"]=Json::UInt64(ref.LocalID);return result;
}
inline const char* ParameterName(GameSproutRuntime::AnimatorParameterType type) {
    switch(type){case GameSproutRuntime::AnimatorParameterType::Float:return "Float";case GameSproutRuntime::AnimatorParameterType::Int:return "Int";
    case GameSproutRuntime::AnimatorParameterType::Bool:return "Bool";case GameSproutRuntime::AnimatorParameterType::Trigger:return "Trigger";}return "Invalid";
}
inline const char* CompareName(GameSproutRuntime::AnimatorCompare value) {
    switch(value){case GameSproutRuntime::AnimatorCompare::Greater:return "Greater";case GameSproutRuntime::AnimatorCompare::Less:return "Less";
    case GameSproutRuntime::AnimatorCompare::Equal:return "Equal";case GameSproutRuntime::AnimatorCompare::NotEqual:return "NotEqual";
    case GameSproutRuntime::AnimatorCompare::If:return "If";case GameSproutRuntime::AnimatorCompare::IfNot:return "IfNot";}return "Invalid";
}
inline bool ReadParameters(const Json::Value& source, std::vector<GameSproutRuntime::AnimatorParameter>& parametersOutput,
    std::map<std::string,GameSproutRuntime::AnimatorParameterType>& parameters, std::string& error) {
    const auto fail=[&error](const std::string& message){error=message;return false;};
    for(const auto& value:source["Parameters"]) {
        GameSproutRuntime::AnimatorParameter parameter;
        if(!Keys(value,{"Name","Type","DefaultValue"},error))return false;
        if(!value["Name"].isString()||value["Name"].asString().empty()||value["Name"].asString().size()>256||!value["Type"].isString()
            ||!Number(value["DefaultValue"],parameter.DefaultValue))return fail("Invalid Animator parameter");
        parameter.Name=value["Name"].asString();const auto type=value["Type"].asString();
        if(type=="Float")parameter.Type=GameSproutRuntime::AnimatorParameterType::Float;
        else if(type=="Int")parameter.Type=GameSproutRuntime::AnimatorParameterType::Int;
        else if(type=="Bool")parameter.Type=GameSproutRuntime::AnimatorParameterType::Bool;
        else if(type=="Trigger")parameter.Type=GameSproutRuntime::AnimatorParameterType::Trigger;
        else return fail("Unsupported Animator parameter type: "+type);
        if(!parameters.emplace(parameter.Name,parameter.Type).second)return fail("Duplicate Animator parameter: "+parameter.Name);
        if(parameter.Type==GameSproutRuntime::AnimatorParameterType::Int && (parameter.DefaultValue<std::numeric_limits<int>::min()
            ||parameter.DefaultValue>std::numeric_limits<int>::max()||std::floor(parameter.DefaultValue)!=parameter.DefaultValue))return fail("Integer default is out of range");
        if((parameter.Type==GameSproutRuntime::AnimatorParameterType::Bool||parameter.Type==GameSproutRuntime::AnimatorParameterType::Trigger)
            &&parameter.DefaultValue!=0&&parameter.DefaultValue!=1)return fail("Boolean default must be 0 or 1");
        if(std::fabs(parameter.DefaultValue)>std::numeric_limits<float>::max())return fail("Parameter default exceeds runtime range");
        parametersOutput.push_back(std::move(parameter));
    }
    return true;
}
inline bool ReadLayerGraph(Json::Value source,bool prepared,
    const std::map<std::string,GameSproutRuntime::AnimatorParameterType>& parameters,
    GameSproutRuntime::AnimatorLayer& data,std::string& error) {
    const auto fail=[&error](const std::string& message){error=message;return false;};
    if(!prepared) {
        if(!source.isMember("Transitions"))source["Transitions"]=Json::Value(Json::arrayValue);
        if(source["States"].isArray())for(auto& state:source["States"])if(state.isObject()) {
            if(!state.isMember("Speed"))state["Speed"]=1;
            if(!state.isMember("Loop"))state["Loop"]=true;
            if(state.isMember("Motions")&&state["Motions"].isArray())for(auto& motion:state["Motions"])if(motion.isObject()&&!motion.isMember("Speed"))motion["Speed"]=1;
        }
        if(source["Transitions"].isArray())for(auto& transition:source["Transitions"])if(transition.isObject()) {
            if(!transition.isMember("HasExitTime"))transition["HasExitTime"]=false;
            if(!transition.isMember("ExitTime"))transition["ExitTime"]=1;
            if(!transition.isMember("Duration"))transition["Duration"]=0.15;
            if(!transition.isMember("TargetOffset"))transition["TargetOffset"]=0;
            if(!transition.isMember("CanTransitionToSelf"))transition["CanTransitionToSelf"]=false;
            if(!transition.isMember("Conditions"))transition["Conditions"]=Json::Value(Json::arrayValue);
            if(transition["Conditions"].isArray())for(auto& condition:transition["Conditions"])if(condition.isObject()&&!condition.isMember("Value"))condition["Value"]=0;
        }
        if(!source.isMember("DefaultState")&&source["States"].isArray()&&!source["States"].empty())source["DefaultState"]=source["States"][0]["Name"];
    }
    if(!source["States"].isArray()||source["States"].empty()||source["States"].size()>4096
        ||!source["Transitions"].isArray()||source["Transitions"].size()>16384)
        return fail("Each layer requires States and Transitions arrays within supported limits");
    std::map<std::string,std::uint32_t> states;
    for(const auto& value:source["States"]) {
        GameSproutRuntime::AnimatorState state;double number;
        if(!Keys(value,{"Name","Clip","Speed","Loop","BlendParameter","Motions"},error))return false;
        if(!value["Name"].isString()||value["Name"].asString().empty()||value["Name"]=="AnyState"||value["Name"].asString().size()>256
            ||!Number(value["Speed"],number)||number<0||number>1000||!value["Loop"].isBool())return fail("Invalid Animator state");
        state.Name=value["Name"].asString();state.Speed=static_cast<float>(number);state.Loop=value["Loop"].asBool();
        if(!states.emplace(state.Name,static_cast<std::uint32_t>(data.States.size())).second)return fail("Duplicate Animator state: "+state.Name);
        if(value.isMember("BlendParameter")) {
            if(!value["BlendParameter"].isString()||value.isMember("Clip"))return fail("Blend state cannot have a direct Clip");
            state.BlendParameter=value["BlendParameter"].asString();const auto parameter=parameters.find(state.BlendParameter);
            if(parameter==parameters.end()||parameter->second!=GameSproutRuntime::AnimatorParameterType::Float)return fail("1D BlendParameter must reference a Float parameter");
            if(!value["Motions"].isArray()||value["Motions"].size()<2||value["Motions"].size()>256)return fail("1D blend requires at least two motions");
            for(const auto& motionValue:value["Motions"]) {
                GameSproutRuntime::BlendMotion motion;
                if(!Keys(motionValue,{"Clip","Threshold","Speed"},error))return false;
                if(!Reference(motionValue["Clip"],motion.Clip)||!Number(motionValue["Threshold"],number)||std::fabs(number)>1e20)return fail("Invalid blend motion");
                motion.Threshold=static_cast<float>(number);
                if(!state.Motions.empty()&&motion.Threshold<=state.Motions.back().Threshold)return fail("Blend thresholds must be strictly increasing");
                if(!Number(motionValue["Speed"],number)||number<=0||number>1000)return fail("Invalid blend motion speed");
                motion.Speed=static_cast<float>(number);state.Motions.push_back(std::move(motion));
            }
        } else if(value.isMember("Motions")||!Reference(value["Clip"],state.Clip))return fail("State requires a valid Clip reference");
        data.States.push_back(std::move(state));
    }
    if(prepared) {
        if(!source["DefaultState"].isUInt()||source["DefaultState"].asUInt()>=data.States.size())return fail("Invalid compiled default state");
        data.DefaultState=source["DefaultState"].asUInt();
    }else{
        if(!source["DefaultState"].isString()||!states.count(source["DefaultState"].asString()))return fail("DefaultState must name a state");
        data.DefaultState=states.at(source["DefaultState"].asString());
    }
    for(const auto& value:source["Transitions"]) {
        GameSproutRuntime::AnimatorTransition transition;double number;
        if(!Keys(value,{"From","To","Conditions","HasExitTime","ExitTime","Duration","TargetOffset","CanTransitionToSelf"},error))return false;
        if(prepared) {
            if(!value["From"].isInt()||value["From"].asInt()<-1||value["From"].asInt()>=static_cast<int>(data.States.size())
                ||!value["To"].isUInt()||value["To"].asUInt()>=data.States.size())return fail("Invalid compiled transition index");
            transition.From=value["From"].asInt();transition.To=value["To"].asUInt();
        }else{
            if(!value["From"].isString()||!value["To"].isString()||!states.count(value["To"].asString()))return fail("Transition points to a missing state");
            const auto from=value["From"].asString();if(from!="AnyState"&&!states.count(from))return fail("Transition source state is missing");
            transition.From=from=="AnyState"?-1:static_cast<int>(states.at(from));transition.To=states.at(value["To"].asString());
        }
        if(!value["HasExitTime"].isBool()||!value["CanTransitionToSelf"].isBool())return fail("Transition flags must be boolean");
        transition.HasExitTime=value["HasExitTime"].asBool();transition.CanTransitionToSelf=value["CanTransitionToSelf"].asBool();
        if(!Number(value["ExitTime"],number)||number<0||number>1000000)return fail("Invalid exit time");transition.ExitTime=static_cast<float>(number);
        if(!Number(value["Duration"],number)||number<0||number>3600)return fail("Invalid transition duration");transition.Duration=static_cast<float>(number);
        if(!Number(value["TargetOffset"],number)||number<0||number>1)return fail("TargetOffset must be normalized to [0,1]");transition.TargetOffset=static_cast<float>(number);
        if(!value["Conditions"].isArray()||value["Conditions"].size()>256)return fail("Invalid transition conditions");
        for(const auto& conditionValue:value["Conditions"]) {
            GameSproutRuntime::AnimatorCondition condition;
            if(!Keys(conditionValue,{"Parameter","Compare","Value"},error))return false;
            if(!conditionValue["Parameter"].isString()||!conditionValue["Compare"].isString()||!Number(conditionValue["Value"],condition.Value))return fail("Invalid transition condition");
            condition.Parameter=conditionValue["Parameter"].asString();const auto parameter=parameters.find(condition.Parameter);
            if(parameter==parameters.end())return fail("Transition parameter is missing: "+condition.Parameter);
            const auto compare=conditionValue["Compare"].asString();
            if(compare=="Greater")condition.Compare=GameSproutRuntime::AnimatorCompare::Greater;
            else if(compare=="Less")condition.Compare=GameSproutRuntime::AnimatorCompare::Less;
            else if(compare=="Equal")condition.Compare=GameSproutRuntime::AnimatorCompare::Equal;
            else if(compare=="NotEqual")condition.Compare=GameSproutRuntime::AnimatorCompare::NotEqual;
            else if(compare=="If")condition.Compare=GameSproutRuntime::AnimatorCompare::If;
            else if(compare=="IfNot")condition.Compare=GameSproutRuntime::AnimatorCompare::IfNot;
            else return fail("Unsupported condition comparison");
            const bool boolean=parameter->second==GameSproutRuntime::AnimatorParameterType::Bool||parameter->second==GameSproutRuntime::AnimatorParameterType::Trigger;
            const bool booleanComparison=condition.Compare==GameSproutRuntime::AnimatorCompare::If||condition.Compare==GameSproutRuntime::AnimatorCompare::IfNot;
            if(boolean!=booleanComparison)return fail("Condition comparison does not match parameter type");
            transition.Conditions.push_back(std::move(condition));
        }
        if(transition.Conditions.empty()&&!transition.HasExitTime)return fail("Transition needs conditions or ExitTime");
        if(transition.From<0&&transition.HasExitTime)return fail("AnyState transition cannot have ExitTime");
        if(transition.From==static_cast<int>(transition.To)&&!transition.CanTransitionToSelf)return fail("Self transition is disabled");
        data.Transitions.push_back(std::move(transition));
    }
    return true;
}
inline bool ReadController(const Json::Value& input,bool prepared,GameSproutRuntime::AnimatorControllerData& output,std::string& error) {
    output={};error.clear();Json::Value source=input;
    const auto fail=[&error](const std::string& message){error=message;return false;};
    if(!Keys(source,{"Version","Format","Parameters","Layers"},error))return false;
    if(source["Version"]!=1||(!prepared&&source.isMember("Format"))||(prepared&&source["Format"]!="AnimatorController3"))
        return fail("Unsupported AnimatorController format");
    if(!prepared&&!source.isMember("Parameters"))source["Parameters"]=Json::Value(Json::arrayValue);
    if(!source["Parameters"].isArray()||source["Parameters"].size()>256||!source["Layers"].isArray()
        ||source["Layers"].empty()||source["Layers"].size()>32)return fail("Controller requires Parameters and 1 to 32 Layers");
    if(!prepared)for(auto& parameter:source["Parameters"])if(parameter.isObject()&&!parameter.isMember("DefaultValue"))parameter["DefaultValue"]=0;
    GameSproutRuntime::AnimatorControllerData data;
    std::map<std::string,GameSproutRuntime::AnimatorParameterType> parameters;
    if(!ReadParameters(source,data.Parameters,parameters,error))return false;
    std::set<std::string> names;std::vector<std::string> synchronizedNames;
    for(auto value:source["Layers"]) {
        if(!Keys(value,{"Name","BlendingMode","DefaultWeight","Mask","ReferenceClip","ReferenceTime","States","Transitions","DefaultState","IKPass","SyncedLayer","SyncedLayerIndex","SyncedLayerAffectsTiming","MotionOverrides"},error))return false;
        if(!prepared) {
            if(!value.isMember("BlendingMode"))value["BlendingMode"]="Override";
            if(!value.isMember("DefaultWeight"))value["DefaultWeight"]=1;
            if(!value.isMember("ReferenceTime"))value["ReferenceTime"]=0;
        }
        GameSproutRuntime::AnimatorLayer layer;double number;
        if(value.isMember("IKPass")){if(!value["IKPass"].isBool())return fail("IKPass must be boolean");layer.IKPass=value["IKPass"].asBool();}
        if(value.isMember("SyncedLayerAffectsTiming")){if(!value["SyncedLayerAffectsTiming"].isBool())return fail("SyncedLayerAffectsTiming must be boolean");layer.SyncedLayerAffectsTiming=value["SyncedLayerAffectsTiming"].asBool();}
        std::string synchronizedName;
        if(prepared) {
            if(value.isMember("SyncedLayer")||!value["SyncedLayerIndex"].isInt()||value["SyncedLayerIndex"].asInt()<-1)return fail("Invalid compiled sync layer index");
            layer.SyncedLayerIndex=value["SyncedLayerIndex"].asInt();
        } else {
            if(value.isMember("SyncedLayerIndex"))return fail("Source sync layers use names, not indices");
            if(value.isMember("SyncedLayer")){if(!value["SyncedLayer"].isString()||value["SyncedLayer"].asString().empty())return fail("SyncedLayer must name a layer");synchronizedName=value["SyncedLayer"].asString();layer.SyncedLayerIndex=0;}
        }
        synchronizedNames.push_back(synchronizedName);
        if(!value["Name"].isString()||value["Name"].asString().empty()||value["Name"].asString().size()>256
            ||!names.insert(value["Name"].asString()).second)return fail("Layer names must be nonempty and unique");
        layer.Name=value["Name"].asString();
        if(value["BlendingMode"]=="Override")layer.BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Override;
        else if(value["BlendingMode"]=="Additive")layer.BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Additive;
        else return fail("Unsupported layer BlendingMode");
        if(!Number(value["DefaultWeight"],number)||number<0||number>1)return fail("Layer DefaultWeight must be in [0,1]");
        layer.DefaultWeight=static_cast<float>(number);
        if(value.isMember("Mask")&&!Reference(value["Mask"],layer.Mask))return fail("Invalid layer AvatarMask reference");
        if(value.isMember("ReferenceClip")&&!Reference(value["ReferenceClip"],layer.ReferenceClip))return fail("Invalid additive ReferenceClip");
        if(!Number(value["ReferenceTime"],number)||number<0||number>1000000)return fail("Invalid additive ReferenceTime");
        layer.ReferenceTime=static_cast<float>(number);
        if(layer.BlendingMode!=GameSproutRuntime::AnimatorLayerBlendingMode::Additive
            &&(!layer.ReferenceClip.Guid.empty()||layer.ReferenceTime!=0))return fail("Only Additive layers support a reference pose clip");
        if(layer.ReferenceClip.Guid.empty()&&layer.ReferenceTime!=0)return fail("ReferenceTime requires ReferenceClip");
        if(data.Layers.empty()&&(layer.BlendingMode!=GameSproutRuntime::AnimatorLayerBlendingMode::Override||layer.DefaultWeight!=1
            ||!layer.Mask.Guid.empty()||!layer.ReferenceClip.Guid.empty()))return fail("Base layer must be Override, weight 1, without Mask or ReferenceClip");
        if(layer.SyncedLayerIndex<0) {
            if(layer.SyncedLayerAffectsTiming||value.isMember("MotionOverrides"))return fail("Only synchronized layers can override motions or affect timing");
            if(!ReadLayerGraph(value,prepared,parameters,layer,error))return false;
        } else {
            if(data.Layers.empty()||value.isMember("States")||value.isMember("Transitions")||value.isMember("DefaultState"))return fail("Synchronized layer inherits source state machine; independent graph fields are forbidden");
            if(layer.SyncedLayerAffectsTiming&&layer.BlendingMode!=GameSproutRuntime::AnimatorLayerBlendingMode::Override)return fail("Additive sync layers cannot affect source timing");
            if(value.isMember("MotionOverrides")&&(!value["MotionOverrides"].isArray()||value["MotionOverrides"].size()>4096))return fail("Invalid sync motion overrides");
            std::set<std::string> states;
            for(auto overrideValue:value["MotionOverrides"]) {
                if(!Keys(overrideValue,{"State","Clip","BlendParameter","Motions"},error))return false;
                if(!overrideValue["State"].isString()||overrideValue["State"].asString().empty()||!states.insert(overrideValue["State"].asString()).second)return fail("Sync motion overrides must identify unique source states");
                const auto name=overrideValue["State"].asString();overrideValue.removeMember("State");overrideValue["Name"]=name;overrideValue["Speed"]=1;overrideValue["Loop"]=true;
                Json::Value graph;graph["States"]=Json::Value(Json::arrayValue);graph["States"].append(overrideValue);graph["Transitions"]=Json::Value(Json::arrayValue);graph["DefaultState"]=prepared?Json::Value(0U):Json::Value(name);
                GameSproutRuntime::AnimatorLayer checked;if(!ReadLayerGraph(graph,prepared,parameters,checked,error))return false;
                GameSproutRuntime::AnimatorMotionOverride motion;motion.State=name;motion.Clip=checked.States[0].Clip;motion.BlendParameter=checked.States[0].BlendParameter;motion.Motions=checked.States[0].Motions;layer.MotionOverrides.push_back(std::move(motion));
            }
        }
        data.Layers.push_back(std::move(layer));
    }
    for(std::size_t index=0;index<data.Layers.size();++index) {
        auto& layer=data.Layers[index];
        if(!prepared&&!synchronizedNames[index].empty()) {
            bool found=false;for(std::size_t sourceIndex=0;sourceIndex<data.Layers.size();++sourceIndex)if(data.Layers[sourceIndex].Name==synchronizedNames[index]){layer.SyncedLayerIndex=static_cast<int>(sourceIndex);found=true;break;}
            if(!found)return fail("Sync source layer is missing: "+synchronizedNames[index]);
        }
        if(layer.SyncedLayerIndex<0)continue;
        if(layer.SyncedLayerIndex>=static_cast<int>(data.Layers.size())||layer.SyncedLayerIndex==static_cast<int>(index)||data.Layers[layer.SyncedLayerIndex].SyncedLayerIndex>=0)return fail("Sync layer cycles, chains or invalid sources are forbidden");
        const auto& graph=data.Layers[layer.SyncedLayerIndex];
        for(const auto& motion:layer.MotionOverrides) {
            bool found=false;for(const auto& state:graph.States)if(state.Name==motion.State){found=true;break;}
            if(!found)return fail("Motion override names a missing source state: "+motion.State);
        }
    }
    output=std::move(data);return true;
}
inline Json::Value LayerGraphJson(const GameSproutRuntime::AnimatorLayer& data,bool prepared) {
    Json::Value result;
    result["DefaultState"]=prepared?Json::Value(data.DefaultState):Json::Value(data.DefaultState<data.States.size()?data.States[data.DefaultState].Name:"");
    result["States"]=Json::Value(Json::arrayValue);result["Transitions"]=Json::Value(Json::arrayValue);
    for(const auto& state:data.States){Json::Value value;value["Name"]=state.Name;value["Speed"]=state.Speed;value["Loop"]=state.Loop;
        if(state.BlendParameter.empty())value["Clip"]=Reference(state.Clip);
        else{value["BlendParameter"]=state.BlendParameter;value["Motions"]=Json::Value(Json::arrayValue);for(const auto& motion:state.Motions){Json::Value child;child["Clip"]=Reference(motion.Clip);child["Threshold"]=motion.Threshold;child["Speed"]=motion.Speed;value["Motions"].append(child);}}
        result["States"].append(value);}
    for(const auto& transition:data.Transitions){Json::Value value;
        value["From"]=prepared?Json::Value(transition.From):Json::Value(transition.From<0?"AnyState":(static_cast<unsigned>(transition.From)<data.States.size()?data.States[transition.From].Name:""));
        value["To"]=prepared?Json::Value(transition.To):Json::Value(transition.To<data.States.size()?data.States[transition.To].Name:"");
        value["HasExitTime"]=transition.HasExitTime;value["ExitTime"]=transition.ExitTime;value["Duration"]=transition.Duration;value["TargetOffset"]=transition.TargetOffset;value["CanTransitionToSelf"]=transition.CanTransitionToSelf;
        value["Conditions"]=Json::Value(Json::arrayValue);for(const auto& condition:transition.Conditions){Json::Value child;child["Parameter"]=condition.Parameter;child["Compare"]=CompareName(condition.Compare);child["Value"]=condition.Value;value["Conditions"].append(child);}result["Transitions"].append(value);}
    return result;
}
inline Json::Value ControllerJson(const GameSproutRuntime::AnimatorControllerData& data,bool prepared) {
    Json::Value result;result["Version"]=1;if(prepared)result["Format"]="AnimatorController3";
    result["Parameters"]=Json::Value(Json::arrayValue);result["Layers"]=Json::Value(Json::arrayValue);
    for(const auto& parameter:data.Parameters){Json::Value value;value["Name"]=parameter.Name;value["Type"]=ParameterName(parameter.Type);value["DefaultValue"]=parameter.DefaultValue;result["Parameters"].append(value);}
    for(const auto& layer:data.Layers) {
        Json::Value value=layer.SyncedLayerIndex<0?LayerGraphJson(layer,prepared):Json::Value(Json::objectValue);value["Name"]=layer.Name;
        value["IKPass"]=layer.IKPass;value["SyncedLayerAffectsTiming"]=layer.SyncedLayerAffectsTiming;
        if(prepared)value["SyncedLayerIndex"]=layer.SyncedLayerIndex;
        else if(layer.SyncedLayerIndex>=0)value["SyncedLayer"]=static_cast<std::size_t>(layer.SyncedLayerIndex)<data.Layers.size()?data.Layers[layer.SyncedLayerIndex].Name:"";
        if(layer.SyncedLayerIndex>=0) {
            value["MotionOverrides"]=Json::Value(Json::arrayValue);
            for(const auto& motion:layer.MotionOverrides) {
                Json::Value entry;entry["State"]=motion.State;
                if(motion.BlendParameter.empty())entry["Clip"]=Reference(motion.Clip);
                else {entry["BlendParameter"]=motion.BlendParameter;entry["Motions"]=Json::Value(Json::arrayValue);for(const auto& child:motion.Motions){Json::Value item;item["Clip"]=Reference(child.Clip);item["Threshold"]=child.Threshold;item["Speed"]=child.Speed;entry["Motions"].append(item);}}
                value["MotionOverrides"].append(entry);
            }
        }
        value["BlendingMode"]=layer.BlendingMode==GameSproutRuntime::AnimatorLayerBlendingMode::Additive?"Additive":layer.BlendingMode==GameSproutRuntime::AnimatorLayerBlendingMode::Override?"Override":"Invalid";
        value["DefaultWeight"]=layer.DefaultWeight;value["ReferenceTime"]=layer.ReferenceTime;
        if(!layer.Mask.Guid.empty()||layer.Mask.LocalID!=0)value["Mask"]=Reference(layer.Mask);
        if(!layer.ReferenceClip.Guid.empty()||layer.ReferenceClip.LocalID!=0)value["ReferenceClip"]=Reference(layer.ReferenceClip);
        result["Layers"].append(value);
    }
    return result;
}
}
