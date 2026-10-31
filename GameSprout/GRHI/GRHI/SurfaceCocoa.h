#pragma once
#include <string>
struct SDL_Window;
namespace GRHI {
// Private backend bridge; does not change the host view/window geometry or expose Cocoa publicly.
bool SyncCocoaSurfaceDrawable(void* hostView,SDL_Window* window,bool& available,std::string& error);
}
