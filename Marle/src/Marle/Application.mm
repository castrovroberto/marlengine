#include "mrlpch.h"
#include "Application.h"

#ifdef MRL_PLATFORM_MACOS
#define GL_SILENCE_DEPRECATION
#include <glad/gl.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>

// Function to get OpenGL proc address on macOS
void* GetProcAddress(const char* name) {
    return dlsym(RTLD_DEFAULT, name);
}

// Helper function to get current time in seconds
double GetCurrentTimeSeconds() {
    static mach_timebase_info_data_t timebaseInfo;
    static bool initialized = false;
    
    if (!initialized) {
        mach_timebase_info(&timebaseInfo);
        initialized = true;
    }
    
    uint64_t absoluteTime = mach_absolute_time();
    return (double)absoluteTime * timebaseInfo.numer / timebaseInfo.denom / 1000000000.0;
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
        
        // Initialize last frame time
    #ifdef MRL_PLATFORM_MACOS
        m_LastFrameTime = GetCurrentTimeSeconds();
    #else
        m_LastFrameTime = 0.0;
    #endif
    }

    Application::~Application()
    {
        printf("Destroying Marle Application\n");
        ShutdownGraphics();
        ShutdownWindow();
    }

    void Application::OnUpdate(double fixed_dt) {
        // Base implementation can be empty or provide some core engine update logic if needed in the future.
    }

    void Application::OnRender(double interpolation_alpha) {
        // This is where rendering commands specific to the application/game state will go.
        // For now, we'll keep the clear color and buffer swap in the main Run() loop,
        // but eventually, those might move into a more structured rendering system called from here.
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
            // 1. Process events
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
            
            // 2. Calculate Frame Time
            double current_time = GetCurrentTimeSeconds();
            double frame_time = current_time - m_LastFrameTime;
            m_LastFrameTime = current_time;

            // Optional: Clamp frame_time to avoid spiral of death if debugging or extreme stalls
            if (frame_time > 0.25) { // Max frame time step e.g. 0.25s
                frame_time = 0.25;
            }

            m_Accumulator += frame_time;

            // 3. Fixed Updates for Game Logic
            while (m_Accumulator >= m_FixedDeltaTime) {
                // --- Input Polling could go here if needed (e.g., IsKeyPressed) ---
                
                // --- Core Engine Subsystem Updates (e.g., Physics, Animation) ---
                // (None yet)

                // --- Game Specific Update ---
                OnUpdate(m_FixedDeltaTime); // Call the virtual update method

                m_Accumulator -= m_FixedDeltaTime;
                // m_TotalGameTime += m_FixedDeltaTime; // Optional: track total game time
            }

            // 4. Rendering
            double interpolation_alpha = m_Accumulator / m_FixedDeltaTime; // For smooth rendering between states

            // --- Clear Screen ---
            [context makeCurrentContext];
            glClear(GL_COLOR_BUFFER_BIT);

            // --- Call Game Specific Render ---
            OnRender(interpolation_alpha); // Call the virtual render method

            // --- UI Rendering / Debug Rendering could go here ---

            // --- Swap Buffers ---
            [context flushBuffer];
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