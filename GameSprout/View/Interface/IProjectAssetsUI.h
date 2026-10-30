#ifndef __IProjectAssetsUI_h__
#define __IProjectAssetsUI_h__

#include "GameSproutApi.h"

struct ProjectAssetSelection {
    std::string GuidString;
    uint64_t LocalID = 0; // 源文件/目录选择时为 0；子资源为非零 LocalID。
    bool IsSubAsset = false;
};

class IProjectAssetsUI : public GameSprout::IGameSproutView {
public:
    virtual ~IProjectAssetsUI() {}
    // 主线程；返回身份快照，不借用资源对象。
    virtual std::vector<ProjectAssetSelection> GetSelection() const = 0;
    virtual bool NavigateToDirectory(const std::string & path) = 0;
};

#endif //__IProjectAssetsUI_h__
