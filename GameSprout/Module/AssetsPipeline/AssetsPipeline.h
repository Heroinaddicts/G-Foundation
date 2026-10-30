#ifndef __AssetsPipeline_h__
#define __AssetsPipeline_h__

#include "Header.h"
#include "ITaskApi.h"
#include "IIDEWindow.h"
#include "IProject.h"
#include "ILibraryManager.h"
#include <map>

class IAssetsManager;
class IShaderCompiler;

class AssetsPipeline : public IAssetsPipeline {
public:
    virtual ~AssetsPipeline() {}
    void ImportAsset(IAssetMeta * meta, const fImportCallback & callback, bool force = false) override;
    void Invalidate(const Guid & guid) override;
private:
    void BeforeInitialize(GameSproutApi * api) override;
    bool Initialize(GameSproutApi * api) override;
    bool Launch(GameSproutApi * api) override;
    void LaunchFinished(GameSproutApi * api) override;
    void Update(GameSproutApi * api) override;
    void Release(GameSproutApi * api) override;
    Foundation::ITaskApi * _Tasks = nullptr;
    IIDEWindow * _IDE = nullptr;
    IProject * _Project = nullptr;
    IAssetsManager * _Assets = nullptr;
    IShaderCompiler * _ShaderCompiler = nullptr;
    ILibraryManager * _Library = nullptr;
    UInt64 _Owner = Foundation::InvalidTaskOwnerId;
    std::map<UInt64, std::string> _Running;
    std::map<std::string, uint64_t> _Revisions;
};

#endif //__AssetsPipeline_h__
