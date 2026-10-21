#include "MarleGameView.h"

#ifdef MRL_PLATFORM_MACOS
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "MacOSKeyCodes.h"
#import <Cocoa/Cocoa.h>

@implementation MarleGameView {
    const Marle::Window::EventCallbackFn* m_Callback;
}

- (instancetype)initWithFrame:(NSRect)frameRect 
                  pixelFormat:(NSOpenGLPixelFormat*)format 
                  eventCallback:(const Marle::Window::EventCallbackFn*)callback {
    self = [super initWithFrame:frameRect pixelFormat:format];
    if (self) {
        m_Callback = callback;
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
    if (!m_Callback || !*m_Callback) return;

    unsigned short nativeKeyCode = [event keyCode];
    int marleKeyCode = Marle::TranslateMacOSKeyCodeToMarle(nativeKeyCode);
    int repeatCount = [event isARepeat] ? 1 : 0;

    Marle::KeyPressedEvent keyPressedEvent(marleKeyCode, repeatCount);
    (*m_Callback)(keyPressedEvent);

    // Handle KeyTypedEvent
    NSString *characters = [event characters];
    if ([characters length] > 0) {
        // For simplicity, sending the marleKeyCode for now
        // TODO: In the future, KeyTypedEvent should handle actual character data
        Marle::KeyTypedEvent keyTypedEvent(marleKeyCode);
        (*m_Callback)(keyTypedEvent);
    }
}

- (void)keyUp:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;

    unsigned short nativeKeyCode = [event keyCode];
    int marleKeyCode = Marle::TranslateMacOSKeyCodeToMarle(nativeKeyCode);

    Marle::KeyReleasedEvent keyReleasedEvent(marleKeyCode);
    (*m_Callback)(keyReleasedEvent);
}

- (void)flagsChanged:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;

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
                (*m_Callback)(e);
            } else {
                Marle::KeyReleasedEvent e(m.keyCode);
                (*m_Callback)(e);
            }
        }
    }

    s_PreviousFlags = current;
}

- (void)mouseMoved:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    Marle::MouseMovedEvent e((float)loc.x, (float)loc.y);
    (*m_Callback)(e);
}

- (void)mouseDragged:(NSEvent *)event      { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }

- (void)mouseDown:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;
    Marle::MouseButtonPressedEvent e(0);
    (*m_Callback)(e);
}

- (void)mouseUp:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;
    Marle::MouseButtonReleasedEvent e(0);
    (*m_Callback)(e);
}

- (void)rightMouseDown:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;
    Marle::MouseButtonPressedEvent e(1);
    (*m_Callback)(e);
}

- (void)rightMouseUp:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;
    Marle::MouseButtonReleasedEvent e(1);
    (*m_Callback)(e);
}

- (void)scrollWheel:(NSEvent *)event {
    if (!m_Callback || !*m_Callback) return;
    Marle::MouseScrolledEvent e((float)[event scrollingDeltaX], (float)[event scrollingDeltaY]);
    (*m_Callback)(e);
}

@end

#endif 