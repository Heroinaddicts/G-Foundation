#ifndef __ILibraryManager_h__
#define __ILibraryManager_h__

#include "GameSproutApi.h"
#include "ILocalDatabase.h"
#include "IAssetsPipeline.h"

class ILibraryManager : public GameSprout::IGameSproutModule {
public:
    virtual ~ILibraryManager() {}
    virtual ILDBSession * GetLDBSession() const = 0;
    virtual const std::string & GetLibraryRoot() const = 0;
    virtual std::vector<std::string> GetManifestGuids() const = 0;
    virtual bool GetManifest(const Guid & guid, AssetImportManifest & manifest) const = 0;
    // Stage lives below Library/Staging. Final paths are Library/Artifact/<Guid>_<LocalID>.gsasset.
    // Publication journals restore old files after an uncommitted replacement or crash.
    virtual bool CommitImport(const std::string & stage, AssetImportManifest & manifest, std::string & error) = 0;
    // 主线程同步执行；additionalWrites 与产物/依赖索引删除在同一事务提交。
    // AssetsManager 用它原子写备份、删除资源活动记录。
    virtual bool RemoveAsset(const Guid & guid, std::string & error, const std::vector<LDBWriteOperation> & additionalWrites = {}) = 0;
    virtual std::vector<std::string> FindDependents(const std::string & sourcePath) const = 0;
    // Borrowed until the next commit/removal for this GUID or Library release.
    virtual IAsset * QueryAsset(const Guid & guid, uint64_t localID) = 0;
};

#endif //__ILibraryManager_h__
