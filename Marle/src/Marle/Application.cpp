#include "mrlpch.h"
#include "Application.h"

#include "Marle/Events/ApplicationEvent.h"
#include "Marle/Log.h"

namespace Marle {

    Application::Application() 
    {

    }

    Application::~Application()
    {

    }

    void Application::Run()
    {
        WindowResizeEvent e(1200,720);

        if(e.IsInCategory(EventCategoryApplication))
        {
            MRL_TRACE(e);  
        }
        if (e.IsInCategory(EventCategoryInput))
		{
			MRL_TRACE(e);
		}

        while(true);
    }

}