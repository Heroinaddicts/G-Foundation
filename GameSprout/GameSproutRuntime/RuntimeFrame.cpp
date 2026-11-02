#include "GameSproutRuntime.h"
#include "GameObject.h"
#include <exception>

namespace GameSproutRuntime {
bool Runtime::EndFrame(std::string& error) {
    error.clear();
    if(IsExecuting() || _ShuttingDown) {error="Runtime cannot end a frame inside a callback or traversal";return false;}
    // A loader may deliberately span several host operations. Its partial graph
    // remains configurable until the explicit construction scope is closed.
    if(_ConstructionDepth) return true;
    struct FrameScope {
        bool& Ending;
        explicit FrameScope(bool& ending):Ending(ending) {Ending=true;}
        ~FrameScope() {Ending=false;}
    } frame(_EndingFrame);
    try {
        FlushCommands();
        std::vector<std::uint64_t> roots;
        // Inspect only lifetime candidates, never scan all scene components.
        // Freeze this batch: objects created by OnRelease belong to the next one.
        const auto candidates=_DetachedObjects;
        for(const auto id:candidates) {
            auto* node=Find(id);
            if(!node || node->IsSceneRoot() || IsInScene(node)) {_DetachedObjects.erase(id);continue;}
            if(!node->Parent) roots.push_back(id);
        }
        {
            RuntimeOperation operation(*this);
            // Mark every orphan root before invoking any release callback, so a
            // sibling orphan cannot be resurrected from another root's release.
            for(const auto id:roots) if(auto* node=Find(id)) node->PendingDestroy=true;
            for(const auto id:roots) if(auto* node=Find(id)) DestroyNow(*node);
        }
        DispatchAnimationEvents();
        DispatchEvents();
        if(_ConstructionDepth) {
            _ConstructionDepth=0;
            error="A frame cleanup callback left an object construction scope open";
            return false;
        }
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="Runtime frame cleanup failed";}
    _ConstructionDepth=0;
    return false;
}
}
