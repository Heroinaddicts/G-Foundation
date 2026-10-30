#pragma once
#include "GameSproutApi.h"
#include <string>
#include <vector>

struct ProjectAssetSelection {
    std::string GuidString;
    uint64_t LocalID = 0; // 源文件/目录为 0；子资源为非零 LocalID。
    bool IsSubAsset = false;
    bool operator==(const ProjectAssetSelection &) const = default;
};

class ISelectionListener {
public:
    virtual ~ISelectionListener() {}
    virtual void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> & selection) = 0;
};

// 编辑器主线程使用。持有身份快照，不持有资源对象；监听者由调用者管理。
class ISelectionManager : public GameSprout::IGameSproutModule {
public:
    virtual ~ISelectionManager() {}
    virtual std::vector<ProjectAssetSelection> GetAssetSelection() const = 0;
    virtual void SetAssetSelection(const std::vector<ProjectAssetSelection> & selection) = 0;
    // Update 合并派发；先订阅，再查询初始选择，释放监听者前注销。
    virtual bool RegisterSelectionListener(ISelectionListener * listener) = 0;
    virtual void UnregisterSelectionListener(ISelectionListener * listener) = 0;
};
