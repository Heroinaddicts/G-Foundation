# StaticAuthoring

StaticAuthoring owns the Editor capability policy for GameObject static declarations.
Runtime stores `StaticFlags`; SceneManager owns generic authoring transactions;
ObjectDocument persists node `StaticFlags` and `RootStaticFlags` in Scene/Prefab data.
The flags are independent bits, not a physics motion mode, Transform lock, script gate,
or a declaration that any baking result already exists.

The API defines seven known uses, but currently reports no supported baking
consumers. StaticUI omits unavailable uses from its menu and hides the entire Static
header control when none are available. When consumers become available, Nothing,
Everything and the checkbox operate only on the available subset, preserving other
stored bits. The authoring API allows clearing stored declarations; preserving
already-set bits is a no-op, and enabling an unsupported bit on any descendant rejects
the complete operation before a scene transaction starts.

Physics static colliders are already supported independently: a Collider with no
attached enabled Rigidbody uses a static Jolt body. An attached Rigidbody selects
dynamic or kinematic motion. StaticFlags do not override that ownership or motion
type. No separate Physics Static flag or unimplemented baking effect is advertised.

`SetFlags` takes `values`, `changedFlags` and explicit `includeChildren`. SceneManager
updates only masked bits separately for each node, preserving unrelated flags, with
one Undo/Redo entry for a subtree. Runtime/Scene/source callbacks do not retain node
pointers. Play and reentrant Runtime calls are rejected. Views submit identities and
values; they never parse or modify scene JSON for this operation.

The current source schema accepts only uint32 values within the defined seven-bit
mask (0..127); omitted fields use the normal default zero. Undefined bits fail before
object construction. There are no legacy names, version migration or old-data readers.
The existing Prefab property machinery tracks StaticFlags, including save,
instantiation, source propagation, Override, Apply/Revert and deletion restoration.
