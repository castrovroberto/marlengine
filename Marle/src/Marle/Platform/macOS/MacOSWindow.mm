#include "mrlpch.h"
#include "MacOSWindow.h"
#include "Marle/Events/ApplicationEvent.h"

#define GL_SILENCE_DEPRECATION
#include <glad/gl.h>
#include <dlfcn.h>
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#include "MarleGameView.h"

@interface MarleWindowDelegate : NSObject <NSWindowDelegate>
- (instancetype)initWithWindowData:(Marle::MacOSWindow::WindowData*)data;
@end

@implementation MarleWindowDelegate {
    Marle::MacOSWindow::WindowData* m_Data;
}

- (instancetype)initWithWindowData:(Marle::MacOSWindow::WindowData*)data {
    self = [super init];
    if (self) m_Data = data;
    return self;
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    if (m_Data->EventCallback) {
        Marle::WindowFocusEvent event;
        m_Data->EventCallback(event);
    }
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (m_Data->EventCallback) {
        Marle::WindowLostFocusEvent event;
        m_Data->EventCallback(event);
    }
}

- (void)windowDidResize:(NSNotification*)notification {
    NSWindow* window = [notification object];
    NSRect contentRect = [window contentRectForFrameRect:[window frame]];
    m_Data->Width = (unsigned int)contentRect.size.width;
    m_Data->Height = (unsigned int)contentRect.size.height;
    if (m_Data->EventCallback) {
        Marle::WindowResizeEvent event(m_Data->Width, m_Data->Height);
        m_Data->EventCallback(event);
    }
}

@end

// Function to get OpenGL proc address on macOS
static void* GetProcAddress(const char* name) {
    return dlsym(RTLD_DEFAULT, name);
}

namespace Marle {

    std::unique_ptr<Window> Window::Create(const WindowProps& props)
    {
        auto window = std::make_unique<MacOSWindow>(props);
        if (!window->IsValid()) return nullptr;
        return window;
    }

    MacOSWindow::MacOSWindow(const WindowProps& props)
    {
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        if (InitWindow(props)) InitGraphics();
    }

    MacOSWindow::~MacOSWindow()
    {
        if (m_GLContext) {
            [NSOpenGLContext clearCurrentContext];
            CFRelease(m_GLContext);
            m_GLContext = nullptr;
        }
        if (m_WindowDelegate) {
            CFRelease(m_WindowDelegate);
            m_WindowDelegate = nullptr;
        }
        if (m_Window) {
            NSWindow* window = (__bridge NSWindow*)m_Window;
            [window close];
            CFRelease(m_Window);
            m_Window = nullptr;
        }
        printf("macOS window shutdown complete\n");
    }

    bool MacOSWindow::InitWindow(const WindowProps& props)
    {
        printf("Initializing macOS window...\n");
        printf("Window properties: %s (%dx%d)\n", props.Title, props.Width, props.Height);
        
        // Initialize Cocoa application
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        
        // Create window
        NSRect windowRect = NSMakeRect(100, 100, props.Width, props.Height);
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | 
                                     NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        
        NSWindow* window = [[NSWindow alloc] initWithContentRect:windowRect
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        
        [window setTitle:[NSString stringWithUTF8String:props.Title]];
        [window makeKeyAndOrderFront:nil];
        [window center];

        // Receive windowDidResize: notifications
        MarleWindowDelegate* delegate = [[MarleWindowDelegate alloc] initWithWindowData:&m_Data];
        [window setDelegate:delegate];
        m_WindowDelegate = (void*)CFBridgingRetain(delegate);

        m_Window = (void*)CFBridgingRetain(window);
        
        printf("macOS window initialization complete\n");
        return true;
    }

    bool MacOSWindow::InitGraphics()
    {
        printf("Initializing OpenGL context...\n");
        
        NSWindow* window = (__bridge NSWindow*)m_Window;
        
        // Create OpenGL pixel format
        NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFADepthSize, 24,
            NSOpenGLPFAStencilSize, 8,
            NSOpenGLPFAColorSize, 32,
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
            0
        };
        
        NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!pixelFormat) {
            printf("Failed to create OpenGL pixel format!\n");
            return false;
        }
        
        // Create OpenGL context
        NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
        if (!context) {
            printf("Failed to create OpenGL context!\n");
            return false;
        }
        
        // Create custom MarleGameView and set it as window's content view
        MarleGameView* glView = [[MarleGameView alloc] initWithFrame:[window.contentView bounds] 
                                                         pixelFormat:pixelFormat 
                                                       eventCallback:&m_Data.EventCallback];
        [glView setOpenGLContext:context];
        [window setContentView:glView];
        
        // Make context current
        [context makeCurrentContext];
        
        m_GLContext = (void*)CFBridgingRetain(context);
        
        // Initialize GLAD
        int version = gladLoadGL((GLADloadfunc)GetProcAddress);
        if (version == 0) {
            printf("Failed to initialize GLAD!\n");
            CFRelease(m_GLContext);
            m_GLContext = nullptr;
            return false;
        }
        
        printf("GLAD Initialized. OpenGL Version: %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
        printf("OpenGL context initialization complete\n");
        return true;
    }

    void MacOSWindow::OnUpdate()
    {
        NSWindow* window = (__bridge NSWindow*)m_Window;
        NSOpenGLContext* context = (__bridge NSOpenGLContext*)m_GLContext;

        // Process events
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event) {
            [NSApp sendEvent:event];
        }

        // Check if window was closed
        if (![window isVisible] && !m_CloseSent) {
            m_CloseSent = true;
            if (m_Data.EventCallback) {
                WindowCloseEvent closeEvent;
                m_Data.EventCallback(closeEvent);
            }
            return;
        }

        [context makeCurrentContext];
        [context flushBuffer];
    }

}
