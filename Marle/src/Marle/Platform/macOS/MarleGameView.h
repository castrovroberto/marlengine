#pragma once

#ifdef MRL_PLATFORM_MACOS
#import <Cocoa/Cocoa.h>

#include "Marle/Window.h"

@interface MarleGameView : NSOpenGLView

- (instancetype)initWithFrame:(NSRect)frameRect 
                  pixelFormat:(NSOpenGLPixelFormat*)format 
                eventCallback:(const Marle::Window::EventCallbackFn*)callback;

@end

#endif 