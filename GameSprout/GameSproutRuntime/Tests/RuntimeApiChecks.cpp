#include "GameSproutRuntimeApi.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <typeindex>
#include <map>

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static bool Near(float a, float b) { return std::fabs(a-b) < 0.0002f; }
static void CheckVector(GameSproutRuntime::Vector3 a, GameSproutRuntime::Vector3 b) {
    Check(Near(a.X,b.X) && Near(a.Y,b.Y) && Near(a.Z,b.Z), "Vector mismatch");
}
static void CheckMatrix(const GameSproutRuntime::Mat4& a, const GameSproutRuntime::Mat4& b) {
    for (std::size_t i=0; i<16; ++i) Check(Near(a.Data[i],b.Data[i]), "Matrix mismatch");
}
static GameSproutRuntime::Mat4 Rotation(GameSproutRuntime::Quaternion q) {
    GameSproutRuntime::Mat4 m;
    m(0,0)=1-2*(q.Y*q.Y+q.Z*q.Z); m(0,1)=2*(q.X*q.Y-q.Z*q.W); m(0,2)=2*(q.X*q.Z+q.Y*q.W);
    m(1,0)=2*(q.X*q.Y+q.Z*q.W); m(1,1)=1-2*(q.X*q.X+q.Z*q.Z); m(1,2)=2*(q.Y*q.Z-q.X*q.W);
    m(2,0)=2*(q.X*q.Z-q.Y*q.W); m(2,1)=2*(q.Y*q.Z+q.X*q.W); m(2,2)=1-2*(q.X*q.X+q.Y*q.Y);
    return m;
}

// 测试实现只存在于测试程序。外部调用仅拿到抽象接口，不能 new IImageComponent。
class Image final : public GameSproutRuntime::IImageComponent {
public:
    explicit Image(GameSproutRuntime::IGameObject* owner) : IImageComponent(owner) {}
    const std::type_info& GetComponentType() const override { return typeid(GameSproutRuntime::IImageComponent); }
    const char* GetTypeName() const override { return "GameSproutRuntime.Image"; }
    GameSproutRuntime::ResourceReference GetTexture() const override { return Texture; }
    void SetTexture(const GameSproutRuntime::ResourceReference& value) override { Texture=value; }
    GameSproutRuntime::Color GetColor() const override { return Tint; }
    void SetColor(GameSproutRuntime::Color value) override { Tint=value; }
    GameSproutRuntime::Rect GetUVRect() const override { return UV; }
    void SetUVRect(const GameSproutRuntime::Rect& value) override { UV=value; }
    GameSproutRuntime::ResourceReference Texture;
    GameSproutRuntime::Color Tint;
    GameSproutRuntime::Rect UV{{0,0},{1,1}};
};

class Object final : public GameSproutRuntime::IGameObject {
public:
    ~Object() override { for (auto& entry:Components) delete entry.second; }
    std::uint64_t GetInstanceID() const override { return 1; }
    const std::string& GetName() const override { return Name; }
    void SetName(const std::string& name) override { Name=name; }
    GameSproutRuntime::IScene* GetScene() const override { return nullptr; }
    IGameObject* Parent() const override { return nullptr; }
    std::size_t GetChildCount() const override { return 0; }
    IGameObject* GetChild(std::size_t) const override { return nullptr; }
    IGameObject* FindChild(const std::string&) const override { return nullptr; }
    void ForeachChild(const fForeachChildCallback&) const override {}
    bool AddChild(IGameObject*, bool) override { return false; }
    bool RemoveChild(IGameObject*, bool) override { return false; }
    bool SetSiblingIndex(std::size_t) override { return false; }
    void SetActive(bool value) override { Active=value; }
    bool IsActiveSelf() const override { return Active; }
    bool IsActiveInHierarchy() const override { return Active; }
    GameSproutRuntime::ITransformComponent* GetTransform() const override { return nullptr; }
    std::size_t GetComponentCount() const override { return Components.size(); }
    GameSproutRuntime::IComponent* GetComponentAt(std::size_t index) const override {
        for(const auto& entry:Components) if(index--==0) return entry.second;
        return nullptr;
    }
    void ForeachComponent(const fForeachComponentCallback& callback) const override {
        if(callback) for(const auto& entry:Components) callback(entry.second);
    }
private:
    GameSproutRuntime::IComponent* AddComponent(const std::type_info& type) override {
        if(auto* existing=GetComponent(type)) return existing;
        if(type!=typeid(GameSproutRuntime::IImageComponent)) return nullptr;
        auto* image=new Image(this);
        Components.emplace(std::type_index(type),image);
        image->OnCreate(nullptr);
        return image;
    }
    GameSproutRuntime::IComponent* GetComponent(const std::type_info& type) const override {
        const auto found=Components.find(std::type_index(type));
        return found==Components.end()?nullptr:found->second;
    }
    bool RemoveComponent(const std::type_info& type) override {
        const auto found=Components.find(std::type_index(type));
        if(found==Components.end()) return false;
        found->second->OnRelease(nullptr); delete found->second; Components.erase(found); return true;
    }
    std::string Name;
    bool Active=true;
    std::map<std::type_index,GameSproutRuntime::IComponent*> Components;
};

int main() {
    try {
        static_assert(sizeof(GameSproutRuntime::Vector2)==8);
        static_assert(sizeof(GameSproutRuntime::Vector3)==12);
        static_assert(sizeof(GameSproutRuntime::Vector4)==16);
        static_assert(sizeof(GameSproutRuntime::Mat3)==36);
        static_assert(sizeof(GameSproutRuntime::Mat4)==64);
        static_assert(sizeof(GameSproutRuntime::Quaternion)==16);
        Object implementation;
        GameSproutRuntime::IGameObject* object=&implementation;
        auto* image=object->AddComponent<GameSproutRuntime::IImageComponent>();
        Check(image && image->GetGameObject()==object, "Factory did not bind owner");
        Check(image==object->GetComponent<GameSproutRuntime::IImageComponent>() &&
              image==object->AddComponent<GameSproutRuntime::IImageComponent>(), "Interface type lookup/duplicate mismatch");
        Check(!object->AddComponent<GameSproutRuntime::IMeshComponent>(), "Unsupported interface created");
        image->SetTexture({"0123456789abcdef0123456789abcdef",7});
        Check(image->GetTexture().LocalID==7, "Resource identity lost");
        Check(object->RemoveComponent<GameSproutRuntime::IImageComponent>() &&
              !object->GetComponent<GameSproutRuntime::IImageComponent>(), "Component removal failed");

        GameSproutRuntime::CoordinateConversion conversion;
        const GameSproutRuntime::CoordinateSystem left{
            GameSproutRuntime::Handedness::LeftHanded,GameSproutRuntime::Axis::PositiveY,GameSproutRuntime::Axis::PositiveZ,0.01f};
        Check(GameSproutRuntime::CoordinateConversion::TryCreate(left,GameSproutRuntime::RuntimeCoordinates,conversion), "LH conversion failed");
        CheckVector(conversion.ConvertPosition({100,200,300}),{1,2,-3});
        CheckVector(conversion.ConvertDirection({0,0,1}),{0,0,-1});
        Check(conversion.ReversesWinding,"Handedness did not flag winding");
        CheckVector(conversion.ConvertBounds({{100,200,300},{10,20,30}}).Extents,{0.1f,0.2f,0.3f});
        const auto convertedPlane=conversion.ConvertPlane({{0,0,1},-300});
        Check(Near(convertedPlane.SignedDistance({1,2,-3}),0),"Plane offset conversion failed");
        const GameSproutRuntime::Ray ray{{100,200,300},{0,0,1}};
        const auto convertedRay=conversion.ConvertRay(ray);
        CheckVector(convertedRay.GetPoint(conversion.ConvertDistance(200)),conversion.ConvertPosition(ray.GetPoint(200)));

        // 穷举 48 种合法轴/手性约定的两两换基，检查逆变换、方向、旋转和节点矩阵。
        std::array<GameSproutRuntime::CoordinateSystem,48> systems{};
        std::size_t count=0;
        for(int hand=0;hand<2;++hand) for(int up=0;up<6;++up) for(int forward=0;forward<6;++forward) {
            GameSproutRuntime::CoordinateSystem system{static_cast<GameSproutRuntime::Handedness>(hand),
                static_cast<GameSproutRuntime::Axis>(up),static_cast<GameSproutRuntime::Axis>(forward),hand==0?1.0f:0.01f};
            if(GameSproutRuntime::CoordinateConversion::TryCreate(system,GameSproutRuntime::RuntimeCoordinates,conversion)) systems[count++]=system;
        }
        Check(count==48,"Unexpected coordinate combinations");
        GameSproutRuntime::Quaternion rotation;
        Check(GameSproutRuntime::Quaternion(1,2,3,4).TryNormalize(rotation),"Quaternion normalization failed");
        const auto sourceRotation=Rotation(rotation);
        for(const auto& source:systems) for(const auto& target:systems) {
            GameSproutRuntime::CoordinateConversion reverse;
            Check(GameSproutRuntime::CoordinateConversion::TryCreate(source,target,conversion) &&
                  GameSproutRuntime::CoordinateConversion::TryCreate(target,source,reverse),"Pair conversion failed");
            CheckVector(reverse.ConvertPosition(conversion.ConvertPosition({1,2,3})),{1,2,3});
            CheckVector(reverse.ConvertDirection(conversion.ConvertDirection({4,5,6})),{4,5,6});
            CheckMatrix(conversion.Matrix*conversion.InverseMatrix,GameSproutRuntime::Mat4{});
            GameSproutRuntime::Quaternion converted;
            Check(conversion.TryConvertRotation(rotation,converted),"Rotation conversion failed");
            CheckMatrix(Rotation(converted),conversion.ConvertTransform(sourceRotation));
            auto local=sourceRotation; local(0,3)=1; local(1,3)=2; local(2,3)=3;
            auto parent=sourceRotation; parent(0,3)=4;
            CheckMatrix(conversion.ConvertTransform(parent*local),conversion.ConvertTransform(parent)*conversion.ConvertTransform(local));
        }
        const auto unchanged=conversion.Matrix;
        auto invalid=GameSproutRuntime::RuntimeCoordinates; invalid.Forward=invalid.Up;
        Check(!GameSproutRuntime::CoordinateConversion::TryCreate(invalid,GameSproutRuntime::RuntimeCoordinates,conversion),"Parallel axes accepted");
        CheckMatrix(unchanged,conversion.Matrix);
        invalid=GameSproutRuntime::RuntimeCoordinates; invalid.MetersPerUnit=0;
        Check(!GameSproutRuntime::CoordinateConversion::TryCreate(invalid,GameSproutRuntime::RuntimeCoordinates,conversion),"Zero unit accepted");

        GameSproutRuntime::Mat4 view,projection;
        Check(GameSproutRuntime::TryMakeLookAt({0,0,3},{0,0,0},{0,1,0},view),"LookAt failed");
        const auto origin=view*GameSproutRuntime::Vector4(0,0,0,1);
        Check(Near(origin.Z,-3)&&Near(origin.W,1),"Camera position mapping failed");
        Check(!GameSproutRuntime::TryMakeLookAt({0,0,0},{0,0,0},{0,1,0},view),"Degenerate camera accepted");
        Check(GameSproutRuntime::TryMakePerspective(GameSproutRuntime::DegreesToRadians(60),1.5f,0.1f,100,projection),"Perspective failed");
        const auto near=projection*GameSproutRuntime::Vector4(0,0,-0.1f,1);
        const auto far=projection*GameSproutRuntime::Vector4(0,0,-100,1);
        Check(Near(near.Z/near.W,0)&&Near(far.Z/far.W,1),"Canonical depth mismatch");
        const auto legacy=GameSproutRuntime::AdaptProjection(projection,GameSproutRuntime::ClipDepthRange::MinusOneToOne,true);
        const auto legacyNear=legacy*GameSproutRuntime::Vector4(0,0,-0.1f,1);
        Check(Near(legacyNear.Z/legacyNear.W,-1),"Backend depth adaptation failed");
        Check(!GameSproutRuntime::TryMakePerspective(0,1,0.1f,100,projection),"Invalid FOV accepted");
        Check(GameSproutRuntime::TryMakeOrthographic(10,2,0.1f,100,projection),"Orthographic failed");
        Check(Near((projection*GameSproutRuntime::Vector4(0,0,-100,1)).Z,1),"Orthographic far depth mismatch");
        const GameSproutRuntime::Viewport viewport{{{10,20},{800,600}}};
        CheckVector(viewport.NdcToScreen({-1,1,0}),{10,20,0});
        GameSproutRuntime::Vector3 ndc;
        Check(viewport.TryScreenToNdc(viewport.NdcToScreen({0.2f,-0.3f,0.7f}),ndc),"Screen conversion failed");
        CheckVector(ndc,{0.2f,-0.3f,0.7f});
        Check(GameSproutRuntime::Vector3(0,0,0).TryNormalize(ndc)==false,"Zero direction normalized");
        std::cout << "PASS abstract component API, resource references, 2304 coordinate pairs, quaternion/transform conversion, projection and screen mapping\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
