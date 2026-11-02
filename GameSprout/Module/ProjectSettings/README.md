# ProjectSettings

ProjectSettings owns project Tag definitions, fixed Layer slots and the global physics collision matrix. It loads before any Module enters Launch and supplies a value snapshot to the application-owned Runtime. No Qt or Editor types enter the Runtime project-settings API.

The current file is `<ProjectRoot>/ProjectSettings/ProjectSettings.json`, Version 1. Missing settings create defaults; malformed existing settings fail initialization without replacement. Layers 0/1/2 are Default / Ignore Raycast / UI; remaining slots can be named or cleared without renumbering. Tags have stable IDs, Untagged is 0, and NextTagID never goes backwards or reuses deleted IDs.

SaveSettings validates the expected in-memory revision and the original disk bytes, rejects Play and reentrancy, checks removed tags against the SceneManager public document/Undo snapshot, live Runtime objects and saved `.scene` / `.prefab` source documents (including Prefab baselines/overrides), writes through QSaveFile, then publishes the same value snapshot to Runtime and synchronous listeners. A listener may unregister itself; newly registered listeners wait for the next event. No Update polling is used.

The collision matrix always stores 32 symmetric unsigned mask rows. Query masks and camera culling masks are separate settings over the same GameObject Layer IDs. This module does not simulate physics, render, migrate legacy formats, edit Scene properties or own UI widgets.
