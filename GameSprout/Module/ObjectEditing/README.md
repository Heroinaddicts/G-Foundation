# ObjectEditing

ObjectEditing is the Editor property-command boundary. Views submit world/object/
component identities and value-capturing property operations through its public API.
It never saves a source file or owns Runtime nodes. SceneManager retains scene
snapshots, structure authoring, source persistence and author Undo/Redo.

In stable Edit mode commands delegate to SceneManager. In stable Playing or Paused
mode they address only the current transient world, without document Capture,
FileID allocation, author history or dirty-state changes. Stop restores the existing
SceneManager snapshot. Layer subtree commands collect runtime identities and old
values before writing; failed property setters attempt rollback of still-live targets.
Deleted objects/components are diagnosed rather than resurrected from author data.

Commands are synchronous on the owner thread. Runtime callbacks, Host frames, mode
transitions and notification reentry are rejected. Play property edits synchronize
physics configuration and poses without advancing time, scripts or collision events.
Completion events contain values only. These identities can later be validated by an
IPC endpoint; local setter closures are not a transport format.
