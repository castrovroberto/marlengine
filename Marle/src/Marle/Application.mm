#include "mrlpch.h"
#include "Application.h"

#ifdef MRL_PLATFORM_MACOS
#define GL_SILENCE_DEPRECATION
#include <glad/gl.h>
#include <dlfcn.h>
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>

// Function to get OpenGL proc address on macOS
void* GetProcAddress(const char* name) {
    return dlsym(RTLD_DEFAULT, name);
}
#endif

namespace Marle {

    Application::Application(const WindowProps& props) 
        : m_WindowProps(props)
    #ifdef MRL_PLATFORM_MACOS
        , m_Window(nullptr), m_GLContext(nullptr)
    #endif
    {
        printf("Creating Marle Application: %s\n", m_WindowProps.Title);
        InitWindow();
        InitGraphics();
    }

    Application::~Application()
    {
        printf("Destroying Marle Application\n");
        ShutdownGraphics();
        ShutdownWindow();
    }

#ifdef MRL_PLATFORM_MACOS

    void Application::InitWindow()
    {
        printf("Initializing macOS window...\n");
        printf("Window properties: %s (%dx%d)\n", m_WindowProps.Title, m_WindowProps.Width, m_WindowProps.Height);
        
        // Initialize Cocoa application
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        
        // Create window
        NSRect windowRect = NSMakeRect(100, 100, m_WindowProps.Width, m_WindowProps.Height);
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | 
                                     NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        
        NSWindow* window = [[NSWindow alloc] initWithContentRect:windowRect
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        
        [window setTitle:[NSString stringWithUTF8String:m_WindowProps.Title]];
        [window makeKeyAndOrderFront:nil];
        [window center];
        
        m_Window = (void*)CFBridgingRetain(window);
        
        printf("macOS window initialization complete\n");
    }

    void Application::InitGraphics()
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
            m_Running = false;
            return;
        }
        
        // Create OpenGL context
        NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
        if (!context) {
            printf("Failed to create OpenGL context!\n");
            m_Running = false;
            return;
        }
        
        // Create OpenGL view and set it as window's content view
        NSOpenGLView* glView = [[NSOpenGLView alloc] initWithFrame:[window.contentView bounds] pixelFormat:pixelFormat];
        [glView setOpenGLContext:context];
        [window setContentView:glView];
        
        // Make context current
        [context makeCurrentContext];
        
        m_GLContext = (void*)CFBridgingRetain(context);
        
        // Initialize GLAD
        int version = gladLoadGL((GLADloadfunc)GetProcAddress);
        if (version == 0) {
            printf("Failed to initialize GLAD!\n");
            m_Running = false;
            return;
        }
        
        printf("GLAD Initialized. OpenGL Version: %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
        printf("  Vendor: %s\n", (const char*)glGetString(GL_VENDOR));
        printf("  Renderer: %s\n", (const char*)glGetString(GL_RENDERER));
        printf("  Version: %s\n", (const char*)glGetString(GL_VERSION));
        
        // Set initial OpenGL state
        glViewport(0, 0, m_WindowProps.Width, m_WindowProps.Height);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f); // Dark moody blue/gray
        
        printf("OpenGL context initialization complete\n");
    }

    void Application::ShutdownGraphics()
    {
        if (m_GLContext) {
            NSOpenGLContext* context = (__bridge NSOpenGLContext*)m_GLContext;
            [NSOpenGLContext clearCurrentContext];
            CFRelease(m_GLContext);
            m_GLContext = nullptr;
        }
        printf("OpenGL context shutdown complete\n");
    }

    void Application::ShutdownWindow()
    {
        if (m_Window) {
            NSWindow* window = (__bridge NSWindow*)m_Window;
            [window close];
            CFRelease(m_Window);
            m_Window = nullptr;
        }
        printf("macOS window shutdown complete\n");
    }

    void Application::Run()
    {
        printf("Application Run loop started (macOS)\n");
        
        NSWindow* window = (__bridge NSWindow*)m_Window;
        NSOpenGLContext* context = (__bridge NSOpenGLContext*)m_GLContext;
        
        while (m_Running)
        {
            // Process events
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) {
                if (event.type == NSEventTypeKeyDown && event.keyCode == 53) { // ESC key
                    m_Running = false;
                }
                [NSApp sendEvent:event];
            }
            
            // Check if window was closed
            if (![window isVisible]) {
                m_Running = false;
                break;
            }
            
            // Render
            [context makeCurrentContext];
            glClear(GL_COLOR_BUFFER_BIT);
            [context flushBuffer];
            
            // Sleep for ~16ms to simulate 60 FPS
            usleep(16000);
        }
        
        printf("Application Run loop ended\n");
    }

#else
    // Other platform stub implementation
    void Application::InitWindow()
    {
        printf("Window creation not implemented for this platform\n");
    }

    void Application::InitGraphics()
    {
        printf("Graphics initialization not implemented for this platform\n");
    }

    void Application::ShutdownGraphics()
    {
        printf("Graphics shutdown not implemented for this platform\n");
    }

    void Application::ShutdownWindow()
    {
        printf("Window shutdown (unknown platform)\n");
    }

    void Application::Run()
    {
        printf("Run loop not implemented for this platform\n");
        while(m_Running)
        {
            // Placeholder
            printf("Unknown platform run loop\n");
            usleep(16000);
        }
    }
#endif

}