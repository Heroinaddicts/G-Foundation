#include "SceneDocument.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
Json::Value Values(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);
    for (float value:values) result.append(value);
    return result;
}
Json::Value Vector(GameSproutRuntime::Vector2 value) {return Values({value.X,value.Y});}
Json::Value Vector(GameSproutRuntime::Vector3 value) {return Values({value.X,value.Y,value.Z});}
Json::Value Reference(const GameSproutRuntime::ResourceReference& reference) {
    if (reference.Guid.empty()) {
        if (reference.LocalID) throw std::runtime_error("Unbound resource has a nonzero LocalID");
        return {};
    }
    Json::Value result(Json::objectValue);
    result["Guid"]=reference.Guid;result["LocalID"]=Json::UInt64(reference.LocalID);
    return result;
}
Json::Value Components(GameSproutRuntime::IGameObject& object) {
    Json::Value result(Json::arrayValue);
    for (std::size_t index=0;index<object.GetComponentCount();++index) {
        auto* component=object.GetComponentAt(index);
        if (!component || !component->GetTypeName()) throw std::runtime_error("Invalid Runtime component");
        Json::Value data(Json::objectValue);
        data["Type"]=component->GetTypeName();
        const std::string type=component->GetTypeName();
        if (type=="GameSproutRuntime.Transform") {
            auto* value=dynamic_cast<GameSproutRuntime::ITransformComponent*>(component);
            if (!value) throw std::runtime_error("Transform interface mismatch");
            data["Position"]=Vector(value->GetLocalPosition());data["Scale"]=Vector(value->GetLocalScale());
            const auto rotation=value->GetLocalRotation();data["Rotation"]=Values({rotation.X,rotation.Y,rotation.Z,rotation.W});
        } else if (type=="GameSproutRuntime.RectTransform") {
            auto* value=dynamic_cast<GameSproutRuntime::IRectTransformComponent*>(component);
            if (!value) throw std::runtime_error("RectTransform interface mismatch");
            data["AnchorMin"]=Vector(value->GetAnchorMin());data["AnchorMax"]=Vector(value->GetAnchorMax());
            data["Pivot"]=Vector(value->GetPivot());data["AnchoredPosition"]=Vector(value->GetAnchoredPosition());
            data["SizeDelta"]=Vector(value->GetSizeDelta());
        } else if (type=="GameSproutRuntime.Image") {
            auto* value=dynamic_cast<GameSproutRuntime::IImageComponent*>(component);
            if (!value) throw std::runtime_error("Image interface mismatch");
            const auto texture=Reference(value->GetTexture());if (!texture.isNull()) data["Texture"]=texture;
            const auto color=value->GetColor();data["Color"]=Values({color.R,color.G,color.B,color.A});
            const auto uv=value->GetUVRect();data["UVRect"]["Position"]=Vector(uv.Position);data["UVRect"]["Size"]=Vector(uv.Size);
        } else if (type=="GameSproutRuntime.Canvas") {
            auto* value=dynamic_cast<GameSproutRuntime::ICanvasComponent*>(component);
            if (!value) throw std::runtime_error("Canvas interface mismatch");
            data["RenderMode"]=value->GetRenderMode()==GameSproutRuntime::CanvasRenderMode::WorldSpace?"WorldSpace":"ScreenSpace";
            data["ReferenceResolution"]=Vector(value->GetReferenceResolution());data["PixelsPerUnit"]=value->GetPixelsPerUnit();
        } else if (type=="GameSproutRuntime.Mesh") {
            auto* value=dynamic_cast<GameSproutRuntime::IMeshComponent*>(component);
            if (!value) throw std::runtime_error("Mesh interface mismatch");
            const auto mesh=Reference(value->GetMesh());if (!mesh.isNull()) data["Mesh"]=mesh;
            // Bounds 是加载后派生数据，不写入编辑器场景源文件。
        } else if (type=="GameSproutRuntime.MeshRenderer") {
            auto* value=dynamic_cast<GameSproutRuntime::IMeshRendererComponent*>(component);
            if (!value) throw std::runtime_error("MeshRenderer interface mismatch");
            data["Visible"]=value->IsVisible();data["Materials"]=Json::Value(Json::arrayValue);
            if (value->GetMaterialCount()>1024) throw std::runtime_error("Too many material slots");
            for (std::size_t slot=0;slot<value->GetMaterialCount();++slot) data["Materials"].append(Reference(value->GetMaterial(slot)));
        } else if (type=="GameSproutRuntime.Camera") {
            auto* value=dynamic_cast<GameSproutRuntime::ICameraComponent*>(component);
            if (!value) throw std::runtime_error("Camera interface mismatch");
            const auto& settings=value->GetSettings();auto& json=data["Settings"];
            json["Projection"]=settings.Projection==GameSproutRuntime::CameraProjection::Orthographic?"Orthographic":"Perspective";
            json["VerticalFovRadians"]=settings.VerticalFovRadians;json["OrthographicHeight"]=settings.OrthographicHeight;
            json["NearPlane"]=settings.NearPlane;json["FarPlane"]=settings.FarPlane;json["CullingMask"]=Json::UInt(settings.CullingMask);
        } else throw std::runtime_error("Unknown component cannot be saved: "+type);
        result.append(std::move(data));
    }
    return result;
}
}

namespace SceneDocument {
bool Serialize(GameSproutRuntime::IScene& scene,ObjectIDs& ids,Json::Value& output,std::string& error) {
    error.clear();
    try {
        struct Visit {GameSproutRuntime::IGameObject* Object;std::size_t Depth;};
        std::vector<Visit> pending,ordered;
        for (std::size_t i=scene.GetChildCount();i>0;--i) pending.push_back({scene.GetChild(i-1),1});
        std::set<std::uint64_t> visited,fileIDs;
        ObjectIDs next;next.Next=ids.Next;
        while (!pending.empty()) {
            const auto entry=pending.back();pending.pop_back();
            if (!entry.Object || entry.Object->GetScene()!=&scene || entry.Depth>1024 || ordered.size()>=100000 ||
                !visited.insert(entry.Object->GetInstanceID()).second) throw std::runtime_error("Invalid or oversized Runtime hierarchy");
            const auto world=entry.Object->GetTransform()->GetWorldMatrix();
            for (float value:world.Data) if (!std::isfinite(value)) throw std::runtime_error("World transform overflows for object "+entry.Object->GetName());
            ordered.push_back(entry);
            const auto found=ids.RuntimeToFile.find(entry.Object->GetInstanceID());
            if (found!=ids.RuntimeToFile.end()) {
                if (!found->second || !fileIDs.insert(found->second).second || (next.Next && found->second>=next.Next))
                    throw std::runtime_error("Invalid stable file ID map");
                next.RuntimeToFile.emplace(found->first,found->second);
            }
            for (std::size_t i=entry.Object->GetChildCount();i>0;--i) {
                auto* child=entry.Object->GetChild(i-1);
                if (!child || child->Parent()!=entry.Object) throw std::runtime_error("Invalid Runtime parent relationship");
                pending.push_back({child,entry.Depth+1});
            }
        }
        for (const auto& entry:ordered) if (!next.RuntimeToFile.count(entry.Object->GetInstanceID())) {
            if (!next.Next) throw std::runtime_error("Scene file ID space exhausted");
            const auto id=next.Next;
            next.Next=id==std::numeric_limits<std::uint64_t>::max()?0:id+1;
            next.RuntimeToFile.emplace(entry.Object->GetInstanceID(),id);
        }
        Json::Value document(Json::objectValue);
        document["Version"]=Json::UInt(1);document["Name"]=scene.GetName();document["RootActive"]=scene.IsActiveSelf();
        document["RootComponents"]=Components(scene);document["NextObjectID"]=Json::UInt64(next.Next);
        document["MainCameraID"]=Json::UInt64(0);document["Objects"]=Json::Value(Json::arrayValue);
        for (const auto& entry:ordered) {
            auto* object=entry.Object;auto* parent=object->Parent();
            if (!parent) throw std::runtime_error("Detached Runtime object has no scene parent");
            Json::Value node(Json::objectValue);
            node["ID"]=Json::UInt64(next.RuntimeToFile.at(object->GetInstanceID()));
            node["ParentID"]=Json::UInt64(parent==&scene?0:next.RuntimeToFile.at(parent->GetInstanceID()));
            node["Name"]=object->GetName();node["Active"]=object->IsActiveSelf();node["Components"]=Components(*object);
            document["Objects"].append(std::move(node));
        }
        if (auto* camera=scene.GetMainCamera()) {
            auto* object=camera->GetGameObject();
            if (object==&scene) document["RootMainCamera"]=true;
            else document["MainCameraID"]=Json::UInt64(next.RuntimeToFile.at(object->GetInstanceID()));
        }
        if (!Validate(document,error)) return false;
        output.swap(document);ids.RuntimeToFile.swap(next.RuntimeToFile);ids.Next=next.Next;
        return true;
    } catch (const std::exception& exception) {error=exception.what();}
    catch (...) {error="Unexpected scene serialization failure";}
    return false;
}
}
