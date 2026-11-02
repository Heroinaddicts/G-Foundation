#pragma once
#include "../RuntimeScriptModule.h"
#include "../RuntimeScriptApi.h"
#include "../RuntimeScriptFieldValues.h"
#include <array>
#include <algorithm>
#include <deque>
#include <list>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <limits>
#include <cmath>
#include <type_traits>

namespace GameSproutRuntime::Internal {
template<typename T> struct NativeSequence { static constexpr bool Value=false; };
template<typename T,typename A> struct NativeSequence<std::vector<T,A>> { static constexpr bool Value=true;static constexpr std::int64_t Length=-1;using Element=T; };
template<typename T,typename A> struct NativeSequence<std::list<T,A>> { static constexpr bool Value=true;static constexpr std::int64_t Length=-1;using Element=T; };
template<typename T,typename A> struct NativeSequence<std::deque<T,A>> { static constexpr bool Value=true;static constexpr std::int64_t Length=-1;using Element=T; };
template<typename T,std::size_t N> struct NativeSequence<std::array<T,N>> { static constexpr bool Value=true;static constexpr std::int64_t Length=N;using Element=T; };
template<typename T,std::size_t N> struct NativeSequence<T[N]> { static constexpr bool Value=true;static constexpr std::int64_t Length=N;using Element=T; };
template<typename T> struct NativeDictionary {static constexpr bool Value=false;};
template<typename K,typename V,typename C,typename A> struct NativeDictionary<std::map<K,V,C,A>> {static constexpr bool Value=true;using Key=K;using Element=V;};
template<typename K,typename V,typename H,typename E,typename A> struct NativeDictionary<std::unordered_map<K,V,H,E,A>> {static constexpr bool Value=true;using Key=K;using Element=V;};
template<typename T> bool ReadNativeValue(const T&,ScriptValue&,const ScriptReferenceContext&,std::string&);
template<typename T> bool WriteNativeValue(T&,const ScriptValue&,const ScriptReferenceContext&,std::string&);
template<typename T> bool ValidateNativeValue(const ScriptValue&,const ScriptReferenceContext&,std::string&);

template<typename T> constexpr ScriptValueKind NativeValueKind() {
    using U=std::remove_cv_t<T>;
    if constexpr(std::is_same_v<U,bool>)return ScriptValueKind::Boolean;
    else if constexpr(std::is_enum_v<U>)return NativeValueKind<std::underlying_type_t<U>>();
    else if constexpr(std::is_integral_v<U>)return std::is_signed_v<U>?ScriptValueKind::Integer:ScriptValueKind::Unsigned;
    else if constexpr(std::is_floating_point_v<U>)return ScriptValueKind::Number;
    else if constexpr(std::is_same_v<U,std::string>)return ScriptValueKind::String;
    else if constexpr(std::is_same_v<U,Vector2>)return ScriptValueKind::Vector2;
    else if constexpr(std::is_same_v<U,Vector3>)return ScriptValueKind::Vector3;
    else if constexpr(std::is_same_v<U,Vector4>)return ScriptValueKind::Vector4;
    else if constexpr(std::is_same_v<U,Quaternion>)return ScriptValueKind::Quaternion;
    else if constexpr(std::is_same_v<U,Color>)return ScriptValueKind::Color;
    else if constexpr(std::is_same_v<U,ResourceReference>)return ScriptValueKind::Resource;
    else if constexpr(std::is_same_v<U,IGameObject*>)return ScriptValueKind::SceneObject;
    else if constexpr(std::is_pointer_v<U> && std::is_base_of_v<IComponent,std::remove_pointer_t<U>>)return ScriptValueKind::Component;
    else if constexpr(NativeSequence<U>::Value)return ScriptValueKind::Sequence;
    else if constexpr(NativeDictionary<U>::Value)return ScriptValueKind::Dictionary;
    else return ScriptValueKind::Structure;
}

template<typename T> struct NativeArraySnapshot {using Type=T;};
template<typename T,std::size_t N> struct NativeArraySnapshot<T[N]> {using Type=std::array<typename NativeArraySnapshot<T>::Type,N>;};
template<typename T> typename NativeArraySnapshot<T>::Type CopyNativeArrayValue(const T& value);
template<typename T,std::size_t N,std::size_t... I>
auto CopyNativeArray(const T (&values)[N],std::index_sequence<I...>) {
    return std::array<typename NativeArraySnapshot<T>::Type,N>{CopyNativeArrayValue(values[I])...};
}
template<typename T> typename NativeArraySnapshot<T>::Type CopyNativeArrayValue(const T& value) {
    if constexpr(std::is_array_v<T>)return CopyNativeArray(value,std::make_index_sequence<std::extent_v<T>>{});
    else return value;
}
template<typename T,std::size_t N>
void CommitNativeArray(T (&output)[N],const std::array<typename NativeArraySnapshot<T>::Type,N>& values) {
    for(std::size_t index=0;index<N;++index){
        if constexpr(std::is_array_v<T>)CommitNativeArray(output[index],values[index]);
        else output[index]=values[index];
    }
}

inline bool ScriptDictionaryKeyLess(const ScriptValue& a,const ScriptValue& b) {
    if(a.Kind!=b.Kind)return a.Kind<b.Kind;
    switch(a.Kind){
    case ScriptValueKind::Boolean:return a.Boolean<b.Boolean;
    case ScriptValueKind::Integer:return a.Integer<b.Integer;
    case ScriptValueKind::Unsigned:return a.Unsigned<b.Unsigned;
    case ScriptValueKind::Number:return a.Number<b.Number;
    case ScriptValueKind::String:return a.String<b.String;
    default:return false;
    }
}

template<typename T> bool ReadNativeValue(const T& input,ScriptValue& output,const ScriptReferenceContext& context,std::string& error) {
    using U=std::remove_cv_t<T>;
    ScriptValue value;value.Kind=NativeValueKind<U>();error.clear();
    if constexpr(std::is_same_v<U,bool>)value.Boolean=input;
    else if constexpr(std::is_enum_v<U>)return ReadNativeValue(static_cast<std::underlying_type_t<U>>(input),output,context,error);
    else if constexpr(std::is_integral_v<U> && std::is_signed_v<U>)value.Integer=input;
    else if constexpr(std::is_integral_v<U>)value.Unsigned=input;
    else if constexpr(std::is_floating_point_v<U>)value.Number=input;
    else if constexpr(std::is_same_v<U,std::string>)value.String=input;
    else if constexpr(std::is_same_v<U,Vector2>)value.Vector2=input;
    else if constexpr(std::is_same_v<U,Vector3>)value.Vector=input;
    else if constexpr(std::is_same_v<U,Vector4>)value.Vector4=input;
    else if constexpr(std::is_same_v<U,Quaternion>)value.Rotation=input;
    else if constexpr(std::is_same_v<U,Color>)value.Tint=input;
    else if constexpr(std::is_same_v<U,ResourceReference>)value.Resource=input;
    else if constexpr(std::is_same_v<U,IGameObject*>) {
        if(input && (!context.EncodeObject || !context.EncodeObject(input,value.SceneObject,error)))return false;
    } else if constexpr(std::is_pointer_v<U> && std::is_base_of_v<IComponent,std::remove_pointer_t<U>>) {
        if(input && (!context.EncodeComponent || !context.EncodeComponent(input,value.Component,error)))return false;
    } else if constexpr(NativeSequence<U>::Value) {
        for(const auto& item:input){ScriptValue element;
            if constexpr(std::is_same_v<typename NativeSequence<U>::Element,bool>){const bool boolean=item;if(!ReadNativeValue(boolean,element,context,error))return false;}
            else if(!ReadNativeValue(item,element,context,error))return false;
            value.Elements.push_back(std::move(element));}
    } else if constexpr(NativeDictionary<U>::Value) {
        for(const auto& item:input){ScriptValue key,element;if(!ReadNativeValue(item.first,key,context,error)||!ReadNativeValue(item.second,element,context,error))return false;value.Entries.emplace_back(std::move(key),std::move(element));}
        std::sort(value.Entries.begin(),value.Entries.end(),[](const auto& a,const auto& b){return ScriptDictionaryKeyLess(a.first,b.first);});
    } else return ScriptFieldAccess<U>::Read(input,output,context,error);
    output=std::move(value);return true;
}

template<typename T> bool ValidateNativeValue(const ScriptValue& value,const ScriptReferenceContext& context,std::string& error) {
    using U=std::remove_cv_t<T>;
    error.clear();
    if(value.Kind!=NativeValueKind<U>()){error="Script field value has the wrong kind";return false;}
    if constexpr(std::is_enum_v<U>)return ValidateNativeValue<std::underlying_type_t<U>>(value,context,error);
    else if constexpr(std::is_integral_v<U> && !std::is_same_v<U,bool> && std::is_signed_v<U>){if(value.Integer<std::numeric_limits<U>::min()||value.Integer>std::numeric_limits<U>::max()){error="Integer is out of range";return false;}}
    else if constexpr(std::is_integral_v<U> && !std::is_same_v<U,bool>){if(value.Unsigned>std::numeric_limits<U>::max()){error="Unsigned integer is out of range";return false;}}
    else if constexpr(std::is_floating_point_v<U>){if(!std::isfinite(value.Number)||std::fabs(value.Number)>std::numeric_limits<U>::max()){error="Number is not finite or out of range";return false;}}
    else if constexpr(std::is_same_v<U,std::string>){if(value.String.size()>1024*1024||value.String.find('\0')!=std::string::npos){error="Invalid script string";return false;}}
    else if constexpr(std::is_same_v<U,Vector2>){if(!std::isfinite(value.Vector2.X)||!std::isfinite(value.Vector2.Y)){error="Vector is not finite";return false;}}
    else if constexpr(std::is_same_v<U,Vector3>){if(!std::isfinite(value.Vector.X)||!std::isfinite(value.Vector.Y)||!std::isfinite(value.Vector.Z)){error="Vector is not finite";return false;}}
    else if constexpr(std::is_same_v<U,Vector4>){if(!std::isfinite(value.Vector4.X)||!std::isfinite(value.Vector4.Y)||!std::isfinite(value.Vector4.Z)||!std::isfinite(value.Vector4.W)){error="Vector is not finite";return false;}}
    else if constexpr(std::is_same_v<U,Quaternion>){if(!std::isfinite(value.Rotation.X)||!std::isfinite(value.Rotation.Y)||!std::isfinite(value.Rotation.Z)||!std::isfinite(value.Rotation.W)){error="Quaternion is not finite";return false;}}
    else if constexpr(std::is_same_v<U,Color>){if(!std::isfinite(value.Tint.R)||!std::isfinite(value.Tint.G)||!std::isfinite(value.Tint.B)||!std::isfinite(value.Tint.A)){error="Color is not finite";return false;}}
    else if constexpr(std::is_same_v<U,IGameObject*>){IGameObject* target=nullptr;if(value.SceneObject.Assigned&&context.DecodeObject&&!context.DecodeObject(value.SceneObject,target,error))return false;}
    else if constexpr(std::is_pointer_v<U> && std::is_base_of_v<IComponent,std::remove_pointer_t<U>>) {
        IComponent* target=nullptr;
        if(value.Component.Assigned&&context.DecodeComponent&&!context.DecodeComponent(value.Component,std::is_base_of_v<IScript,std::remove_pointer_t<U>>,target,error))return false;
        if(target&&!dynamic_cast<U>(target)){error="Referenced component does not match the declared pointer type";return false;}
    } else if constexpr(NativeSequence<U>::Value) {
        if(value.Elements.size()>10000 || (NativeSequence<U>::Length>=0 && value.Elements.size()!=static_cast<std::size_t>(NativeSequence<U>::Length))){error="Invalid sequence length";return false;}
        for(const auto& item:value.Elements)if(!ValidateNativeValue<typename NativeSequence<U>::Element>(item,context,error))return false;
    } else if constexpr(NativeDictionary<U>::Value) {
        if(value.Entries.size()>10000){error="Dictionary is too large";return false;}
        std::set<std::string> keys;
        for(const auto& item:value.Entries){if(!ValidateNativeValue<typename NativeDictionary<U>::Key>(item.first,context,error)||!ValidateNativeValue<typename NativeDictionary<U>::Element>(item.second,context,error))return false;std::string token;if(!ScriptDictionaryKeyToken(item.first,token)||!keys.insert(std::move(token)).second){error="Unsupported or duplicate dictionary key";return false;}}
    } else if constexpr(!std::is_arithmetic_v<U> && !std::is_same_v<U,ResourceReference>)return ScriptFieldAccess<U>::Validate(value,context,error);
    return true;
}

template<typename T> bool WriteNativeValue(T& output,const ScriptValue& value,const ScriptReferenceContext& context,std::string& error) {
    using U=std::remove_cv_t<T>;
    if(!ValidateNativeValue<U>(value,context,error))return false;
    if constexpr(std::is_same_v<U,bool>)output=value.Boolean;
    else if constexpr(std::is_enum_v<U>){std::underlying_type_t<U> next{};if(!WriteNativeValue(next,value,context,error))return false;output=static_cast<U>(next);}
    else if constexpr(std::is_integral_v<U> && std::is_signed_v<U>)output=static_cast<U>(value.Integer);
    else if constexpr(std::is_integral_v<U>)output=static_cast<U>(value.Unsigned);
    else if constexpr(std::is_floating_point_v<U>)output=static_cast<U>(value.Number);
    else if constexpr(std::is_same_v<U,std::string>)output=value.String;
    else if constexpr(std::is_same_v<U,Vector2>)output=value.Vector2;
    else if constexpr(std::is_same_v<U,Vector3>)output=value.Vector;
    else if constexpr(std::is_same_v<U,Vector4>)output=value.Vector4;
    else if constexpr(std::is_same_v<U,Quaternion>)output=value.Rotation;
    else if constexpr(std::is_same_v<U,Color>)output=value.Tint;
    else if constexpr(std::is_same_v<U,ResourceReference>)output=value.Resource;
    else if constexpr(std::is_same_v<U,IGameObject*>){IGameObject* next=nullptr;if(value.SceneObject.Assigned&&(!context.DecodeObject||!context.DecodeObject(value.SceneObject,next,error)))return false;output=next;}
    else if constexpr(std::is_pointer_v<U> && std::is_base_of_v<IComponent,std::remove_pointer_t<U>>){IComponent* next=nullptr;if(value.Component.Assigned&&(!context.DecodeComponent||!context.DecodeComponent(value.Component,std::is_base_of_v<IScript,std::remove_pointer_t<U>>,next,error)))return false;output=dynamic_cast<U>(next);}
    else if constexpr(NativeSequence<U>::Value){
        if constexpr(std::is_array_v<U>){auto next=CopyNativeArray(output,std::make_index_sequence<std::extent_v<U>>{});std::size_t index=0;for(auto& item:next)if(!WriteNativeValue(item,value.Elements[index++],context,error))return false;CommitNativeArray(output,next);}
        else if constexpr(NativeSequence<U>::Length>=0){U next=output;std::size_t index=0;for(auto& item:next)if(!WriteNativeValue(item,value.Elements[index++],context,error))return false;output=std::move(next);}
        else {U next;for(const auto& item:value.Elements){typename NativeSequence<U>::Element element{};if(!WriteNativeValue(element,item,context,error))return false;next.push_back(std::move(element));}output=std::move(next);}
    } else if constexpr(NativeDictionary<U>::Value){U next;for(const auto& item:value.Entries){typename NativeDictionary<U>::Key key{};typename NativeDictionary<U>::Element element{};if(!WriteNativeValue(key,item.first,context,error)||!WriteNativeValue(element,item.second,context,error))return false;if(!next.emplace(std::move(key),std::move(element)).second){error="Duplicate dictionary key";return false;}}output=std::move(next);}
    else {U next=output;if(!ScriptFieldAccess<U>::Write(next,value,context,error))return false;output=std::move(next);}
    return true;
}
}
