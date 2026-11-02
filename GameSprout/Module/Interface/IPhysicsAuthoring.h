#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimePhysicsData.h"
#include "ResourceReference.h"

// Editor 物理作者服务：源资源、校验与文件事务，不负责模拟或界面。
class IPhysicsAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IPhysicsAuthoring() = default;
    virtual bool CreatePhysicsMaterialAsset(const std::string& directory,const std::string& name,
        std::string& path,std::string& error) = 0;
    // LocalID=0 denotes the standalone source, including before a successful import.
    virtual bool LoadPhysicsMaterialSource(const GameSproutRuntime::ResourceReference& reference,
        GameSproutRuntime::PhysicsMaterialData& data,std::string& sourceSnapshot,std::string& error) const = 0;
    // expectedSourceSnapshot 拒绝覆盖外部修改。参数属于源资产，不属于 Meta 导入设置。
    virtual bool SavePhysicsMaterialSource(const GameSproutRuntime::ResourceReference& reference,
        const GameSproutRuntime::PhysicsMaterialData& data,const std::string& expectedSourceSnapshot,
        std::string& error) = 0;
};
