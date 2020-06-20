#include "Core.h"

namespace Marle 
{

    class EXPORT Application
    {
        public:
            Application();
            virtual ~Application();
            void Run();
    };

    // to be defined in Client Application
    Application* CreateApplication();

}