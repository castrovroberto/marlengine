#pragma once

#include "Marle/Core.h"

// These imports should be included in a precompiled header.

/*
 * Standard library header <string>
 * This header is part of the strings library.
 * https://en.cppreference.com/w/cpp/header/string
 * 
/**/
#include <string>

/*
 * Standard library header <functional>
 * This header is part of the function objects library and provides the standard hash function.
 * https://en.cppreference.com/w/cpp/header/functional
 * 
/**/
#include <functional>

namespace Marle 
{
	/*
	 * Enum
     * Enumeration declaration
	 * https://en.cppreference.com/w/cpp/language/enum
	 * 
	 * An enumeration is a distinct type whose value is restricted to a range of values, 
	 * which may include several explicitly named constants ("enumerators"). 
	 * 
	 * The values of the constants are values of an integral type known as the 
	 * underlying type of the enumeration.
	 * 
	 * An number is assigned to each type of event.
	 * 
	/**/
    enum class EventType
    {
        None = 0,
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
        AppTick, AppUpdate, AppRender,
        KeyPressed, KeyReleased,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

	/*
	 * EventCategory enumerator declaration
	 * We might want to filter certain events by category.
	 * MouseEvents will enclose several EventTypes for example.
	 * 
	 * BIT defined in Marle/Core.h
	 * 
	 * #define BIT(x) (1 << x)
	 * 1 shifted left x places
	 * 
	/**/ 
    enum EventCategory
    {
        None = 0,
        EventCategoryApplication    = BIT(0),
        EventCategoryInput          = BIT(1),
        EventCategoryKeyboard       = BIT(2),
        EventCategoryMouse          = BIT(3),
        EventCategoryMouseButton    = BIT(4)
    };

	/*
	 * Macro that handles the retrieval of event types names.
	/**/
    #define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

    #define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

	/*
	 * Event
	 * Base class for events.
	 * 
	 * The Cherno:
	 * Events in Marle are currently blocking, meaning when an event occurs it immediately gets
	 * dispatched and must be dealt with right then and there.
	 * 
	 * For the future, a better strategy might be to buffer events in an event bus and process
	 * them during the "event" part of the update stage.
	 * 
	/**/
    class EXPORT Event
	{
		friend class EventDispatcher;
	    public:
			/*
		     * virtual function specifier
			 * Virtual functions are member functions whose behavior can be overridden in derived classes. 
			 * https://en.cppreference.com/w/cpp/language/virtual
			 * 
			/**/
		    virtual EventType GetEventType() const = 0;
		    virtual const char* GetName() const = 0;
		    virtual int GetCategoryFlags() const = 0;
		    virtual std::string ToString() const { return GetName(); }

		/*
		 * Utility function
		 * 
		 * Returns true when:
		 * The event belongs to a certain category.
		 * 
		/**/
		inline bool IsInCategory(EventCategory category)
		{
			return GetCategoryFlags() & category;
		}
        
        protected:
			/*
			 * m_Handled
			 * 
			 * Required to know if an event is handled or not.
			 * We might want to consume certain events and not all of them.
			 * 
			/**/
		    bool m_Handled = false;
	};

	class EventDispatcher
	{
		template<typename T>
		using EventFn = std::function<bool(T&)>;

	    public:
			EventDispatcher(Event& event) : m_Event(event)
			{
			
			}

			template<typename T>
			bool Dispatch(EventFn<T> func)
			{
				if (m_Event.GetEventType() == T::GetStaticType())
				{
					m_Event.m_Handled = func(*(T*)&m_Event);
					return true;
				}
				return false;
			}
		private:
			Event& m_Event;
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}