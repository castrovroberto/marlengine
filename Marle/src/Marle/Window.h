#pragma once

#include "Core.h"

namespace Marle {
    
    struct WindowProps {
        const char* Title;
        unsigned int Width;
        unsigned int Height;

        WindowProps(const char* title = "Marle Engine",
                    unsigned int width = 1280,
                    unsigned int height = 720)
            : Title(title), Width(width), Height(height) {}
    };

} 