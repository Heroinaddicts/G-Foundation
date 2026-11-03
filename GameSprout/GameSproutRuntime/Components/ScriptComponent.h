#pragma once
#include "../GameSproutRuntime.h"
#include <set>

namespace GameSproutRuntime {
class ScriptComponent final : public IScriptComponent {
public:
    ScriptComponent(Runtime&,IGameObject*,std::string,ResourceReference,std::uint64_t);
    ~ScriptComponent() override;
    const std::type_info& GetComponentType() const override {return typeid(IScriptComponent);}
    const char* GetTypeName() const override {return "GameSproutRuntime.ScriptComponent";}
    std::uint64_t GetScriptInstanceID() const override {return _InstanceID;}
    const std::string& GetScriptTypeID() const override {return _TypeID;}
    ResourceReference GetScriptSource() const override {return _Source;}
    bool IsScriptTypeAvailable() const override;
    bool IsEnabled() const override {return _Enabled;}
    void SetEnabled(bool) override;
    const std::map<std::string,ScriptValue>& GetFields() const override {return _Fields;}
    bool ReadFields(std::map<std::string,ScriptValue>&,std::string&) const override;
    bool SetField(const std::string&,const ScriptValue&,std::string&) override;
    bool ReplaceFields(const std::map<std::string,ScriptValue>&,std::string&) override;
    bool RestoreSerializedFields(const std::map<std::string,ScriptValue>&,std::string&) override;
    const std::string& GetScriptError() const override {return _Error;}
    IScript* GetNativeInstance() const;
    IScript* PeekNativeInstance() const {return _Instance;}
    IScript* EnsureStarted();
    IScript* PrepareInstance();
    bool ApplyPreparedFields();
    bool InvokeCreate();
    bool InvokeInitialized();
    bool HasCreatedInstance() const {return _Created && _Instance && !_Failed && !_StopRequested;}
    bool IsInitialized() const {return _Initialized && _Instance && !_Failed && !_StopRequested;}
    bool HasFailed() const {return _Failed;}
    void ReportInitializationFailure(const std::string& message);
    void EarlyUpdate(IGameSproutRuntime*,float) override;
    void Update(IGameSproutRuntime*,float) override;
    void LateUpdate(IGameSproutRuntime*,float) override;
    void FixedUpdate(IGameSproutRuntime*,float) override;
    void OnRelease(IGameSproutRuntime*) override {Stop();}
    void Stop() noexcept;
    // External subscriptions execute under the same lifetime barrier as Update.
    void InvokeExternal(const std::function<void()>& callback);
    void BindDefaults();
    void ReplaceAuthoredFields(std::map<std::string,ScriptValue>& fields) noexcept;
    bool InvalidateObjectReference(IGameObject* object);
    bool InvalidateComponentReference(IComponent* component);
    bool InvalidateNativeInstanceReference(ScriptComponent* component);
    bool RebindMissingObjectReferences();
private:
    struct InvocationScope {
        ScriptComponent& Owner;
        ScriptComponent* Previous;
        bool CaptureChanges;
        explicit InvocationScope(ScriptComponent& owner,bool captureChanges=true);
        ~InvocationScope();
    };
    bool IsAttached() const;
    void Fail(const std::string&);
    ScriptReferenceContext ReferenceContext() const;
    bool ValidateReferences(const ScriptFieldInfo&,const ScriptValue&,std::string&) const;
    bool InvalidateReferences(IGameObject*,IComponent*,bool nativeOnly=false);
    bool ApplyNativeField(const ScriptFieldRegistration&,const ScriptValue&,std::string&);
    Runtime& _Runtime;
    std::string _TypeID;
    ResourceReference _Source;
    std::uint64_t _InstanceID;
    std::map<std::string,ScriptValue> _Fields;
    std::map<std::string,std::map<std::string,ScriptValue>> _MissingReferences;
    std::string _Error;
    IScript* _Instance=nullptr;
    void (*_Destroy)(IScript*)=nullptr;
    std::size_t _InvocationDepth=0;
    bool _Enabled=true, _Failed=false, _StopRequested=false, _Starting=false;
    bool _Created=false, _Initialized=false, _FieldsApplied=false, _Stopping=false;
};
}
