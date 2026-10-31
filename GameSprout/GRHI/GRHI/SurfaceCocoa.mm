#include "SurfaceCocoa.h"
#include <SDL3/SDL.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include <cmath>

namespace GRHI {
bool SyncCocoaSurfaceDrawable(void* handle,SDL_Window* window,bool& available,std::string& error) {
    available=false;error.clear();
    @autoreleasepool {
        if(!handle || !window || ![NSThread isMainThread]) {error="Cocoa surface synchronization requires a live host view on the main thread.";return false;}
        NSView* host=(NSView*)handle;
        if(!host.window || host.window.isMiniaturized || host.isHiddenOrHasHiddenAncestor)return true;
        const NSRect bounds=host.bounds;
        if(bounds.size.width<=0 || bounds.size.height<=0)return true;
        const NSInteger tag=static_cast<NSInteger>(SDL_GetNumberProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_COCOA_METAL_VIEW_TAG_NUMBER,0));
        NSView* metalView=nil;
        for(NSView* child in host.subviews) {
            if(child.tag==tag && [child.layer isKindOfClass:[CAMetalLayer class]]) {metalView=child;break;}
        }
        if(!metalView) {error="The SDL-owned Metal view is no longer attached to the surface host.";return false;}
        // SDL's native NSView autoresizes, but its Metal layer drawableSize only follows SDL
        // window-size events. Foreign child views can resize without such an NSWindow event.
        [CATransaction begin];[CATransaction setDisableActions:YES];
        if(!NSEqualRects(metalView.frame,bounds))metalView.frame=bounds;
        const NSSize logical=metalView.bounds.size;
        const NSSize backing=[metalView convertSizeToBacking:logical];
        if(!std::isfinite(backing.width) || !std::isfinite(backing.height) || backing.width<=0 || backing.height<=0) {
            [CATransaction commit];error="Invalid native Metal drawable dimensions.";return false;
        }
        CAMetalLayer* layer=(CAMetalLayer*)metalView.layer;
        const CGSize pixels=CGSizeMake(std::round(backing.width),std::round(backing.height));
        const CGFloat scale=backing.height/logical.height;
        if(layer.contentsScale!=scale)layer.contentsScale=scale;
        if(!CGSizeEqualToSize(layer.drawableSize,pixels))layer.drawableSize=pixels;
        [CATransaction commit];
        available=true;return true;
    }
}
}
