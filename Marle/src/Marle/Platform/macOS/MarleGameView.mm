#include "MarleGameView.h"

#ifdef MRL_PLATFORM_MACOS
#include "Marle/Application.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "MacOSKeyCodes.h"
#import <Cocoa/Cocoa.h>

@implementation MarleGameView {
    Marle::Application* m_Application;
}

- (instancetype)initWithFrame:(NSRect)frameRect 
                  pixelFormat:(NSOpenGLPixelFormat*)format 
                  application:(Marle::Application*)app {
    self = [super initWithFrame:frameRect pixelFormat:format];
    if (self) {
        m_Application = app;
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)canBecomeKeyView {
    return YES;
}

- (void)viewDidMoveToWindow {
    if ([self window]) {
        [[self window] makeFirstResponder:self];
        NSTrackingAreaOptions opts = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
        NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:opts owner:self userInfo:nil];
        [self addTrackingArea:area];
    }
}

- (void)keyDown:(NSEvent *)event {
    if (!m_Application) return;

    unsigned short nativeKeyCode = [event keyCode];
    int marleKeyCode = Marle::TranslateMacOSKeyCodeToMarle(nativeKeyCode);
    int repeatCount = [event isARepeat] ? 1 : 0;

    Marle::KeyPressedEvent keyPressedEvent(marleKeyCode, repeatCount);
    m_Application->OnEvent(keyPressedEvent);

    // Handle KeyTypedEvent
    NSString *characters = [event characters];
    if ([characters length] > 0) {
        // For simplicity, sending the marleKeyCode for now
        // TODO: In the future, KeyTypedEvent should handle actual character data
        Marle::KeyTypedEvent keyTypedEvent(marleKeyCode);
        m_Application->OnEvent(keyTypedEvent);
    }
}

- (void)keyUp:(NSEvent *)event {
    if (!m_Application) return;

    unsigned short nativeKeyCode = [event keyCode];
    int marleKeyCode = Marle::TranslateMacOSKeyCodeToMarle(nativeKeyCode);

    Marle::KeyReleasedEvent keyReleasedEvent(marleKeyCode);
    m_Application->OnEvent(keyReleasedEvent);
}

- (void)flagsChanged:(NSEvent *)event {
    if (!m_Application) return;

    static NSEventModifierFlags s_PreviousFlags = 0;
    NSEventModifierFlags current = [event modifierFlags];
    NSEventModifierFlags changed = current ^ s_PreviousFlags;

    struct { NSEventModifierFlags flag; int keyCode; } modifiers[] = {
        { NSEventModifierFlagShift,   Marle::Key::LeftShift   },
        { NSEventModifierFlagControl, Marle::Key::LeftControl },
        { NSEventModifierFlagOption,  Marle::Key::LeftAlt     },
        { NSEventModifierFlagCommand, Marle::Key::LeftSuper   },
    };

    for (auto& m : modifiers) {
        if (changed & m.flag) {
            if (current & m.flag) {
                Marle::KeyPressedEvent e(m.keyCode, 0);
                m_Application->OnEvent(e);
            } else {
                Marle::KeyReleasedEvent e(m.keyCode);
                m_Application->OnEvent(e);
            }
        }
    }

    s_PreviousFlags = current;
}

- (void)mouseMoved:(NSEvent *)event {
    if (!m_Application) return;
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    Marle::MouseMovedEvent e((float)loc.x, (float)loc.y);
    m_Application->OnEvent(e);
}

- (void)mouseDragged:(NSEvent *)event      { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }

- (void)mouseDown:(NSEvent *)event {
    if (!m_Application) return;
    Marle::MouseButtonPressedEvent e(0);
    m_Application->OnEvent(e);
}

- (void)mouseUp:(NSEvent *)event {
    if (!m_Application) return;
    Marle::MouseButtonReleasedEvent e(0);
    m_Application->OnEvent(e);
}

- (void)rightMouseDown:(NSEvent *)event {
    if (!m_Application) return;
    Marle::MouseButtonPressedEvent e(1);
    m_Application->OnEvent(e);
}

- (void)rightMouseUp:(NSEvent *)event {
    if (!m_Application) return;
    Marle::MouseButtonReleasedEvent e(1);
    m_Application->OnEvent(e);
}

- (void)scrollWheel:(NSEvent *)event {
    if (!m_Application) return;
    Marle::MouseScrolledEvent e((float)[event scrollingDeltaX], (float)[event scrollingDeltaY]);
    m_Application->OnEvent(e);
}

@end

#endif 