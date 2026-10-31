#pragma once
#include <string>
#include <vector>

// Editor authoring settings. Times are seconds relative to the imported clip start.
struct AnimationImportEvent {
    float Time=0;
    std::string Name;
    std::string Payload;
};
struct AnimationClipImportOptions {
    // Model: exact source Take name. Native .anim: empty (one source clip).
    std::string Name;
    bool ExtractRootMotion=false;
    // Stable source node path; no automatic guessing from display names.
    std::string RootMotionNode;
    std::vector<AnimationImportEvent> Events;
};
