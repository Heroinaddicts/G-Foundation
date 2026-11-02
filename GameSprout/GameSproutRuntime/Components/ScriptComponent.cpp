#include "ScriptComponent.h"
#include "../GameObject.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace GameSproutRuntime {
namespace {
bool Valid(const ScriptValue& value) {
    if(value.Kind==ScriptValueKind::Number) return std::isfinite(value.Number);
    if(value.Kind==ScriptValueKind::Vector3) return std::isfinite(value.Vector.X)&&std::isfinite(value.Vector.Y)&&std::isfinite(value.Vector.Z);
    if(value.Kind==ScriptValueKind::SceneObject) return value.SceneObject.Assigned || value.SceneObject.ObjectFileID==0;
    return value.Kind>=ScriptValueKind::Boolean&&value.Kind<=ScriptValueKind::SceneObject;
}
}
ScriptComponent::ScriptComponent(Runtime& runtime,IGameObject* owner,std::string type,ResourceReference source,std::uint64_t id)
    :IScriptComponent(owner),_Runtime(runtime),_TypeID(std::move(type)),_Source(std::move(source)),_InstanceID(id) {BindDefaults();}
ScriptComponent::~ScriptComponent() {Stop();}
ScriptComponent::InvocationScope::~InvocationScope() {
    if(--Owner._InvocationDepth==0 && (Owner._StopRequested||!Owner._Enabled||!Owner.IsAttached())) Owner.Stop();
}
bool ScriptComponent::IsScriptTypeAvailable() const {return _Runtime.FindScriptType(_TypeID)!=nullptr;}
void ScriptComponent::BindDefaults() {
    if(const auto* type=_Runtime.FindScriptType(_TypeID))
        for(const auto& field:type->Info.Fields) _Fields.try_emplace(field.ID,field.Default);
    _Failed=false;_Error.clear();
}
void ScriptComponent::ReplaceAuthoredFields(std::map<std::string,ScriptValue>& fields) noexcept {
    _Fields.swap(fields);_Failed=false;_Error.clear();
}
bool ScriptComponent::IsAttached() const {
    const auto* node=_Runtime.Find(_GameObject);
    if(!_Runtime.IsUsable(node)) return false;
    for(const auto& entry:node->Components) if(entry.Pointer==this) return !entry.PendingRemove;
    return false;
}
void ScriptComponent::Stop() noexcept {
    // Disabling oneself from Update must not delete the user object while its
    // method is still on the stack. Finish the callback, then release it.
    if(_InvocationDepth) {_StopRequested=true;return;}
    _StopRequested=false;
    _Created=false;_Initialized=false;
    _MissingObjectFields.clear();
    IScript* instance=_Instance;
    auto destroy=_Destroy;
    _Instance=nullptr;_Destroy=nullptr;
    if(!instance) return;
    try {instance->OnRelease(&_Runtime);} catch(...) {}
    try {destroy(instance);} catch(...) {}
}
void ScriptComponent::Fail(const std::string& message) {
    Stop();_Failed=true;_Error=message;
    _Runtime.NotifyScriptError(_GameObject,message);
}
void ScriptComponent::ReportInitializationFailure(const std::string& message) {
    RuntimeOperation operation(_Runtime);
    InvocationScope invocation(*this);
    Fail(message);
}
void ScriptComponent::SetEnabled(bool enabled) {
    if(_Enabled==enabled) return;
    RuntimeOperation operation(_Runtime);
    _Enabled=enabled;_Failed=false;_Error.clear();
    if(!enabled) Stop();
    else _Runtime.QueueScriptInitialization(*this);
    if(auto* node=_Runtime.Find(_GameObject)) _Runtime.Notify(*node,SceneNodeEventType::ComponentChanged,GetTypeName());
}
bool ScriptComponent::SetField(const std::string& id,const ScriptValue& value,std::string& error) {
    if(id.empty()||!Valid(value)) {error="Invalid script field value";return false;}
    const ScriptFieldRegistration* bridge=nullptr;
    if(const auto* type=_Runtime.FindScriptType(_TypeID)) {
        for(const auto& field:type->Fields) if(field.Info.ID==id) {bridge=&field;break;}
        if(!bridge||bridge->Info.Default.Kind!=value.Kind) {error="Unknown script field or mismatched value kind: "+id;return false;}
    }
    RuntimeOperation operation(_Runtime);
    InvocationScope invocation(*this);
    try {if(_Instance&&bridge&&!ApplyNativeField(*bridge,value,error)) return false;}
    catch(const std::exception& exception) {error=exception.what();return false;}
    catch(...) {error="Script field setter threw an exception";return false;}
    _Fields[id]=value;_Failed=false;_Error.clear();error.clear();
    if(!IsInitialized()) _Runtime.QueueScriptInitialization(*this);
    if(auto* node=_Runtime.Find(_GameObject)) _Runtime.Notify(*node,SceneNodeEventType::ComponentChanged,GetTypeName());
    return true;
}
IScript* ScriptComponent::GetNativeInstance() const {
    return _Runtime.IsScriptExecutionEnabled()&&!_Failed&&IsAttached()?_Instance:nullptr;
}
IScript* ScriptComponent::PrepareInstance() {
    if(!_Enabled||!_Runtime.IsScriptExecutionEnabled()||_Failed||!IsAttached()) return nullptr;
    if(_Starting) return nullptr;
    if(_Instance) return _StopRequested?nullptr:_Instance;
    const auto* type=_Runtime.FindScriptType(_TypeID);
    if(!type) return nullptr;
    InvocationScope invocation(*this);
    _Starting=true;
    struct StartingScope {
        bool& Starting;
        ~StartingScope() {Starting=false;}
    } starting{_Starting};
    try {
        _Instance=type->Create(_GameObject);_Destroy=type->Destroy;
        if(!_Instance) {Fail("Native script factory returned null: "+_TypeID);return nullptr;}
        if(_Instance->GetGameObject()!=_GameObject) {Fail("Native script factory returned an instance for another object");return nullptr;}
        if(!type->CppType||typeid(*_Instance)!=*type->CppType) {Fail("Native script factory returned a different C++ type");return nullptr;}
        for(const auto& value:_Fields) {
            const auto bridge=std::find_if(type->Fields.begin(),type->Fields.end(),[&](const auto& field){return field.Info.ID==value.first;});
            std::string error;
            if(bridge==type->Fields.end()||!ApplyNativeField(*bridge,value.second,error)) {
                Fail("Cannot restore script field "+value.first+": "+error);return nullptr;
            }
        }
        if(!IsAttached()||!_Enabled||_StopRequested) return nullptr;
        return IsAttached()&&_Enabled&&!_StopRequested?_Instance:nullptr;
    } catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script callback threw an exception: "+_TypeID);}
    return nullptr;
}
bool ScriptComponent::InvokeCreate() {
    if(!_Instance||_Created||!_Enabled||!IsAttached()) return !_Failed;
    InvocationScope invocation(*this);
    _Created=true;
    try {_Instance->OnCreate(&_Runtime);return !_Failed;}
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script OnCreate threw an exception: "+_TypeID);}
    return false;
}
bool ScriptComponent::InvokeInitialized() {
    if(!_Instance||_Initialized||!_Created||!_Enabled||!IsAttached()||
       !_Runtime.IsInScene(_Runtime.Find(_GameObject))) return !_Failed;
    InvocationScope invocation(*this);
    _Initialized=true;
    try {_Instance->OnInitialized(&_Runtime);return !_Failed;}
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script OnInitialized threw an exception: "+_TypeID);}
    return false;
}
IScript* ScriptComponent::EnsureStarted() {
    if(!PrepareInstance()) return nullptr;
    // During session startup Get/AddComponent may retrieve a prepared peer, but
    // cannot recursively run its callbacks ahead of the global startup phases.
    if(_Runtime.IsStartingScripts()) return _Instance;
    // Return a configurable native instance. Initialization belongs to the
    // assembly scheduler, never to an individual Add/GetComponent call.
    const auto* node=_Runtime.Find(_GameObject);
    if(node&&node->PendingAttach) return _Instance;
    if(!InvokeCreate()||!_Instance||!_Enabled||!IsAttached()) return nullptr;
    return _StopRequested?nullptr:_Instance;
}
void ScriptComponent::Update(IGameSproutRuntime*,float delta) {
    if(delta<=0) return;
    auto* instance=IsInitialized()&&_Enabled&&IsAttached()&&_Runtime.IsInScene(_Runtime.Find(_GameObject))?_Instance:nullptr;
    if(!instance) return;
    InvocationScope invocation(*this);
    try {instance->Update(&_Runtime,delta);}
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script Update threw an exception: "+_TypeID);}
}
void ScriptComponent::FixedUpdate(IGameSproutRuntime*,float delta) {
    if(delta<=0) return;
    auto* instance=IsInitialized()&&_Enabled&&IsAttached()&&_Runtime.IsInScene(_Runtime.Find(_GameObject))?_Instance:nullptr;
    if(!instance) return;
    InvocationScope invocation(*this);
    try {instance->FixedUpdate(&_Runtime,delta);}
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script FixedUpdate threw an exception: "+_TypeID);}
}
}
