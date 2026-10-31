#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "AnimationSampling.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <stdexcept>

namespace GameSproutRuntime {
void Runtime::SetResourceProvider(IRuntimeResourceProvider* provider){
    if(provider==_AnimationResources.GetProvider()||IsExecuting())return;
    for(const auto& entry:_Nodes)if(auto* animator=entry.second->Object->GetComponent<IAnimatorComponent>())animator->Stop();
    _AnimationResources.SetProvider(provider);
}
IGameObject* Runtime::InstantiateModel(IScene* scene,const ResourceReference& reference,IGameObject* parent,std::string& error){
    error.clear();auto* sceneState=Find(scene);
    if(IsExecuting()||!IsUsable(sceneState)||sceneState->GetScene()!=scene){error="Model instantiation requires a live idle Runtime scene";return nullptr;}
    if(parent){auto* state=Find(parent);if(!IsUsable(state)||state->GetScene()!=scene){error="Model parent belongs to another scene";return nullptr;}}
    auto* provider=GetResourceProvider();if(!provider){error="Runtime resource provider is not attached";return nullptr;}
    ModelData model;if(!provider->LoadModel(reference,model,error))return nullptr;
    SkeletonData structure;for(const auto& n:model.Nodes)structure.Nodes.push_back(n);
    if(!AnimationSampling::Validate(structure,error))return nullptr;
    for(const auto& node:model.Nodes)if(!RuntimeMath::IsFinite(node.GeometryTransform.Position)||!RuntimeMath::IsFinite(node.GeometryTransform.Scale)){error="Model geometry transform is invalid";return nullptr;}
    IGameObject* root=nullptr;
    try {
        std::string name="Model";for(const auto& node:model.Nodes)if(node.Parent<0&&!node.Name.empty()){name=node.Name;break;}
        root=scene->CreateGameObject(name,parent);if(!root)throw std::runtime_error("Cannot create model root");
        std::vector<IGameObject*> nodes(model.Nodes.size(),nullptr);std::size_t remaining=nodes.size();
        while(remaining){std::size_t created=0;for(std::size_t i=0;i<model.Nodes.size();++i){const auto& data=model.Nodes[i];if(nodes[i]||(data.Parent>=0&&!nodes[data.Parent]))continue;
                auto* object=scene->CreateGameObject(data.Name.empty()?"Root":data.Name,data.Parent>=0?nodes[data.Parent]:root);
                if(!object)throw std::runtime_error("Cannot create model node");auto* transform=object->GetTransform();transform->SetLocalPosition(data.Transform.Position);if(!transform->SetLocalRotation(data.Transform.Rotation))throw std::runtime_error("Invalid model rotation");transform->SetLocalScale(data.Transform.Scale);nodes[i]=object;--remaining;++created;}
            if(!created)throw std::runtime_error("Model node hierarchy has a cycle");
        }
        IAnimatorComponent* animator=nullptr;
        if(!model.Skeleton.Guid.empty()){
            animator=root->AddComponent<IAnimatorComponent>();if(!animator)throw std::runtime_error("Cannot create model Animator");
            animator->SetSkeleton(model.Skeleton);if(!model.Avatar.Guid.empty())animator->SetAvatar(model.Avatar);std::vector<AnimationNodeBinding> bindings;
            for(std::size_t i=0;i<nodes.size();++i)bindings.push_back({model.Nodes[i].Path,nodes[i]->GetInstanceID()});
            if(!animator->SetNodeBindings(bindings))throw std::runtime_error("Cannot bind model skeleton nodes");
            if(!model.Animations.empty())animator->SetDefaultClip(model.Animations.front());
        }
        for(std::size_t i=0;i<nodes.size();++i){const auto& data=model.Nodes[i];if(data.Mesh.Guid.empty())continue;
            // FBX geometry transform affects this mesh only, not its child bones.
            auto* geometry=scene->CreateGameObject(data.Name+" Geometry",nodes[i]);if(!geometry)throw std::runtime_error("Cannot create mesh geometry node");
            auto* transform=geometry->GetTransform();transform->SetLocalPosition(data.GeometryTransform.Position);if(!transform->SetLocalRotation(data.GeometryTransform.Rotation))throw std::runtime_error("Invalid geometry rotation");transform->SetLocalScale(data.GeometryTransform.Scale);
            auto* mesh=geometry->AddComponent<IMeshComponent>();auto* renderer=geometry->AddComponent<IMeshRendererComponent>();
            if(!mesh||!renderer)throw std::runtime_error("Cannot create model mesh components");mesh->SetMesh(data.Mesh);if(!renderer->SetMaterials(data.Materials))throw std::runtime_error("Cannot bind model materials");
            MeshRenderData prepared;if(!provider->LoadMesh(data.Mesh,prepared,error))throw std::runtime_error(error);
            if(!prepared.Bones.empty()){
                if(!animator)throw std::runtime_error("Skinned model has no Skeleton resource");
                auto* skin=geometry->AddComponent<ISkinnedMeshRendererComponent>();if(!skin||!skin->SetAnimatorObjectID(root->GetInstanceID()))throw std::runtime_error("Cannot bind model SkinnedMeshRenderer");
                for(const auto& bone:prepared.Bones){std::uint64_t id;if(!animator->ResolveBone(bone.NodePath,id))throw std::runtime_error("Model mesh bone path is missing: "+bone.NodePath);}
            }
        }
        return root;
    } catch(const std::exception& exception){error=exception.what();if(root)scene->DestroyGameObject(root);return nullptr;}
    catch(...){error="Model instantiation failed";if(root)scene->DestroyGameObject(root);return nullptr;}
}
}
