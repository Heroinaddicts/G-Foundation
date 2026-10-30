#ifndef __LibraryManager_h__
#define __LibraryManager_h__

#include "Header.h"
#include <map>
#include "IIDEWindow.h"
#include "ITaskApi.h"
#include <QLockFile>
#include <QDateTime>

class LibraryManager : public ILibraryManager {
public:
    virtual ~LibraryManager() {}

    const std::string & GetLibraryRoot() const override { return _LibraryRoot; }
    std::vector<std::string> GetManifestGuids() const override;
    bool GetManifest(const Guid & guid, AssetImportManifest & manifest) const override;
    bool CommitImport(const std::string & stage, AssetImportManifest & manifest, std::string & error) override;
    bool RemoveAsset(const Guid & guid, std::string & error, const std::vector<LDBWriteOperation> & additionalWrites = {}) override;
    std::vector<std::string> FindDependents(const std::string & sourcePath) const override;
    IAsset * QueryAsset(const Guid & guid, uint64_t localID) override;

    ILDBSession * GetLDBSession() const override {
        return _LDBSession;
    }

private:
    void BeforeInitialize(GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;

private:
    void StartGarbageCollection();
    bool RecoverPublications();
    bool PublishFlatImport(const std::string & stage, AssetImportManifest & manifest, std::string & error);
    Foundation::ITaskApi * _Tasks = nullptr;
    IIDEWindow * _IDE = nullptr;
    UInt64 _TaskOwner = Foundation::InvalidTaskOwnerId;
    size_t _CleanupTasks = 0;
    QDateTime _StartupTime;
    QLockFile * _Lock = nullptr;
    bool _PublicationBlocked = false;
    std::string _LibraryRoot;
    ILDBSession * _LDBSession = nullptr;
    std::map<std::string, std::vector<IAsset *>> _Loaded;
    void ClearLoaded(const std::string & guid);
    bool BuildIndexOperations(const AssetImportManifest * oldManifest,
        const AssetImportManifest * newManifest, std::vector<LDBWriteOperation> & operations);
};

#endif //__LibraryManager_h__
