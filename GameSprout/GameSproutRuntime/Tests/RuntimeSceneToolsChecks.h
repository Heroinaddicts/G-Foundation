#pragma once
#include <cmath>

static void CheckSceneTools(GameSproutRuntime::IGameSproutRuntime& runtime,GameSproutRuntime::IRuntimeRenderer& renderer,
    GameSproutRuntime::IScene& scene,GameSproutRuntime::IGameObject& object,Provider& provider,
    GameSproutRuntime::RenderView& view,GRHI::IDevice& device,GRHI::ITexture& target) {
    GameSproutRuntime::RenderedCamera camera;
    Check(renderer.GetRenderedCamera(camera) && camera.Width==128 && camera.Height==128 && camera.SceneID==scene.GetInstanceID(),"Rendered camera did not report actual target/scene");
    GameSproutRuntime::SceneRaycastHit hit;
    const GameSproutRuntime::Ray ray{{0,0,3},{0,0,-7}};
    const int loaded=provider.MeshLoads;
    Check(renderer.Raycast(&scene,ray,hit) && hit.ObjectID==object.GetInstanceID() && std::fabs(hit.Distance-3)<1e-5f && hit.Normal.Z>0.99f,
        "Exact raycast did not normalize distance or return triangle geometry");
    Check(provider.MeshLoads==loaded,"Picking reloaded/repacked an unchanged mesh");
    Check(!renderer.Raycast(&scene,{{0.9f,0.9f,3},{0,0,-1}},hit) && !hit.ObjectID,"Raycast selected an AABB corner outside the triangle");
    Check(!renderer.Raycast(&scene,ray,hit,0) && !hit.ObjectID,"Raycast ignored culling mask");
    Check(!renderer.Raycast(&scene,{{0,0,3},{0,0,0}},hit) && !renderer.GetLastError().empty(),"Zero-direction ray accepted");
    auto* parent=scene.CreateGameObject("Mirrored picking parent");
    parent->GetTransform()->SetLocalPosition({2,1,0.5f});parent->GetTransform()->SetLocalScale({-2,3,0.5f});
    Check(parent->GetTransform()->SetLocalRotation({0,0,0.38268343f,0.9238795f}),"Cannot set transformed picking parent");
    auto* child=scene.CreateGameObject("Nearest picking child",parent);
    child->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(object.GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh());
    auto* childRenderer=child->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    Check(renderer.Raycast(&scene,{{2,1,3},{0,0,-3}},hit) && hit.ObjectID==child->GetInstanceID() && std::fabs(hit.Distance-2.5f)<1e-5f && hit.Normal.Z<-.99f,
        "Parent nonuniform/mirrored rotation broke exact nearest world raycast");
    GameSproutRuntime::Bounds bounds;
    Check(renderer.GetWorldBounds(&scene,child->GetInstanceID(),bounds) && std::fabs(bounds.Center.X-2)<1e-5f && std::fabs(bounds.Center.Y-1)<1e-5f &&
        std::fabs(bounds.Center.Z-.5f)<1e-5f && bounds.Extents.X>3.5f && bounds.Extents.Y>3.5f,
        "Transformed indexed mesh world bounds mismatch");
    // 非对称三角形旋转后，tight worldAABB的中心会漂移；Renderer.bounds采用本地AABB的世界变换。
    // 非均匀且镜像的父缩放与子旋转组合还会形成shear，八角变换仍应保持M*localCenter。
    for(float angle:{.2f,.65f,1.1f}) {
        child->GetTransform()->SetLocalPosition({.3f,-.2f,.4f});
        Check(child->GetTransform()->SetLocalRotation({0,0,std::sin(angle*.5f),std::cos(angle*.5f)}),"Bounds rotation fixture failed");
        const auto world=child->GetTransform()->GetWorldMatrix();
        const auto expected=world*GameSproutRuntime::Vector4{0,0,0,1};
        Check(renderer.GetWorldBounds(&scene,child->GetInstanceID(),bounds) && std::fabs(bounds.Center.X-expected.X)<2e-5f &&
            std::fabs(bounds.Center.Y-expected.Y)<2e-5f && std::fabs(bounds.Center.Z-expected.Z)<2e-5f &&
            std::fabs(bounds.Extents.X-(std::fabs(world(0,0))+std::fabs(world(0,1))))<2e-5f &&
            std::fabs(bounds.Extents.Y-(std::fabs(world(1,0))+std::fabs(world(1,1))))<2e-5f,
            "Mesh rotation changed local-box center or broke conservative bounds under mirrored shear");
    }
    child->GetTransform()->SetLocalPosition({0,0,0});Check(child->GetTransform()->SetLocalRotation({0,0,0,1}),"Cannot restore bounds fixture");
    GameSproutRuntime::DebugDrawList outline;
    Check(renderer.AppendSelectionOutline(&scene,child->GetInstanceID(),outline) && outline.Lines.size()==3,"Selection outline did not use real triangle edges");
    const auto count=outline.Lines.size();
    Check(!renderer.AppendSelectionOutline(&scene,99999999,outline) && outline.Lines.size()==count,"Rejected outline partially changed the caller list");
    childRenderer->SetVisible(false);Check(!renderer.Raycast(&scene,{{2,1,3},{0,0,-1}},hit),"Invisible renderer was picked");childRenderer->SetVisible(true);
    parent->SetActive(false);Check(!renderer.Raycast(&scene,{{2,1,3},{0,0,-1}},hit),"Child of inactive parent was picked");parent->SetActive(true);
    parent->GetTransform()->SetLocalScale({0,1,1});Check(!renderer.Raycast(&scene,{{2,1,3},{0,0,-1}},hit),"Singular transform was picked");
    Check(scene.DestroyGameObject(parent),"Cannot clean transformed pick fixture");
    auto* near=scene.CreateGameObject("Nearest triangle");near->GetTransform()->SetLocalPosition({0,0,0.5f});
    near->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(object.GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh());
    near->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    Check(renderer.Raycast(&scene,ray,hit) && hit.ObjectID==near->GetInstanceID() && std::fabs(hit.Distance-2.5f)<1e-5f,"Raycast returned traversal-first instead of closest triangle");
    Check(scene.DestroyGameObject(near),"Cannot clean nearest pick fixture");
    bool rejected=false;scene.ForeachChild([&](GameSproutRuntime::IGameObject*) {rejected=!renderer.Raycast(&scene,ray,hit);});
    Check(rejected,"Raycast ran inside Runtime traversal callback");
    provider.Missing=true;++provider.Revision;
    Check(!renderer.Raycast(&scene,ray,hit) && !hit.ObjectID,"Deleted mesh revision retained stale picking geometry");
    provider.Missing=false;++provider.Revision;
    auto* objectRenderer=object.GetComponent<GameSproutRuntime::IMeshRendererComponent>();
    objectRenderer->SetMaterials({{"0123456789abcdef0123456789abcdef",2}});
    provider.MissingMaterial=true;++provider.Revision;
    Check(!renderer.Raycast(&scene,ray,hit),"Undrawn missing-material section was picked");
    provider.MissingMaterial=false;provider.MissingTexture=true;++provider.Revision;
    Check(!renderer.Raycast(&scene,ray,hit),"Undrawn missing-texture section was picked");
    provider.MissingTexture=false;objectRenderer->SetMaterials({});++provider.Revision;

    // 真实GPU验证：蓝色工具三角形在模型后面；深度打开被遮挡，关闭后覆盖模型。
    GameSproutRuntime::DebugDrawList debug;
    debug.Triangles.push_back({{-.7f,-.7f,-.5f},{.7f,-.7f,-.5f},{0,.7f,-.5f},{0,0,1,1},true});
    view.DebugDraw=&debug;
    Check(renderer.Render(view),renderer.GetLastError());
    auto pixel=Center(device,target);Check(pixel[0]>40,"Depth-tested tool incorrectly covered closer mesh");
    debug.Triangles[0].DepthTest=false;
    Check(renderer.Render(view),renderer.GetLastError());pixel=Center(device,target);
    Check(pixel[2]>240 && pixel[0]<5 && pixel[1]<5,"Depthless tool triangle was not actually drawn over the mesh");
    debug.Lines.push_back({{-.6f,0,-.6f},{.6f,0,-.6f},{1,0,0,1},false});
    Check(renderer.Render(view),renderer.GetLastError());
    std::vector<std::uint8_t> pixels;Check(device.ReadTexture(&target,pixels),device.GetLastError());
    bool redLine=false;
    for(int y=61;y<=66;++y) {const auto offset=(std::size_t(y)*128+64)*4;redLine|=pixels[offset]>240 && pixels[offset+1]<5 && pixels[offset+2]<5;}
    Check(redLine,"World-space debug line was not drawn after overlay fill");
    Check(renderer.GetRenderedCamera(camera) && camera.Width==128,"Debug frame lost rendered camera snapshot");
    debug.Lines[0].From.X=std::numeric_limits<float>::quiet_NaN();
    Check(!renderer.Render(view) && !renderer.GetRenderedCamera(camera) && !camera.Width,"Invalid debug input retained a valid rendered camera");
    view.DebugDraw=nullptr;
    Check(renderer.Render(view),renderer.GetLastError());
}
