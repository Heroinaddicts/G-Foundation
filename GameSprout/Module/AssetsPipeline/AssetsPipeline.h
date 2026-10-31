#ifndef __AssetsPipeline_h__
#define __AssetsPipeline_h__

#include "Header.h"
#include "ITaskApi.h"
#include "IIDEWindow.h"
#include "IProject.h"
#include "ILibraryManager.h"
#include <map>
#include "AssetFileFingerprintCache.h"

class IAssetsManager;
class IShaderCompiler;

class AssetsPipeline : public IAssetsPipeline {
public:
    virtual ~AssetsPipeline() {}
    void ImportAsset(IAssetMeta * meta, const fImportCallback & callback, bool force = false) override;
    bool CanReuseAsset(IAssetMeta * meta) const override;
    uint64_t BeginImportBatch(uint64_t count) override;
    void EndImportBatch(uint64_t batch) override;
    void Invalidate(const Guid & guid) override;
    std::vector<std::string> FindSourceDependents(const std::string&) const override;
private:
    void BeforeInitialize(GameSproutModuleApi * api) override;
    bool Initialize(GameSproutModuleApi * api) override;
    bool Launch(GameSproutModuleApi * api) override;
    void LaunchFinished(GameSproutModuleApi * api) override;
    void Update(GameSproutModuleApi * api) override;
    void Release(GameSproutModuleApi * api) override;
    void SaveImportValidation(const struct AssetImportJob & job, const AssetImportResult & result);
    void RestoreFileFingerprints(const AssetImportManifest & manifest, const std::string & assetsRoot);
    void ImportAssetInternal(IAssetMeta*,const fImportCallback&,bool,uint64_t,bool);
    void ResumeAnimationImports();
    struct DeferredAnimationImport { std::string GuidString,Path; fImportCallback Callback;bool Force=false;uint64_t Batch=0;std::set<std::string> Dependencies; };
    std::map<std::string,DeferredAnimationImport> _DeferredAnimationImports;
    bool _ResumingAnimationImports=false,_Releasing=false;
    std::map<std::string,std::map<std::string,std::string>> _DeclaredAnimationDependencies;
    struct ImportCompletion;
    uint64_t RegisterImportProgress(const std::string & path);
    void FinishImportProgress(uint64_t batch, const std::string & path);
    void ClearImportProgress();
    Foundation::ITaskApi * _Tasks = nullptr;
    IIDEWindow * _IDE = nullptr;
    IProject * _Project = nullptr;
    IAssetsManager * _Assets = nullptr;
    IShaderCompiler * _ShaderCompiler = nullptr;
    ILibraryManager * _Library = nullptr;
    UInt64 _Owner = Foundation::InvalidTaskOwnerId;
    struct RunningImport { std::string GuidString; uint64_t Batch = 0; };
    struct ImportBatch {
        uint64_t Token = 0, Total = 0, Registered = 0, Completed = 0;
        bool RegistrationOpen = false;
    };
    std::map<UInt64, RunningImport> _Running;
    std::map<uint64_t, ImportBatch> _ImportBatches;
    std::vector<uint64_t> _BatchRegistration;
    uint64_t _NextImportBatch = 0;
    std::map<std::string, uint64_t> _Revisions;
    mutable AssetFileFingerprintCache _Fingerprints;
};

#endif //__AssetsPipeline_h__
