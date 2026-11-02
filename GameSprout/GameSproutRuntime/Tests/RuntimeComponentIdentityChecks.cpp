#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <stdexcept>
#include <set>

static void Check(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass the Runtime directory");
        runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        auto* scene=runtime->CreateScene("Components");
        auto* object=scene->CreateGameObject("Repeated colliders");
        auto* first=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto* second=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto* sphere=object->AddComponent<GameSproutRuntime::ISphereColliderComponent>();
        Check(first&&second&&sphere&&first!=second,"Same Collider type was treated as a singleton");
        const auto firstID=first->GetComponentInstanceID(),secondID=second->GetComponentInstanceID();
        Check(firstID&&secondID&&firstID!=secondID,"Live Collider identities are missing or repeated");
        Check(object->GetComponent<GameSproutRuntime::IColliderComponent>()==first,
            "Base Collider lookup did not resolve a derived Collider");
        Check(object->GetComponents<GameSproutRuntime::IColliderComponent>().size()==3,
            "Base Collider enumeration lost repeated or derived instances");
        auto* rigidbody=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
        Check(rigidbody&&object->AddComponent<GameSproutRuntime::IRigidbodyComponent>()==rigidbody,
            "Rigidbody lost its singleton contract");
        std::set<std::uint64_t> identities;
        for(std::size_t i=0;i<object->GetComponentCount();++i)
            Check(identities.insert(object->GetComponentAt(i)->GetComponentInstanceID()).second,
                "Components on an owner share a live identity");
        Check(object->RemoveComponentInstance(secondID),"Exact Collider removal failed");
        const auto remaining=object->GetComponents<GameSproutRuntime::IColliderComponent>();
        Check(remaining.size()==2&&remaining.front()==first,"Exact removal deleted the first Collider instead");
        auto* third=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        Check(third&&third->GetComponentInstanceID()>secondID,"A deleted component identity was reused");
        Check(!object->RemoveComponentInstance(object->GetTransform()->GetComponentInstanceID()),
            "Exact removal deleted the intrinsic Transform");
        std::string error;
        auto* fromCatalog=runtime->AddBuiltinComponent(object,"GameSproutRuntime.BoxCollider",error);
        Check(fromCatalog&&fromCatalog!=first&&fromCatalog!=third,"Catalog add rejected or reused a repeated Collider");
        bool foundCollider=false,foundBody=false;
        for(const auto& type:runtime->GetBuiltinComponentTypes()) {
            if(type.TypeID=="GameSproutRuntime.BoxCollider")foundCollider=type.AllowMultiple;
            if(type.TypeID=="GameSproutRuntime.Rigidbody")foundBody=!type.AllowMultiple;
        }
        Check(foundCollider&&foundBody,"Add Component catalog has incorrect multiplicity");
        delete runtime;runtime=nullptr;
        std::cout<<"PASS component identities, inherited Collider lookup, multiplicity and exact removal\n";
        return 0;
    } catch(const std::exception& error) {
        delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
