#include "ScriptComponent.h"
#include "../GameObject.h"
#include "../Api/RuntimeScriptFieldValues.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace GameSproutRuntime {
ScriptComponent::ScriptComponent(Runtime& runtime,IGameObject* owner,std::string type,ResourceReference source,std::uint64_t id)
    :IScriptComponent(owner),_Runtime(runtime),_TypeID(std::move(type)),_Source(std::move(source)),_InstanceID(id) {BindDefaults();}
ScriptComponent::~ScriptComponent() {Stop();}
ScriptComponent::InvocationScope::InvocationScope(ScriptComponent& owner,bool captureChanges)
    :Owner(owner),Previous(owner._Runtime.ExchangeInvokingScript(&owner)),CaptureChanges(captureChanges) {++Owner._InvocationDepth;}
ScriptComponent::InvocationScope::~InvocationScope() {
    Owner._Runtime.ExchangeInvokingScript(Previous);
    if(--Owner._InvocationDepth==0) {
        if(Owner._StopRequested||!Owner._Enabled||!Owner.IsAttached())Owner.Stop();
        if(CaptureChanges)Owner._Runtime.MarkNativeScriptPropertiesChanged();
    }
}
bool ScriptComponent::IsScriptTypeAvailable() const {return _Runtime.FindScriptType(_TypeID)!=nullptr;}
void ScriptComponent::BindDefaults() {
    if(!_Runtime.IsScriptExecutionEnabled()) {
        const auto clearTransient=[](auto&& self,ScriptValue& value)->void {
            if(value.Kind==ScriptValueKind::SceneObject&&value.SceneObject.RuntimeObjectID)value.SceneObject={};
            if(value.Kind==ScriptValueKind::Component&&value.Component.RuntimeComponentID)value.Component={};
            for(auto& [id,child]:value.Fields)self(self,child);
            for(auto& child:value.Elements)self(self,child);
            for(auto& item:value.Entries){self(self,item.first);self(self,item.second);}
        };
        for(auto& [id,value]:_Fields)clearTransient(clearTransient,value);
    }
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
    if(_Stopping)return;
    if(_InvocationDepth) {_StopRequested=true;return;}
    _Stopping=true;
    struct StopScope {bool& Value;~StopScope(){Value=false;}} stopping{_Stopping};
    try {_Runtime.InvalidateNativeScriptReferences(this);}catch(...) {}
    _StopRequested=false;
    _Created=false;_Initialized=false;_FieldsApplied=false;
    _MissingReferences.clear();
    IScript* instance=_Instance;
    auto destroy=_Destroy;
    _Instance=nullptr;_Destroy=nullptr;
    if(!instance) {
        _Runtime.DropPhysicsSubscriptions(_GameObject->GetInstanceID(),GetComponentInstanceID());
        _Runtime.DropUISubscriptions(_GameObject->GetInstanceID(),GetComponentInstanceID());
        return;
    }
    ScriptComponent* previous=_Runtime.ExchangeInvokingScript(this);
    try {instance->OnRelease(&_Runtime);} catch(...) {}
    _Runtime.ExchangeInvokingScript(previous);
    _Runtime.DropPhysicsSubscriptions(_GameObject->GetInstanceID(),GetComponentInstanceID());
    _Runtime.DropUISubscriptions(_GameObject->GetInstanceID(),GetComponentInstanceID());
    try {destroy(instance);} catch(...) {}
    _Runtime.MarkNativeScriptPropertiesChanged();
}
void ScriptComponent::InvokeExternal(const std::function<void()>& callback) {
    if(!callback || !IsInitialized() || !_Enabled || !IsAttached()) return;
    RuntimeOperation operation(_Runtime);
    InvocationScope invocation(*this);
    try { callback(); }
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native event callback threw an exception");}
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
    if(id.empty()||!ValidateScriptValue(value,error)) {error="Invalid script field value";return false;}
    const ScriptFieldRegistration* bridge=nullptr;
    if(const auto* type=_Runtime.FindScriptType(_TypeID)) {
        for(const auto& field:type->Fields) if(field.Info.ID==id) {bridge=&field;break;}
        if(!bridge||bridge->Info.Default.Kind!=value.Kind) {error="Unknown script field or mismatched value kind: "+id;return false;}
        if(!ValidateScriptFieldValue(bridge->Info,value,error)||!ValidateReferences(bridge->Info,value,error)||!bridge->Validate(value,ReferenceContext(),error))return false;
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
bool ScriptComponent::ReplaceFields(const std::map<std::string,ScriptValue>& values,std::string& error) {
    error.clear();
    if(_Instance||_InvocationDepth) {error="Replace authored script fields before native execution starts";return false;}
    const auto* type=_Runtime.FindScriptType(_TypeID);
    for(const auto& value:values) {
        if(value.first.empty()||!ValidateScriptValue(value.second,error,true)){error="Invalid authored script field";return false;}
        if(type) {
            const auto bridge=std::find_if(type->Fields.begin(),type->Fields.end(),[&](const auto& field){return field.Info.ID==value.first;});
            if(bridge==type->Fields.end()||bridge->Info.Default.Kind!=value.second.Kind) {
                error="Unknown script field or mismatched value kind: "+value.first;return false;
            }
            if(!ValidateScriptFieldValue(bridge->Info,value.second,error)||!ValidateReferences(bridge->Info,value.second,error)||!bridge->Validate(value.second,ReferenceContext(),error))return false;
        }
    }
    try {
        auto next=values;
        RuntimeOperation operation(_Runtime);
        _Runtime.QueueScriptInitialization(*this);
        if(auto* node=_Runtime.Find(_GameObject))_Runtime.Notify(*node,SceneNodeEventType::ComponentChanged,GetTypeName());
        _Fields.swap(next);_MissingReferences.clear();_Failed=false;_Error.clear();
        return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
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
        _Instance=type->Create(_GameObject,&_Runtime);_Destroy=type->Destroy;
        if(!_Instance) {Fail("Native script factory returned null: "+_TypeID);return nullptr;}
        if(_Instance->GetGameObject()!=_GameObject) {Fail("Native script factory returned an instance for another object");return nullptr;}
        if(!type->CppType||typeid(*_Instance)!=*type->CppType) {Fail("Native script factory returned a different C++ type");return nullptr;}
        _Instance->_ComponentInstanceID=GetComponentInstanceID();
        if(!IsAttached()||!_Enabled||_StopRequested) return nullptr;
        return IsAttached()&&_Enabled&&!_StopRequested?_Instance:nullptr;
    } catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script callback threw an exception: "+_TypeID);}
    return nullptr;
}
bool ScriptComponent::ApplyPreparedFields() {
    if(_FieldsApplied)return !_Failed;
    if(!_Instance||_Failed||_StopRequested)return false;
    const auto* type=_Runtime.FindScriptType(_TypeID);if(!type)return false;
    InvocationScope invocation(*this);
    try {
        for(const auto& value:_Fields) {
            const auto bridge=std::find_if(type->Fields.begin(),type->Fields.end(),[&](const auto& field){return field.Info.ID==value.first;});
            std::string error;
            if(bridge==type->Fields.end()||!ApplyNativeField(*bridge,value.second,error)) {
                Fail("Cannot restore script field "+value.first+": "+error);return false;
            }
        }
        _FieldsApplied=true;return true;
    }catch(const std::exception& exception){Fail(exception.what());}
    catch(...){Fail("Cannot restore native script fields");}
    return false;
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
    if(!ApplyPreparedFields())return nullptr;
    // Return a configurable native instance. Initialization belongs to the
    // assembly scheduler, never to an individual Add/GetComponent call.
    const auto* node=_Runtime.Find(_GameObject);
    if(node&&node->PendingAttach) return _Instance;
    if(!InvokeCreate()||!_Instance||!_Enabled||!IsAttached()) return nullptr;
    return _StopRequested?nullptr:_Instance;
}
void ScriptComponent::EarlyUpdate(IGameSproutRuntime*,float delta) {
    if(delta<=0) return;
    auto* instance=IsInitialized()&&_Enabled&&IsAttached()&&_Runtime.IsInScene(_Runtime.Find(_GameObject))?_Instance:nullptr;
    if(!instance) return;
    InvocationScope invocation(*this);
    try {instance->EarlyUpdate(&_Runtime,delta);}
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script EarlyUpdate threw an exception: "+_TypeID);}
}
void ScriptComponent::LateUpdate(IGameSproutRuntime*,float delta) {
    if(delta<=0) return;
    auto* instance=IsInitialized()&&_Enabled&&IsAttached()&&_Runtime.IsInScene(_Runtime.Find(_GameObject))?_Instance:nullptr;
    if(!instance) return;
    InvocationScope invocation(*this);
    try {instance->LateUpdate(&_Runtime,delta);}
    catch(const std::exception& exception) {Fail(exception.what());}
    catch(...) {Fail("Native script LateUpdate threw an exception: "+_TypeID);}
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
