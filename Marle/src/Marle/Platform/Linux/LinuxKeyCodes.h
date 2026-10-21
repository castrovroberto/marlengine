#pragma once

namespace Marle {
    // Translates a GLFW_KEY_* code to a Marle::Key value (Key::Unknown if unmapped).
    int TranslateGLFWKeyToMarle(int glfwKey);
}
