#pragma once

#include "Marle/Core.h"

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
		/*
		 * friend 
		 * friend class declaration
		 * https://en.cppreference.com/w/cpp/language/friend
		 * 
		 * The friend declaration appears in a class body and grants a function 
		 * or another class access to private and protected members of the class 
		 * where the friend declaration appears.
		 * 
		/**/
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
		/*
		 * Function template
		 * A function template defines a family of functions.
		 * https://en.cppreference.com/w/cpp/language/function_template
		 * 
		 * Function templates are special functions that can operate with generic types. 
		 * This allows us to create a function template whose functionality can be adapted 
		 * to more than one type or class without repeating the entire code for each type.
		 * 
		 * In C++ this can be achieved using template parameters. 
		 * A template parameter is a special kind of parameter that can be used to pass a type as argument: 
		 * just like regular function parameters can be used to pass values to a function, 
		 * template parameters allow to pass also types to a function. 
		 * These function templates can use these parameters as if they were any other regular type.
		 * 
		 * The format for declaring function templates with type parameters is:
		 * 		template <class identifier> function_declaration;
		 * 		template <typename identifier> function_declaration;
		 * 
		 * The only difference between both prototypes is the use of either the keyword class or the keyword typename. 
		 * Its use is indistinct, since both expressions have exactly the same meaning and behave exactly the same way.
		 * 
		/**/
		
		/*
		 * using EventFn
		 * Type alias, alias template (since C++11)
		 * Type alias is a name that refers to a previously defined type (similar to typedef).
		 * Alias template is a name that refers to a family of types.
		 * https://en.cppreference.com/w/cpp/language/type_alias
		 * 
		/**/

		/*
		 * std::function
		 * Defined in header <functional>
		 * Class template std::function is a general-purpose polymorphic function wrapper.
		 * https://en.cppreference.com/w/cpp/utility/functional/function
		 * 
		 * template< class R, class... Args >
		 * class function<R(Args...)>;
		 * 
		 * In my own words: 
		 * Is a template function recieves a class/typename T reference aliased as EventFn
		 * or event function. And returns a boolean type.
		 * 
		 * Event dispatcher class will be instantiated with an Event
		 * Then if the event is dispatched the dispatcher will know if 
		 * the event got handled by a certain function type.
		 * 
		/**/
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
					/*
					 * This line is intended to know if a dispatched event got handled 
					 * &m_Event -> address of m_Event
					 * T* -> pointer to Event object
					 * *(T*) -> Event object pointer pointed to by T*
					 * 
					 * '*(T*)&m_Event' is behaving in the same way as 'T&'
					 * 
					 * Basically it depends on the EventFn template declaration.
					 * 
					 * EventDispatcher can access m_Handled field because of 
					 * friend modifier declaration inside Event class.
					 * 
					/**/
					m_Event.m_Handled = func(*(T*)&m_Event);
					return true;
				}
				return false;
			}
		private:
			Event& m_Event;
	};

	/*
	 * std::ostream::operator<<
	 * Insert formatted output
	 * This operator (<<) applied to an output stream is known as insertion operator.
	 * 
	/**/
	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}