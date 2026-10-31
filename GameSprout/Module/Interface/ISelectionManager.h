#pragma once
#include "GameSproutEditorApi.h"
#include <string>
#include <vector>

struct ProjectAssetSelection {
    std::string GuidString;
    uint64_t LocalID = 0; // 源文件/目录为 0；子资源为非零 LocalID。
    bool IsSubAsset = false;
    bool operator==(const ProjectAssetSelection &) const = default;
};

struct SceneObjectSelection {
    uint64_t SceneID = 0;
    uint64_t ObjectID = 0;
    bool operator==(const SceneObjectSelection &) const = default;
};

class ISelectionListener {
public:
    virtual ~ISelectionListener() {}
    virtual void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> & selection) = 0;
    virtual void OnSceneObjectSelectionChanged(const SceneObjectSelection & selection) { (void)selection; }
};

// 编辑器主线程使用。持有身份快照，不持有资源对象；监听者由调用者管理。
class ISelectionManager : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~ISelectionManager() {}
    virtual std::vector<ProjectAssetSelection> GetAssetSelection() const = 0;
    virtual void SetAssetSelection(const std::vector<ProjectAssetSelection> & selection) = 0;
    virtual SceneObjectSelection GetSceneObjectSelection() const = 0;
    virtual void SetSceneObjectSelection(const SceneObjectSelection & selection) = 0;
    // 非空资源/节点选择互斥。空选择只清理本类目标，避免另一个 View 刷新时抢回选择。
    // Update 合并派发；先订阅，再查询初始选择，释放监听者前注销。
    virtual bool RegisterSelectionListener(ISelectionListener * listener) = 0;
    virtual void UnregisterSelectionListener(ISelectionListener * listener) = 0;
};
