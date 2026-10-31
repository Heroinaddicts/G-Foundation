#include "GameSproutRuntimeExport.h"
#include <cmath>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <vector>

class Recorder final : public GameSproutRuntime::IRuntimeListener {
public:
    std::vector<GameSproutRuntime::SceneNodeEvent> Events;
    std::function<void(const GameSproutRuntime::SceneNodeEvent&)> DuringEvent;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        Events.push_back(event);
        if (DuringEvent) DuringEvent(event);
    }
};

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static bool Near(float a, float b) { return std::fabs(a - b) < 0.0002f; }

static void CheckVector(GameSproutRuntime::Vector3 a, GameSproutRuntime::Vector3 b, const char* message) {
    Check(Near(a.X,b.X) && Near(a.Y,b.Y) && Near(a.Z,b.Z), message);
}

static void CheckMatrix(const GameSproutRuntime::Mat4& a, const GameSproutRuntime::Mat4& b, const char* message) {
    for (std::size_t i = 0; i < 16; ++i) Check(Near(a.Data[i], b.Data[i]), message);
}

static GameSproutRuntime::Vector3 Translation(const GameSproutRuntime::Mat4& matrix) {
    return {matrix(0,3), matrix(1,3), matrix(2,3)};
}

static void CheckComponents(GameSproutRuntime::IGameObject* object) {
    auto* transform = object->GetTransform();
    Check(transform && transform->GetGameObject() == object, "Object has no owned Transform");
    Check(transform == object->GetComponent<GameSproutRuntime::ITransformComponent>() &&
          transform == object->AddComponent<GameSproutRuntime::ITransformComponent>(), "Transform interface key mismatch");
    Check(!object->RemoveComponent<GameSproutRuntime::ITransformComponent>(), "Intrinsic Transform was removed");
    auto* image = object->AddComponent<GameSproutRuntime::IImageComponent>();
    Check(image && image == object->AddComponent<GameSproutRuntime::IImageComponent>(), "Image not created or deduplicated");
    Check(image->GetGameObject() == object && image->GetComponentType() == typeid(GameSproutRuntime::IImageComponent),
          "Factory exposed implementation type or lost owner");
    Check(std::string(image->GetTypeName()) == "GameSproutRuntime.Image", "Image has unstable serialized type name");
    const GameSproutRuntime::ResourceReference texture{"0123456789abcdef0123456789abcdef", 5};
    image->SetTexture(texture);
    Check(image->GetTexture() == texture, "Image resource reference changed");
    image->SetColor({0.2f,0.3f,0.4f,0.5f});
    Check(Near(image->GetColor().A,0.5f), "Image color is not instance data");
    image->SetUVRect({{0.25f,0.5f},{0.5f,0.25f}});
    Check(Near(image->GetUVRect().Position.X,0.25f), "Image UV rect not retained");

    auto* rect = object->AddComponent<GameSproutRuntime::IRectTransformComponent>();
    Check(rect != nullptr, "RectTransform factory missing");
    rect->SetAnchors({0,0},{1,1});
    rect->SetPivot({0.25f,0.75f});
    rect->SetAnchoredPosition({20,30});
    rect->SetSizeDelta({100,200});
    Check(Near(rect->GetPivot().Y,0.75f) && Near(rect->GetSizeDelta().X,100), "UI layout data not retained");

    auto* canvas = object->AddComponent<GameSproutRuntime::ICanvasComponent>();
    Check(canvas && Near(canvas->GetPixelsPerUnit(),100), "Canvas defaults/factory missing");
    Check(!canvas->SetPixelsPerUnit(0) && !canvas->SetReferenceResolution({0,1}), "Invalid Canvas dimensions accepted");
    Check(canvas->SetPixelsPerUnit(50) && canvas->SetReferenceResolution({800,600}), "Valid Canvas settings rejected");
    canvas->SetRenderMode(GameSproutRuntime::CanvasRenderMode::WorldSpace);
    Check(canvas->GetRenderMode() == GameSproutRuntime::CanvasRenderMode::WorldSpace, "Canvas mode did not change");

    auto* mesh = object->AddComponent<GameSproutRuntime::IMeshComponent>();
    auto* renderer = object->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    Check(mesh && renderer, "Mesh/renderer factory missing");
    mesh->SetMesh(texture);
    Check(mesh->GetMesh() == texture, "Mesh identity not retained");
    CheckVector(mesh->GetLocalBounds().Extents,{},"Unloaded mesh fabricated geometry bounds");
    Check(renderer->GetMaterialCount() == 0 && renderer->SetMaterial(0,texture), "First material slot cannot be appended");
    Check(!renderer->SetMaterial(2,texture) && renderer->GetMaterialCount() == 1, "Sparse material slot accepted");
    Check(renderer->SetMaterial(1,texture) && renderer->GetMaterial(1) == texture, "Material slot append failed");
    Check(renderer->SetMaterials({texture}) && renderer->GetMaterialCount()==1, "Material array cannot shrink");
    const std::vector<GameSproutRuntime::ResourceReference> oversized(1025,texture);
    Check(!renderer->SetMaterials(oversized) && renderer->GetMaterialCount()==1, "Oversized material array changed state");
    Check(renderer->SetMaterials({}) && renderer->GetMaterialCount()==0, "Material array cannot clear");
    renderer->SetVisible(false);
    Check(!renderer->IsVisible(), "Renderer visibility not retained");

    auto* camera = object->AddComponent<GameSproutRuntime::ICameraComponent>();
    Check(camera != nullptr, "Camera factory missing");
    const auto original = camera->GetSettings();
    auto invalid = original;
    invalid.NearPlane = -1;
    Check(!camera->SetSettings(invalid) && Near(camera->GetSettings().NearPlane,original.NearPlane),
          "Invalid Camera settings replaced original");
    GameSproutRuntime::Mat4 projection;
    Check(camera->GetProjectionMatrix(1.5f,projection) && !camera->GetProjectionMatrix(0,projection),
          "Camera did not validate output aspect");
    GameSproutRuntime::Mat4 view;
    Check(camera->GetViewMatrix(view), "Camera cannot invert valid world matrix");
    CheckMatrix(view * transform->GetWorldMatrix(),{},"Camera view is not inverse of world matrix");
    transform->SetLocalScale({0,1,1});
    Check(!camera->GetViewMatrix(view), "Camera accepted singular world matrix");
    transform->SetLocalScale({1,1,1});
    Check(!transform->SetLocalRotation({0,0,0,0}), "Zero quaternion accepted");
    Check(transform->SetLocalRotation({0,0,0,2}) && Near(transform->GetLocalRotation().W,1), "Quaternion not normalized");
    const auto position = transform->GetLocalPosition();
    transform->SetLocalPosition({std::numeric_limits<float>::infinity(),0,0});
    CheckVector(transform->GetLocalPosition(),position,"Nonfinite Transform position accepted");
    Check(!object->GetComponent<GameSproutRuntime::IComponent>(), "Unregistered base component interface matched accidentally");
    Check(!object->AddComponent<GameSproutRuntime::IComponent>(), "Unregistered base component interface created");
    Check(!object->GetComponentAt(object->GetComponentCount()), "Out of range component access not null");
    Check(object->RemoveComponent<GameSproutRuntime::ICameraComponent>(), "Camera fixture removal failed");

    std::size_t visited = 0;
    const std::size_t count = object->GetComponentCount();
    object->ForeachComponent([&](GameSproutRuntime::IComponent*) {
        ++visited;
        if (visited == 1) {
            Check(object->AddComponent<GameSproutRuntime::ICameraComponent>() != nullptr, "Component creation inside iteration failed");
            Check(object->RemoveComponent<GameSproutRuntime::IImageComponent>(), "Deferred component removal rejected");
            Check(object->GetComponent<GameSproutRuntime::IImageComponent>() == image, "Component removed in active iteration");
        }
    });
    Check(visited == count && !object->GetComponent<GameSproutRuntime::IImageComponent>(), "Component iteration/removal barrier failed");
    object->ForeachComponent({});
}

static void CheckHierarchy(GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* scene = runtime->CreateScene("Hierarchy");
    auto* otherScene = runtime->CreateScene("Other scene");
    Check(scene && otherScene && scene->GetScene() == scene && !scene->Parent(), "Invalid scene root");
    scene->GetTransform()->SetLocalPosition({10,20,30});
    CheckMatrix(scene->GetTransform()->GetWorldMatrix(),{},"Scene root Transform changed");
    auto* parent = scene->CreateGameObject("Parent");
    auto* child = scene->CreateGameObject("Child",parent);
    auto* grandchild = scene->CreateGameObject("Grandchild",child);
    auto* other = otherScene->CreateGameObject("Other");
    Check(parent && child && grandchild && other, "Object creation failed");
    Check(!scene->CreateGameObject("Wrong scene",other), "Cross-scene creation accepted");
    Check(parent->Parent() == scene && child->Parent() == parent && grandchild->GetScene() == scene,
          "Hierarchy parent/scene relation wrong");
    Check(parent->FindChild("Child") == child && !scene->FindChild("Grandchild"), "FindChild traversed wrong scope");
    Check(!parent->GetChild(1000), "Out of range child access not null");
    Check(!parent->AddChild(parent) && !grandchild->AddChild(parent) && !parent->AddChild(scene) &&
          !parent->AddChild(other) && !parent->AddChild(nullptr), "Illegal hierarchy mutation accepted");
    Check(!scene->DestroyGameObject(scene) && !scene->DestroyGameObject(other) && !runtime->DestroyScene(nullptr),
          "Invalid scene/object destruction accepted");
    parent->GetTransform()->SetLocalPosition({10,0,0});
    child->GetTransform()->SetLocalPosition({1,2,3});
    CheckVector(Translation(child->GetTransform()->GetWorldMatrix()),{11,2,3},"Parent transform not propagated");
    parent->SetActive(false);
    Check(child->IsActiveSelf() && !child->IsActiveInHierarchy(), "Active state inheritance failed");
    parent->SetActive(true);
    Check(grandchild->IsActiveInHierarchy(), "Descendant active state not restored");

    auto* target = scene->CreateGameObject("Target");
    target->GetTransform()->SetLocalPosition({-4,7,0});
    const auto worldBefore = child->GetTransform()->GetWorldMatrix();
    Check(target->AddChild(child,true) && child->Parent() == target, "Valid reparent rejected");
    CheckMatrix(child->GetTransform()->GetWorldMatrix(),worldBefore,"Reparent changed world transform");
    Check(target->RemoveChild(child,true) && child->Parent() == scene, "RemoveChild did not detach to scene root");
    CheckMatrix(child->GetTransform()->GetWorldMatrix(),worldBefore,"Detach changed world transform");
    Check(!target->RemoveChild(child), "Non-child removal accepted");
    auto* singular = scene->CreateGameObject("Singular");
    singular->GetTransform()->SetLocalScale({0,1,1});
    Check(!singular->AddChild(child,true) && child->Parent() == scene, "Singular reparent mutated hierarchy");
    auto* scaled = scene->CreateGameObject("Scaled");
    scaled->GetTransform()->SetLocalScale({2,1,1});
    const float angle = GameSproutRuntime::Pi / 8;
    auto* rotated = scene->CreateGameObject("Rotated");
    Check(rotated->GetTransform()->SetLocalRotation({0,0,std::sin(angle),std::cos(angle)}), "Rotation failed");
    Check(!scaled->AddChild(rotated,true) && rotated->Parent() == scene, "Unrepresentable shear reparent accepted");
    auto* mirrored = scene->CreateGameObject("Mirrored");
    mirrored->GetTransform()->SetLocalScale({-1,2,3});
    const auto mirroredBefore = mirrored->GetTransform()->GetWorldMatrix();
    Check(target->AddChild(mirrored,true), "Representable mirrored transform reparent rejected");
    CheckMatrix(mirrored->GetTransform()->GetWorldMatrix(),mirroredBefore,"Mirrored reparent changed transform");
    CheckComponents(child);

    Check(scene->DestroyGameObject(child), "Subtree deletion failed");
    Check(!scene->FindChild("Child") && !parent->FindChild("Child"), "Deleted subtree left in tree");
    Check(runtime->DestroyScene(otherScene), "Scene deletion failed");
    Check(runtime->DestroyScene(scene), "Hierarchy scene cleanup failed");
}

static void CheckDeferredMutation(GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* scene = runtime->CreateScene("Deferred");
    auto* container = scene->CreateGameObject("Container");
    auto* a = scene->CreateGameObject("A",container);
    auto* b = scene->CreateGameObject("B",container);
    auto* c = scene->CreateGameObject("C",container);
    Check(a && b && c, "Deferred fixture creation failed");
    std::size_t visits = 0;
    container->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        ++visits;
        if (visits == 1) {
            Check(scene->DestroyGameObject(b), "Queued delete rejected");
            // 排在销毁后的同一对象操作可以立即拒绝或执行时跳过，不能解引用悬空指针。
            (void)scene->AddChild(b,false);
            Check(scene->AddChild(c,false), "Queued reparent rejected");
            Check(container->FindChild("B") == b && c->Parent() == container, "Mutation escaped iteration barrier");
        }
    });
    Check(visits == 3 && container->GetChildCount() == 1 && scene->FindChild("C") == c && !scene->FindChild("B"),
          "Queued child operations corrupted iteration");
    container->ForeachChild({});
    // Destroying the object whose children are currently visited must wait until the call returns.
    container->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        Check(scene->DestroyGameObject(container), "Self-container destroy request rejected");
        Check(container->GetChildCount() == 1, "Container destroyed during callback");
    });
    Check(!scene->FindChild("Container"), "Container not destroyed after callback");
    scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        Check(runtime->DestroyScene(scene), "Scene destroy during iteration rejected");
    });
    // scene is now invalid; only use Runtime to confirm removal.
}

static bool HasEvent(const Recorder& recorder, GameSproutRuntime::SceneNodeEventType type,
                     std::uint64_t objectID, const std::string& name = {}) {
    for (const auto& event : recorder.Events) {
        if (event.Type == type && event.ObjectID == objectID && (name.empty() || event.Name == name)) return true;
    }
    return false;
}

static void CheckEvents(GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* historical = runtime->CreateScene("Before subscription");
    Check(historical != nullptr, "Cannot create undrained pre-subscription event");
    Recorder recorder;
    Check(runtime->RegisterListener(&recorder) && !runtime->RegisterListener(&recorder), "Listener registration/dedup failed");
    Check(!runtime->RegisterListener(nullptr), "Null listener accepted");
    runtime->Update(0.016f);
    Check(recorder.Events.empty(), "Listener received events queued before registration");
    Check(runtime->DestroyScene(historical), "Historical scene cleanup failed");
    runtime->Update(0.016f);
    recorder.Events.clear();
    auto* scene = runtime->CreateScene("Events");
    auto* parent = scene->CreateGameObject("Parent");
    auto* object = scene->CreateGameObject("Before");
    const std::uint64_t sceneID = scene->GetInstanceID();
    const std::uint64_t objectID = object->GetInstanceID();
    Check(sceneID && objectID && parent->GetInstanceID() && sceneID != objectID && objectID != parent->GetInstanceID(),
          "Object IDs missing or duplicated");
    Check(runtime->QueryGameObject(sceneID) == scene && runtime->QueryGameObject(objectID) == object &&
          !runtime->QueryGameObject(0), "Runtime identity lookup failed");
    object->SetName("Renamed");
    Check(parent->AddChild(object,false), "Event reparent fixture failed");
    object->SetActive(false);
    auto* image = object->AddComponent<GameSproutRuntime::IImageComponent>();
    image->SetColor({0.1f,0.2f,0.3f,1});
    Check(recorder.Events.empty(), "Node notification dispatched before Update");
    runtime->Update(0.016f);
    Check(HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::Create,sceneID,"Events") &&
          HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::Create,objectID,"Before") &&
          HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::Rename,objectID,"Renamed") &&
          HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::ActiveChanged,objectID) &&
          HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::ComponentAdded,objectID) &&
          HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::ComponentChanged,objectID), "Expected node events missing");
    bool reparent = false;
    bool imageEvent = false;
    for (const auto& event : recorder.Events) {
        if (event.Type == GameSproutRuntime::SceneNodeEventType::Reparent && event.ObjectID == objectID) {
            reparent = event.SceneID == sceneID && event.ParentID == parent->GetInstanceID() && event.OldParentID == sceneID;
        }
        if (event.Type == GameSproutRuntime::SceneNodeEventType::ComponentAdded && event.ObjectID == objectID &&
            event.ComponentType == "GameSproutRuntime.Image") imageEvent = true;
    }
    Check(reparent && imageEvent, "Event hierarchy/interface identity snapshot incorrect");
    recorder.Events.clear();
    bool changedInCallback = false;
    recorder.DuringEvent = [&](const GameSproutRuntime::SceneNodeEvent& event) {
        if (!changedInCallback && event.ObjectID == objectID && event.Type == GameSproutRuntime::SceneNodeEventType::Rename) {
            changedInCallback = true;
            object->SetName("Callback rename");
        }
    };
    object->SetName("Trigger rename");
    runtime->Update(0.016f);
    Check(changedInCallback && !HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::Rename,objectID,"Callback rename"),
          "Callback-generated event dispatched recursively in same Update");
    runtime->Update(0.016f);
    Check(HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::Rename,objectID,"Callback rename"),
          "Callback-generated event lost at next Update");
    recorder.DuringEvent = {};

    Recorder selfRemoving;
    Check(runtime->RegisterListener(&selfRemoving), "Cannot register second listener");
    selfRemoving.DuringEvent = [&](const GameSproutRuntime::SceneNodeEvent&) { runtime->UnregisterListener(&selfRemoving); };
    object->SetName("One");
    object->SetName("Two");
    runtime->Update(0.016f);
    Check(selfRemoving.Events.size() == 1, "Unsubscribed listener received later events in same batch");
    Recorder late;
    Check(runtime->RegisterListener(&late), "Late listener registration failed");
    runtime->Update(0.016f);
    Check(late.Events.empty(), "Late listener replayed historical events");
    runtime->UnregisterListener(&late);

    recorder.Events.clear();
    Check(object->RemoveComponent<GameSproutRuntime::IImageComponent>(), "Image removal failed");
    runtime->Update(0.016f);
    Check(HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::ComponentRemoved,objectID), "Component removal event missing");
    recorder.Events.clear();
    Check(scene->DestroyGameObject(object) && !runtime->QueryGameObject(objectID), "Deleted ID still resolves");
    runtime->Update(0.016f);
    Check(HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::Destroy,objectID,"Two"), "Deletion lost standalone node snapshot");
    auto* replacement = scene->CreateGameObject("Replacement");
    Check(replacement->GetInstanceID() > objectID, "Destroyed node identity was reused");
    runtime->UnregisterListener(&recorder);
    const std::size_t eventCount = recorder.Events.size();
    Check(runtime->DestroyScene(scene), "Event fixture cleanup failed");
    runtime->Update(0.016f);
    Check(recorder.Events.size() == eventCount && !runtime->QueryGameObject(sceneID), "Unregistered listener called or deleted scene ID retained");
}

static void CheckStructuralEdges(GameSproutRuntime::IGameSproutRuntime* runtime) {
    Recorder recorder;
    Check(runtime->RegisterListener(&recorder), "Cannot register structural listener");
    auto* scene=runtime->CreateScene("Structural edges");
    auto* a=scene->CreateGameObject("A");
    auto* b=scene->CreateGameObject("B");
    const auto aID=a->GetInstanceID();
    bool requested=false;
    scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        if(requested) return;
        requested=true;
        Check(a->AddChild(b,false), "First deferred reparent rejected");
        Check(b->AddChild(a,false), "Second deferred reparent not accepted for revalidation");
    });
    Check(a->Parent()==scene && b->Parent()==a, "Deferred cycle was not rejected at commit");
    runtime->Update(0);
    Check(HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::OperationFailed,aID), "Deferred rejection was not reported");
    auto* singular=scene->CreateGameObject("Singular");
    singular->GetTransform()->SetLocalScale({0,1,1});
    auto* child=scene->CreateGameObject("Existing child",singular);
    Check(singular->AddChild(child,true), "Same-parent no-op rejected under singular transform");
    GameSproutRuntime::IGameObject* created=nullptr;
    bool once=false;
    scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        if(once) return;
        once=true;
        created=scene->CreateGameObject("Created during iteration");
        Check(created && !scene->FindChild("Created during iteration"), "Creation attachment was not deferred");
        Check(created->AddComponent<GameSproutRuntime::IImageComponent>(), "Pending object cannot create component");
    });
    Check(scene->FindChild("Created during iteration")==created, "Deferred creation did not attach");
    const auto doomedID=created->GetInstanceID();
    try {
        scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
            Check(scene->DestroyGameObject(created), "Exception cleanup delete rejected");
            throw std::runtime_error("expected callback exception");
        });
    } catch(const std::runtime_error&) {}
    Check(!runtime->QueryGameObject(doomedID), "Exception escaped iteration without flushing deletions");
    runtime->UnregisterListener(&recorder);
    Check(runtime->DestroyScene(scene), "Structural scene cleanup failed");
}

static void CheckMainCamera(GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* scene=runtime->CreateScene("Main camera");
    auto* otherScene=runtime->CreateScene("Other main camera");
    Check(scene && otherScene && !scene->GetMainCamera(), "New scene has an unexpected main camera");
    auto* object=scene->CreateGameObject("Camera");
    auto* foreign=otherScene->CreateGameObject("Foreign Camera");
    auto* camera=object->AddComponent<GameSproutRuntime::ICameraComponent>();
    auto* foreignCamera=foreign->AddComponent<GameSproutRuntime::ICameraComponent>();
    Check(camera && foreignCamera && scene->SetMainCamera(camera) && scene->GetMainCamera()==camera,
          "Cannot select a scene camera");
    Check(!scene->SetMainCamera(foreignCamera) && scene->GetMainCamera()==camera,
          "Foreign scene camera replaced the main camera");
    Check(scene->SetMainCamera(nullptr) && !scene->GetMainCamera(), "Cannot clear main camera");
    Recorder recorder;
    Check(runtime->RegisterListener(&recorder), "Main camera event listener failed");
    Check(scene->SetMainCamera(camera), "Cannot reselect main camera");
    runtime->Update(0);
    Check(HasEvent(recorder,GameSproutRuntime::SceneNodeEventType::MainCameraChanged,scene->GetInstanceID()),
          "Main camera event missing");
    Check(object->RemoveComponent<GameSproutRuntime::ICameraComponent>() && !scene->GetMainCamera(),
          "Removing Camera component left a stale main camera");
    camera=object->AddComponent<GameSproutRuntime::ICameraComponent>();
    Check(camera && scene->SetMainCamera(camera), "Cannot assign recreated Camera component");
    Check(scene->DestroyGameObject(object) && !scene->GetMainCamera(), "Deleting camera node left a stale main camera");
    auto* parent=scene->CreateGameObject("Camera parent");
    auto* child=scene->CreateGameObject("Child camera",parent);
    camera=child->AddComponent<GameSproutRuntime::ICameraComponent>();
    Check(scene->SetMainCamera(camera) && scene->DestroyGameObject(parent) && !scene->GetMainCamera(),
          "Deleting camera ancestor left a stale main camera");
    runtime->UnregisterListener(&recorder);
    Check(runtime->DestroyScene(scene) && runtime->DestroyScene(otherScene), "Main camera fixtures leaked");
}

static void CheckRotationStability(GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* scene=runtime->CreateScene("Rotation restoration");
    auto* transform=scene->CreateGameObject("Quaternion")->GetTransform();
    std::uint32_t state=1234567;
    for(int iteration=0;iteration<1000;++iteration) {
        const auto next=[&]() {state=state*1664525u+1013904223u;return static_cast<float>(static_cast<int>(state%20001)-10000)/10000.0f;};
        Check(transform->SetLocalRotation({next(),next(),next(),next()}),"Valid rotation was rejected");
        const auto before=transform->GetLocalRotation();
        Check(transform->SetLocalRotation({0,0,0,1}) && transform->SetLocalRotation(before),"Rotation restoration failed");
        const auto restored=transform->GetLocalRotation();
        Check(before.X==restored.X&&before.Y==restored.Y&&before.Z==restored.Z&&before.W==restored.W,"Getter quaternion drifted when restored");
        for(int repeat=0;repeat<5;++repeat)Check(transform->SetLocalRotation(before),"Idempotent rotation failed");
        const auto repeated=transform->GetLocalRotation();
        Check(before.X==repeated.X&&before.Y==repeated.Y&&before.Z==repeated.Z&&before.W==repeated.W,"Repeated unit quaternion writes drifted");
    }
    Check(transform->SetLocalRotation({-1,0,0,0}) && transform->GetLocalRotation().X==1,"Half-turn quaternion sign was not canonical");
    Check(runtime->DestroyScene(scene),"Rotation restoration fixture leaked");
}

static void CheckSiblingOrdering(GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* scene=runtime->CreateScene("Sibling order");
    auto* parent=scene->CreateGameObject("Parent");
    auto* a=scene->CreateGameObject("A",parent);
    auto* b=scene->CreateGameObject("B",parent);
    auto* c=scene->CreateGameObject("C",parent);
    auto* d=scene->CreateGameObject("D",parent);
    auto* descendant=scene->CreateGameObject("Descendant",b);
    Check(a&&b&&c&&d&&descendant,"Sibling fixture creation failed");
    parent->GetTransform()->SetLocalPosition({1,2,3});parent->GetTransform()->SetLocalScale({-2,3,4});
    b->GetTransform()->SetLocalPosition({.2f,.3f,.4f});b->GetTransform()->SetLocalScale({1,2,-3});
    Check(b->GetTransform()->SetLocalRotation({0,0,.2f,.9f}),"Sibling fixture rotation failed");
    descendant->GetTransform()->SetLocalPosition({3,2,1});
    auto* camera=b->AddComponent<GameSproutRuntime::ICameraComponent>();
    Check(camera&&scene->SetMainCamera(camera),"Sibling fixture camera failed");
    const auto local=b->GetTransform()->GetLocalMatrix(),world=b->GetTransform()->GetWorldMatrix(),descendantWorld=descendant->GetTransform()->GetWorldMatrix();
    const auto id=b->GetInstanceID(),parentID=parent->GetInstanceID();
    runtime->Update(0);Recorder recorder;Check(runtime->RegisterListener(&recorder),"Sibling listener registration failed");
    const auto order=[&](std::initializer_list<GameSproutRuntime::IGameObject*> expected) {
        Check(parent->GetChildCount()==expected.size(),"Sibling reorder changed child count");std::size_t index=0;
        for(auto* object:expected)Check(parent->GetChild(index++)==object,"Sibling order is incorrect");
    };
    Check(b->SetSiblingIndex(0),"Backward sibling move rejected");order({b,a,c,d});
    Check(b->Parent()==parent&&b->GetScene()==scene&&b->GetInstanceID()==id&&b->GetComponent<GameSproutRuntime::ICameraComponent>()==camera&&scene->GetMainCamera()==camera,
        "Sibling reorder changed ownership, identity or main camera");
    CheckMatrix(b->GetTransform()->GetLocalMatrix(),local,"Sibling reorder changed local transform");
    CheckMatrix(b->GetTransform()->GetWorldMatrix(),world,"Sibling reorder changed world transform");
    CheckMatrix(descendant->GetTransform()->GetWorldMatrix(),descendantWorld,"Sibling reorder changed descendant transform");
    Check(recorder.Events.empty(),"Sibling change event escaped Update dispatch");runtime->Update(0);
    Check(recorder.Events.size()==1&&recorder.Events.front().Type==GameSproutRuntime::SceneNodeEventType::SiblingOrderChanged&&
        recorder.Events.front().ObjectID==id&&recorder.Events.front().ParentID==parentID&&recorder.Events.front().OldParentID==parentID,
        "Sibling event lost stable node or parent identity");recorder.Events.clear();

    Check(b->SetSiblingIndex(0)&&!scene->SetSiblingIndex(0),"Sibling no-op or scene-root rejection failed");
    runtime->Update(0);Check(recorder.Events.empty(),"No-op or rejected root reorder emitted an event");
    Check(b->SetSiblingIndex(std::numeric_limits<std::size_t>::max()),"Out-of-range sibling index rejected instead of clamped");order({a,c,d,b});
    runtime->Update(0);Check(recorder.Events.size()==1,"Forward sibling move did not emit one event");recorder.Events.clear();
    Check(d->SetSiblingIndex(1),"Middle sibling move rejected");order({a,d,c,b});
    runtime->Update(0);recorder.Events.clear();
    Check(descendant->SetSiblingIndex(1000),"Only-child no-op rejected");runtime->Update(0);Check(recorder.Events.empty(),"Only-child no-op emitted event");

    std::size_t visited=0;
    parent->ForeachChild([&](GameSproutRuntime::IGameObject* object) {++visited;Check(!object->SetSiblingIndex(0),"Sibling reorder allowed during child traversal");});
    Check(visited==4,"Rejected sibling changes altered traversal");order({a,d,c,b});
    b->ForeachComponent([&](GameSproutRuntime::IComponent*){Check(!b->SetSiblingIndex(0),"Sibling reorder allowed during component traversal");});
    bool eventCalled=false,eventAccepted=true;
    recorder.DuringEvent=[&](const GameSproutRuntime::SceneNodeEvent& event) {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::Rename&&event.ObjectID==a->GetInstanceID()) {
            eventCalled=true;eventAccepted=b->SetSiblingIndex(0);
        }
    };
    a->SetName("A renamed");runtime->Update(0);Check(eventCalled&&!eventAccepted,"Sibling reorder allowed during listener callback");
    recorder.DuringEvent={};runtime->Update(0);order({a,d,c,b});
    for(const auto& event:recorder.Events)Check(event.Type!=GameSproutRuntime::SceneNodeEventType::SiblingOrderChanged,"Rejected callback reorder was queued for later");
    recorder.Events.clear();

    GameSproutRuntime::IGameObject* pending=nullptr;
    parent->ForeachChild([&](GameSproutRuntime::IGameObject* object) {
        if(object!=c)return;
        pending=scene->CreateGameObject("Pending",parent);Check(pending&&!pending->SetSiblingIndex(0),"Pending-attach sibling move accepted");
        Check(scene->DestroyGameObject(c)&&!c->SetSiblingIndex(0),"Pending-destroy sibling move accepted");
    });
    Check(pending&&pending->Parent()==parent&&parent->GetChild(3)==pending,"Deferred fixture did not attach/destroy correctly");
    Check(pending->SetSiblingIndex(0),"Attached replacement sibling cannot be restored to its saved index");order({pending,a,d,b});
    runtime->UnregisterListener(&recorder);Check(runtime->DestroyScene(scene),"Sibling-order fixture leaked");
}

int main(int argc, char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime = nullptr;
    GameSproutRuntime::IGameSproutRuntime* independent = nullptr;
    try {
        Check(argc == 2, "Expected real Runtime library directory");
        runtime = Export(argv[1]);
        independent = Export(argv[1]);
        Check(runtime && independent && runtime != independent, "Real exported factory unavailable or shared instance");
        Check(runtime->GetSceneCount() == 0 && !runtime->GetScene(0), "New Runtime contains unexpected scenes");
        auto* independentScene = independent->CreateScene("Independent");
        Check(independentScene != nullptr && runtime->GetSceneCount() == 0, "Runtime instances share scene state");
        Check(!runtime->DestroyScene(independentScene), "Foreign Runtime scene destruction accepted");
        CheckHierarchy(runtime);
        Check(runtime->GetSceneCount() == 0, "Hierarchy scenes leaked");
        CheckDeferredMutation(runtime);
        Check(runtime->GetSceneCount() == 0, "Deferred scene destruction did not complete");
        CheckEvents(runtime);
        CheckStructuralEdges(runtime);
        CheckMainCamera(runtime);
        CheckRotationStability(runtime);
        CheckSiblingOrdering(runtime);
        runtime->Update(0.016f);
        runtime->Update(-1);
        runtime->Update(std::numeric_limits<float>::quiet_NaN());
        runtime->Update(std::numeric_limits<float>::infinity());
        runtime->Render(); // 当前只是明确的 GRHI 接入口，不应产生 GPU 对象或崩溃。
        Check(independent->GetSceneCount() == 1, "One Runtime update affected another");
        delete runtime; runtime = nullptr;
        Check(independent->GetScene(0) == independentScene, "Destroying one Runtime invalidated another");
        delete independent; independent = nullptr;
        std::cout << "PASS real Runtime export, scenes/nodes/component factories, transforms/cameras, validation, deferred mutation and instance isolation\n";
        return 0;
    } catch (const std::exception& error) {
        delete runtime;
        delete independent;
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
