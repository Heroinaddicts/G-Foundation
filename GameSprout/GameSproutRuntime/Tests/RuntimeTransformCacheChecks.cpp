#include "GameSproutRuntime.h"
#include "Components/TransformComponent.h"
#include "RuntimeMath.h"
#include "Api/Internal/RuntimeScriptBindings.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const std::string& error){if(!condition)throw std::runtime_error(error);}
void Matrix(const Mat4& actual,const Mat4& expected,const char* message) {
    for(unsigned index=0;index<16;++index)
        Check(std::fabs(actual.Data[index]-expected.Data[index])<.0003f,message);
}
Mat4 Translate(float x,float y=0,float z=0){Mat4 matrix;matrix(0,3)=x;matrix(1,3)=y;matrix(2,3)=z;return matrix;}
void Position(const Mat4& matrix,float x,const char* message){Check(std::fabs(matrix(0,3)-x)<.0003f,message);}

void Transforms(Runtime& runtime) {
    auto* scene=runtime.CreateScene("Cached hierarchy");
    auto* first=scene->CreateGameObject("First");auto* second=scene->CreateGameObject("Second");
    auto* child=scene->CreateGameObject("Child",first);auto* leaf=scene->CreateGameObject("Leaf",child);
    first->GetTransform()->SetLocalPosition({10,0,0});second->GetTransform()->SetLocalPosition({-5,0,0});
    child->GetTransform()->SetLocalPosition({2,0,0});leaf->GetTransform()->SetLocalPosition({3,0,0});
    Position(leaf->GetTransform()->GetWorldMatrix(),15,"Initial ancestor chain is incorrect");
    const auto epoch=runtime.GetTransformEpoch();
    for(unsigned index=0;index<100;++index)Position(leaf->GetTransform()->GetWorldMatrix(),15,"Repeated world query changed data");
    first->GetTransform()->SetLocalPosition({10,0,0});
    Check(runtime.GetTransformEpoch()==epoch,"No-op authored values invalidated matrices");
    first->GetTransform()->SetLocalPosition({20,0,0});
    Position(leaf->GetTransform()->GetWorldMatrix(),25,"Ancestor translation retained cached descendants");
    child->GetTransform()->SetLocalPosition({4,0,0});
    Position(leaf->GetTransform()->GetWorldMatrix(),27,"Local translation retained cached descendants");
    first->GetTransform()->SetLocalScale({-2,3,.5f});
    Check(first->GetTransform()->SetLocalRotation({0,0,.70710678f,.70710678f}),"Cannot rotate parent");
    Matrix(leaf->GetTransform()->GetWorldMatrix(),first->GetTransform()->GetLocalMatrix()*
        child->GetTransform()->GetLocalMatrix()*leaf->GetTransform()->GetLocalMatrix(),
        "Rotated/nonuniform/reflected ancestor cache is incorrect");
    first->GetTransform()->SetLocalScale({1,1,1});first->GetTransform()->SetLocalRotation({});
    Check(child->SetParent(second,false),"Cannot reparent cached subtree");
    Position(leaf->GetTransform()->GetWorldMatrix(),2,"Reparent without keep-world retained old parent");
    const auto before=leaf->GetTransform()->GetWorldMatrix();
    Check(child->SetParent(first,true),"Cannot preserve world transform during reparent");
    Matrix(leaf->GetTransform()->GetWorldMatrix(),before,"Keep-world reparent changed cached descendants");
    const auto old=leaf->GetTransform()->GetWorldMatrix();
    first->ForeachChild([&](IGameObject*) {
        Check(child->SetParent(second,false),"Deferred reparent rejected");
        Matrix(leaf->GetTransform()->GetWorldMatrix(),old,"Pending reparent invalidated uncommitted hierarchy");
    });
    Position(leaf->GetTransform()->GetWorldMatrix(),-23,"Deferred reparent commit retained old world cache");
    Check(child->SetParent(nullptr,false),"Cannot detach cached subtree");
    Position(leaf->GetTransform()->GetWorldMatrix(),-18,"Detached subtree retained old scene parent");
    Check(first->AddChild(child,false),"Cannot reattach cached subtree");
    Position(leaf->GetTransform()->GetWorldMatrix(),2,"Reattached subtree retained detached cache");

    auto* access=dynamic_cast<TransformComponent*>(leaf->GetTransform());Check(access,"Missing native Transform");
    const auto authored=access->GetLocalMatrix();
    runtime.ProcessPendingEvents();
    const auto authoredRevision=scene->GetAuthoringRevision();
    Check(access->SetEvaluatedPose(11,Translate(7),{}),"Cannot apply evaluated animation pose");
    Position(access->GetLocalMatrix(),7,"Animation retained authored local matrix");
    Matrix(access->GetWorldMatrix(),child->GetTransform()->GetWorldMatrix()*Translate(7),"Animation did not invalidate world cache");
    const auto evaluatedEpoch=runtime.GetTransformEpoch();
    Check(access->SetEvaluatedPose(11,Translate(7),{0,0,0,-2}),"Evaluated quaternion normalization failed");
    Quaternion effective;
    Check(access->GetCurrentLocalRotation(effective)&&effective.X==0&&effective.Y==0&&effective.Z==0&&effective.W==1&&
          runtime.GetTransformEpoch()==evaluatedEpoch,"Equivalent evaluated quaternion changed canonical rotation or invalidated the cache");
    Check(!access->SetEvaluatedPose(11,Translate(99),{0,0,0,0}),"Zero evaluated quaternion was accepted");
    Position(access->GetLocalMatrix(),7,"Rejected evaluated quaternion changed the matrix");
    Check(access->SetPhysicsPose(Translate(9),false),"Cannot apply kinematic physics pose");
    Position(access->GetLocalMatrix(),7,"Kinematic pose incorrectly displaced active animation pose");
    Check(access->SetPhysicsPose(Translate(11),true),"Cannot apply dynamic physics pose");
    Position(access->GetLocalMatrix(),11,"Dynamic physics did not override cached animation pose");
    Check(!access->SetEvaluatedPose(11,Translate(12),{}),"Animation overrode dynamic physics");
    access->ClearPhysicsPose();Position(access->GetLocalMatrix(),7,"Physics reset did not restore evaluated animation");
    access->ClearEvaluatedPose(999);Position(access->GetLocalMatrix(),7,"Wrong animator cleared another owner's pose");
    access->ClearEvaluatedPose(11);Matrix(access->GetLocalMatrix(),authored,"Animation reset did not restore authored local matrix");
    Check(scene->GetAuthoringRevision()==authoredRevision,"Evaluated animation/physics poses changed authored document revision");
    Check(access->SetPhysicsPose(Translate(6),true),"Cannot reapply physics pose");access->GetWorldMatrix();
    access->SetLocalPosition({4,0,0});Position(access->GetLocalMatrix(),4,"Authoring after physics retained physics cache");
    std::vector<SceneAuthoringChange> authoredChanges;
    Check(scene->GetAuthoringRevision()>authoredRevision&&scene->GetAuthoringChanges(authoredRevision,authoredChanges)&&
          authoredChanges.size()==1&&authoredChanges[0].ObjectID==leaf->GetInstanceID(),
          "Authored changes were not journaled synchronously before queued listener dispatch");
    Check(access->SetLocalMatrix(Translate(8,2,3)),"Cannot set complete local matrix");
    Matrix(access->GetWorldMatrix(),child->GetTransform()->GetWorldMatrix()*Translate(8,2,3),"Complete matrix replacement retained world cache");
    Mat4 invalid;invalid(0,1)=.25f;
    const auto accepted=access->GetWorldMatrix();Check(!access->SetLocalMatrix(invalid),"Sheared local matrix accepted");
    Matrix(access->GetWorldMatrix(),accepted,"Rejected matrix altered cached pose");

    auto* detached=runtime.CreateGameObject("Detached root");auto* detachedChild=runtime.CreateGameObject("Detached child",detached);
    detached->GetTransform()->SetLocalPosition({2,0,0});detachedChild->GetTransform()->SetLocalPosition({1,0,0});
    Position(detachedChild->GetTransform()->GetWorldMatrix(),3,"Detached hierarchy did not resolve matrices");
    Check(second->AddChild(detached,false),"Cannot attach detached hierarchy");
    Position(detachedChild->GetTransform()->GetWorldMatrix(),-2,"Detached attachment retained old world matrix");
    auto* deepest=scene->CreateGameObject("Deep root");
    for(unsigned index=0;index<512;++index){deepest=scene->CreateGameObject("Child",deepest);deepest->GetTransform()->SetLocalPosition({1,0,0});}
    Position(deepest->GetTransform()->GetWorldMatrix(),512,"Deep iterative resolution failed");
    Position(deepest->GetTransform()->GetWorldMatrix(),512,"Deep warmed cache changed values");
    Check(runtime.DestroyScene(scene),"Cannot clean cached transform scene");
}

unsigned Initialized=0,Updated=0,Fixed=0;
class TickScript final:public ScriptBehaviour {
public:
    explicit TickScript(IGameSproutRuntime*) {}
protected:
    void OnInitialized(IGameSproutRuntime*)override{++Initialized;}
    void Update(IGameSproutRuntime*,float)override{++Updated;}
    void FixedUpdate(IGameSproutRuntime*,float)override{++Fixed;}
};
class Observer final:public IRuntimePropertyObserver {
public: unsigned Calls=0;
    void OnRuntimeObjectPropertiesChanged(std::uint64_t,std::uint64_t)override{++Calls;}
};
class VisualEvents final:public IRuntimeListener {
public:
    unsigned Nodes=0,Visuals=0;
    Runtime* Owner=nullptr;bool Unsubscribe=false;
    void OnSceneNodeEvent(const SceneNodeEvent&)override{++Nodes;}
    void OnSceneVisualChanged(std::uint64_t)override{++Visuals;if(Unsubscribe)Owner->UnregisterListener(this);}
};
void VisualAuthoringBoundary(Runtime& runtime) {
    auto* scene=runtime.CreateScene("Visual/authored boundary");auto* object=scene->CreateGameObject("Posed node");
    runtime.ProcessPendingEvents();
    VisualEvents listener;listener.Owner=&runtime;Check(runtime.RegisterListener(&listener),"Cannot register visual fixture");
    const auto revision=scene->GetAuthoringRevision();
    auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform());
    Check(transform&&transform->SetEvaluatedPose(17,Translate(2),{})&&transform->SetEvaluatedPose(17,Translate(3),{}),"Cannot evaluate visual fixture");
    Check(scene->GetAuthoringRevision()==revision,"Visual poses advanced authored revision");
    runtime.ProcessPendingEvents();
    Check(listener.Visuals==1&&listener.Nodes==0,"Evaluated poses were not coalesced to one visual-only scene event");
    runtime.ProcessPendingEvents();Check(listener.Visuals==1,"Idle event dispatch repeated a visual event");
    transform->SetLocalPosition({4,0,0});std::vector<SceneAuthoringChange> changes;
    Check(scene->GetAuthoringRevision()>revision&&scene->GetAuthoringChanges(revision,changes)&&changes.size()==1,
          "Direct setter authored revision waited for event dispatch");
    listener.Unsubscribe=true;runtime.ProcessPendingEvents();
    Check(listener.Nodes==1&&listener.Visuals==2,"Authored/visual delivery or visual self-unsubscribe failed");
    transform->SetLocalPosition({5,0,0});runtime.ProcessPendingEvents();
    Check(listener.Nodes==1&&listener.Visuals==2,"Visual self-unsubscribe retained the listener");
    Check(runtime.DestroyScene(scene),"Cannot clean visual fixture");
}
void SuspendedScenes(Runtime& runtime) {
    Internal::RegisterScriptClass<TickScript>("tick-cache-fixture","TickScript",__FILE__,{"fixture",1},{});
    std::string error;Check(runtime.RegisterScriptType(Internal::ScriptModuleTypes().back(),error),error);
    auto* scene=runtime.CreateScene("Suspended scene");scene->SetSimulationEnabled(false);
    auto* object=scene->CreateGameObject("Observed");
    Check(runtime.AddScriptComponent(object,"tick-cache-fixture",{"fixture",1},0,error),error);
    Check(runtime.BeginExecution(error),error);Check(Initialized==1,"Suspended scene skipped initialization");
    Observer observer;Check(runtime.RegisterObjectPropertyObserver(object->GetInstanceID(),&observer),"Cannot observe suspended scene object");
    object->GetTransform()->SetLocalPosition({2,0,0});
    runtime.Update(0);runtime.FixedUpdate(.1f);
    Check(Updated==0&&Fixed==0,"Suspended scene ticked components");
    Check(observer.Calls>0,"Suspended update stopped dispatching property events");
    scene->SetSimulationEnabled(true);runtime.Update(.1f);runtime.FixedUpdate(.1f);
    Check(Updated==1&&Fixed==1,"Enabling scene did not resume variable/fixed callbacks");
    scene->SetSimulationEnabled(false);runtime.Update(.1f);runtime.FixedUpdate(.1f);
    Check(Updated==1&&Fixed==1,"Disabling scene retained active tick traversal");
    runtime.UnregisterObjectPropertyObserver(object->GetInstanceID(),&observer);
    Check(runtime.EndExecution(error),error);Check(runtime.DestroyScene(scene),"Cannot clean suspended scene");
}
}
int main() {
    try{Runtime runtime;Transforms(runtime);VisualAuthoringBoundary(runtime);SuspendedScenes(runtime);
        std::cout<<"PASS transform cache: ancestors, reparent/deferred/detached/deep trees, authored/animation/physics precedence, suspension preserves initialization/events and resumes ticks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
