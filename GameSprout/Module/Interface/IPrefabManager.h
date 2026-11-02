#pragma once
#include "GameSproutEditorApi.h"
#include "ResourceReference.h"
#include <string>
#include <vector>

enum class PrefabOverrideKind : uint8_t { Property, AddedObject, RemovedObject, AddedComponent, RemovedComponent };
struct PrefabOverrideInfo {
    PrefabOverrideKind Kind=PrefabOverrideKind::Property;
    uint64_t SourceObjectID=0,ComponentID=0;
    std::string PropertyPath;
    std::string ObjectName,ComponentType;
    std::string SourceValueJson,InstanceValueJson;
};
struct PrefabInstanceInfo {
    uint64_t RootObjectID=0,RootFileID=0;
    GameSproutRuntime::ResourceReference Source;
    std::string SourcePath,Error;
    bool MissingSource=false,HasOverrides=false;
    std::vector<PrefabOverrideInfo> Overrides;
};
class IPrefabListener {
public:
    virtual ~IPrefabListener()=default;
    virtual void OnPrefabStateChanged()=0;
};
// Editor业务：作者文档、来源连接、覆盖与资源更新。所有接口在宿主主线程调用。
// 仅返回稳定身份和值；View不持有模板/Runtime节点地址，Runtime不依赖此接口。
class IPrefabManager : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IPrefabManager()=default;
    // relativePath相对Assets且后缀.prefab；创建不覆盖已有文件，成功后由资源管线登记。
    virtual bool SavePrefab(uint64_t objectID,const std::string& relativePath,std::string& error)=0;
    // Unity式创建：保存源后等待资源Ready，将原对象连接为实例；不替换原对象。
    virtual bool CreatePrefab(uint64_t objectID,const std::string& relativePath,std::string& error) {
        error="当前Prefab服务不支持创建并连接。";return false;
    }
    // Project目录接收Hierarchy拖入；业务层生成合法、唯一的对象名.prefab。
    virtual bool CreatePrefabInDirectory(uint64_t objectID,const std::string& relativeDirectory,
        std::string& outRelativePath,std::string& error) {error="当前Prefab服务不支持目录创建。";return false;}
    virtual bool InstantiatePrefab(const GameSproutRuntime::ResourceReference& source,uint64_t parentID,
        uint64_t& objectID,std::string& error)=0;
    // 接受实例根或后代；源暂时缺失仍返回连接信息，不静默解除来源。
    virtual bool ReadInstance(uint64_t objectID,PrefabInstanceInfo& info,std::string& error) const=0;
    virtual bool ApplyOverrides(uint64_t objectID,std::string& error)=0;
    virtual bool RevertOverrides(uint64_t objectID,std::string& error)=0;
    virtual bool RefreshSource(const std::string& guid,std::string& error)=0;
    virtual bool RegisterPrefabListener(IPrefabListener*)=0;
    virtual void UnregisterPrefabListener(IPrefabListener*)=0;
};
