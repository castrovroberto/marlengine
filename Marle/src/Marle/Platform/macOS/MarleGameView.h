#pragma once

#ifdef MRL_PLATFORM_MACOS
#import <Cocoa/Cocoa.h>

// Forward declaration to avoid including Application.h in the header
namespace Marle {
    class Application;
}

@interface MarleGameView : NSOpenGLView

- (instancetype)initWithFrame:(NSRect)frameRect 
                  pixelFormat:(NSOpenGLPixelFormat*)format 
                  application:(Marle::Application*)app;

@end

#endif 