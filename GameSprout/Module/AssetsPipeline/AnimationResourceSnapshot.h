#pragma once
#include "AssetImportJob.h"
class IAssetsManager;
class ILibraryManager;
// Main-thread database access. Worker jobs receive value snapshots only.
void CaptureAnimationResources(AssetImportRequest&,IAssetsManager*,ILibraryManager*);

void ResolveAnimationSnapshotMismatch(const AssetImportJob&,IAssetsManager*,ILibraryManager*,AssetImportResult&);
