/*
 * #pragma
 * Implementation defined behavior control
 * Implementation defined behavior is controlled by #pragma directive.
 * https://en.cppreference.com/w/cpp/preprocessor/impl
 * 
 * Pragma directive controls implementation-specific behavior of the compiler, 
 * such as disabling compiler warnings or changing alignment requirements. 
 * Any pragma that is not recognized is ignored.
 * 
 * The ISO C++ language standard does not require the compilers to support any pragmas. 
 * However, several non-standard pragmas are supported by multiple implementations.
 * 
 * #pragma once
 * If it appears in a header file, it indicates that it is only to be parsed once, 
 * even if it is (directly or indirectly) included multiple times in the same source file.
 * 
/**/
#pragma once

/*
 * #ifdef
 * Conditional inclusion
 * The preprocessor supports conditional compilation of parts of source file.
 * https://en.cppreference.com/w/cpp/preprocessor/conditional
 * 
 * Evaluation of MRL_PLATFORM_LINUX definition will depend on Core.h definition of variable.
 * 
/**/
#ifdef MRL_PLATFORM_LINUX

/*
 * extern Marle::Application* Marle::CreateApplication()
 * 
 * Language linkage
 * Provides for linkage between modules written in different programming languages.
 * https://en.cppreference.com/w/cpp/language/language_linkage
 * 
 * This comes useful when you have global variables.
 * You declare the existence of global variables in header, so that 
 * each source file that includes the header knows about it, but you
 * only need to define it once in one of your source files.
 * 
 * Using extern <type> <name> i.e. extern Marle::Application* Marle::CreateApplication()
 * Tells the compiler that an object of type Marle::Application* exists somewhere.
 * It's not the compilers job to know where it exists, it just need to know the type and name 
 * so it knows how to use it.
 * 
 * Once all of the source files have been compiled, 
 * the linker will resolve all of the references to the one definition 
 * that it finds in one of the compiled source files. 
 * 
 * For it to work, the definition of the Marle::CreateApplication pointer variable 
 * needs to have what's called “external linkage”, which basically means that 
 * it needs to be declared outside of a function (at what's usually called “the file scope”) 
 * and without the static keyword.
 *    
/**/
extern Marle::Application* Marle::CreateApplication();

int main(int argc, char** argv) {

    Marle::Log::Init();
    MRL_CORE_WARN("Initialized Log!");

    int a = 5;
    MRL_INFO("Hello! Var={0}", a);

    /*
     * auto app
     * 
     * Placeholder type specifiers (since C++11)
     * https://en.cppreference.com/w/cpp/language/auto
     * 
     * For functions, specifies that the return type will be deduced from its return statements. (since C++14)
     * In this case the variable app will be infered by the compiler
     * as a variable of Marle::Application* type.
     * 
    /**/
    auto app = Marle::CreateApplication();
    app->Run();
    delete app;
}

#endif