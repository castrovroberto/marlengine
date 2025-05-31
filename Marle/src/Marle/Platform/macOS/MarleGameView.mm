#include "MarleGameView.h"

#ifdef MRL_PLATFORM_MACOS
#include "Marle/Application.h"
#include "Marle/Events/KeyEvent.h"
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
    
    // Handle modifier keys
    // This is called when modifier keys change state
    // For now, we'll just log it - proper implementation would track modifier state
    // and generate KeyPressed/KeyReleased events for modifiers
    NSEventModifierFlags flags = [event modifierFlags];
    
    // This is a simplified implementation
    // A complete implementation would track previous modifier state
    // and generate appropriate KeyPressed/KeyReleased events
    
    // TODO: Implement proper modifier key handling
}

@end

#endif 