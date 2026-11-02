#pragma once
#include "RuntimeScriptApi.h"
#include <cmath>
#include <set>
#include <cstring>

namespace GameSproutRuntime {
// Exact typed tokens keep uint64 keys lossless and give dictionaries O(n log n)
// uniqueness checks. -0 and +0 are the same floating-point map key.
inline bool ScriptDictionaryKeyToken(const ScriptValue& value,std::string& token) {
    switch(value.Kind){
    case ScriptValueKind::Boolean:token=value.Boolean?"b1":"b0";return true;
    case ScriptValueKind::Integer:token="i"+std::to_string(value.Integer);return true;
    case ScriptValueKind::Unsigned:token="u"+std::to_string(value.Unsigned);return true;
    case ScriptValueKind::String:token="s"+std::to_string(value.String.size())+":"+value.String;return true;
    case ScriptValueKind::Number:{if(!std::isfinite(value.Number))return false;std::uint64_t bits=0;const double number=value.Number==0?0:value.Number;std::memcpy(&bits,&number,sizeof(bits));token="n"+std::to_string(bits);return true;}
    default:return false;
    }
}
inline bool ValidateScriptValue(const ScriptValue& value,std::string& error,bool persistent=false,std::size_t depth=0,std::size_t* budget=nullptr) {
    std::size_t ownBudget=10000;if(!budget)budget=&ownBudget;
    if(depth>32||!*budget){error="Script value exceeds nesting or element limit";return false;}--*budget;
    const auto finite=[](std::initializer_list<double> values){for(double value:values)if(!std::isfinite(value))return false;return true;};
    switch(value.Kind){
    case ScriptValueKind::Boolean:case ScriptValueKind::Integer:case ScriptValueKind::Unsigned:break;
    case ScriptValueKind::Number:if(!finite({value.Number})){error="Number is not finite";return false;}break;
    case ScriptValueKind::String:if(value.String.size()>1024*1024||value.String.find('\0')!=std::string::npos){error="Invalid script string";return false;}break;
    case ScriptValueKind::Vector2:if(!finite({value.Vector2.X,value.Vector2.Y})){error="Vector is not finite";return false;}break;
    case ScriptValueKind::Vector3:if(!finite({value.Vector.X,value.Vector.Y,value.Vector.Z})){error="Vector is not finite";return false;}break;
    case ScriptValueKind::Vector4:if(!finite({value.Vector4.X,value.Vector4.Y,value.Vector4.Z,value.Vector4.W})){error="Vector is not finite";return false;}break;
    case ScriptValueKind::Quaternion:if(!finite({value.Rotation.X,value.Rotation.Y,value.Rotation.Z,value.Rotation.W})){error="Quaternion is not finite";return false;}break;
    case ScriptValueKind::Color:if(!finite({value.Tint.R,value.Tint.G,value.Tint.B,value.Tint.A})){error="Color is not finite";return false;}break;
    case ScriptValueKind::Resource: {
        const auto& resource=value.Resource;
        if(resource.Guid.empty()){if(resource.LocalID){error="Resource LocalID has no GUID";return false;}break;}
        if(resource.Guid.size()!=32||!resource.LocalID){error="Invalid resource identity";return false;}
        for(char character:resource.Guid)if(!((character>='0'&&character<='9')||(character>='a'&&character<='f')||(character>='A'&&character<='F'))){error="Invalid resource GUID";return false;}
        break;
    }
    case ScriptValueKind::SceneObject:{const auto& reference=value.SceneObject;
        if(!reference.Assigned){if(reference.ObjectFileID||reference.RuntimeSceneID||reference.RuntimeObjectID){error="Invalid null object reference";return false;}}
        else if(reference.RuntimeSceneID||reference.RuntimeObjectID){if(persistent||reference.ObjectFileID||!reference.RuntimeSceneID||!reference.RuntimeObjectID){error="Invalid transient object reference";return false;}}
        break;
    }
    case ScriptValueKind::Component:{const auto& reference=value.Component;
        if(!reference.Assigned){if(reference.ComponentFileID||reference.RuntimeSceneID||reference.RuntimeObjectID||reference.RuntimeComponentID){error="Invalid null component reference";return false;}}
        else if(reference.RuntimeSceneID||reference.RuntimeObjectID||reference.RuntimeComponentID){if(persistent||reference.ComponentFileID||!reference.RuntimeSceneID||!reference.RuntimeObjectID||!reference.RuntimeComponentID){error="Invalid transient component reference";return false;}}
        else if(!reference.ComponentFileID){error="Component reference needs a nonzero FileID";return false;}
        break;
    }
    case ScriptValueKind::Structure:for(const auto& [id,child]:value.Fields){if(id.empty()||id.size()>1024||!ValidateScriptValue(child,error,persistent,depth+1,budget))return false;}break;
    case ScriptValueKind::Sequence:for(const auto& child:value.Elements)if(!ValidateScriptValue(child,error,persistent,depth+1,budget))return false;break;
    case ScriptValueKind::Dictionary: {
        std::set<std::string> keys;
        for(const auto& item:value.Entries){
            if(!ValidateScriptValue(item.first,error,persistent,depth+1,budget)||!ValidateScriptValue(item.second,error,persistent,depth+1,budget))return false;
            std::string token;if(!ScriptDictionaryKeyToken(item.first,token)){error="Unsupported dictionary key kind";return false;}
            if(!keys.insert(std::move(token)).second){error="Duplicate dictionary key";return false;}
        }break;
    }
    default:error="Unknown script value kind";return false;
    }
    error.clear();return true;
}
inline bool ValidateScriptFieldValue(const ScriptFieldInfo& schema,const ScriptValue& value,std::string& error,std::size_t depth=0) {
    if(depth>32){error="Script schema is too deep";return false;}
    if(schema.Default.Kind!=value.Kind){error="Script value does not match its schema";return false;}
    if(!schema.EnumValues.empty()){
        bool found=false;for(const auto& item:schema.EnumValues)if((item.IsUnsigned&&value.Kind==ScriptValueKind::Unsigned&&item.UnsignedValue==value.Unsigned)||(!item.IsUnsigned&&value.Kind==ScriptValueKind::Integer&&item.Value==value.Integer)){found=true;break;}
        if(!found){error="Enum value is not declared";return false;}
    }
    switch(value.Kind){
    case ScriptValueKind::Structure:
        if(value.Fields.size()!=schema.Children.size()){error="Structure member count does not match schema";return false;}
        for(const auto& child:schema.Children){const auto found=value.Fields.find(child.ID);if(found==value.Fields.end()||!ValidateScriptFieldValue(child,found->second,error,depth+1))return false;}break;
    case ScriptValueKind::Sequence:
        if(schema.Children.size()!=1||(schema.FixedLength>=0&&value.Elements.size()!=static_cast<std::size_t>(schema.FixedLength))){error="Sequence shape does not match schema";return false;}
        for(const auto& child:value.Elements)if(!ValidateScriptFieldValue(schema.Children[0],child,error,depth+1))return false;break;
    case ScriptValueKind::Dictionary:
        if(schema.Children.size()!=2){error="Dictionary schema needs key and value";return false;}
        for(const auto& item:value.Entries)if(!ValidateScriptFieldValue(schema.Children[0],item.first,error,depth+1)||!ValidateScriptFieldValue(schema.Children[1],item.second,error,depth+1))return false;break;
    default:break;
    }
    error.clear();return true;
}
// Schema evolution uses declared defaults at removed or changed fields. It does
// not translate obsolete file formats or retain runtime addresses.
inline ScriptValue ProjectSerializedScriptValue(const ScriptValue& value,const ScriptFieldInfo& schema) {
    if(value.Kind!=schema.Default.Kind)return schema.Default;
    ScriptValue output=value;
    switch(value.Kind){
    case ScriptValueKind::Structure:
        output.Fields.clear();for(const auto& child:schema.Children){const auto found=value.Fields.find(child.ID);output.Fields.emplace(child.ID,found==value.Fields.end()?child.Default:ProjectSerializedScriptValue(found->second,child));}break;
    case ScriptValueKind::Sequence:
        if(schema.Children.size()!=1)return schema.Default;
        if(schema.FixedLength>=0)output.Elements.resize(static_cast<std::size_t>(schema.FixedLength),schema.Children[0].Default);
        for(auto& child:output.Elements)child=ProjectSerializedScriptValue(child,schema.Children[0]);break;
    case ScriptValueKind::Dictionary:
        if(schema.Children.size()!=2)return schema.Default;
        for(auto& item:output.Entries){item.first=ProjectSerializedScriptValue(item.first,schema.Children[0]);item.second=ProjectSerializedScriptValue(item.second,schema.Children[1]);}break;
    default:break;
    }
    std::string error;if(!ValidateScriptValue(output,error,true)||!ValidateScriptFieldValue(schema,output,error))return schema.Default;
    return output;
}
inline std::map<std::string,ScriptValue> ProjectSerializedScriptFields(const std::map<std::string,ScriptValue>& values,const ScriptTypeInfo* type) {
    if(!type)return values;
    std::map<std::string,ScriptValue> result;
    for(const auto& field:type->Fields){const auto found=values.find(field.ID);result.emplace(field.ID,found==values.end()?field.Default:ProjectSerializedScriptValue(found->second,field));}
    return result;
}
}
