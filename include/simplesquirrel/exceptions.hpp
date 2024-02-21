#pragma once

#include <string>
#include <sstream>
#include "type.hpp"

namespace ssq {
    /**
    * @brief Raw exception
    * @ingroup simplesquirrel
    */
    class Exception: public std::exception {
    public:
        Exception() : message() {}
        Exception(HSQUIRRELVM vm, const std::string& msg) : message() {
            generate_message(vm, msg);
        }

        virtual const char* what() const throw() override {
            return message.c_str();
        }

    protected:
        void generate_message(HSQUIRRELVM vm, const std::string& msg);

    private:
        std::string message;
    };
    /**
    * @brief Not Found exception thrown if object with a given name does not exist
    * @ingroup simplesquirrel
    */
    class NotFoundException: public Exception {
    public:
        NotFoundException(HSQUIRRELVM vm, const std::string& msg) : Exception() {
            std::stringstream ss;
            ss << "Not found: " << msg;
            generate_message(vm, ss.str());
        }
    };
    /**
    * @brief Compile exception thrown during compilation
    * @ingroup simplesquirrel
    */
    class CompileException: public Exception {
    public:
        CompileException(HSQUIRRELVM vm, const std::string& msg) : Exception(vm, msg) {}
        CompileException(HSQUIRRELVM vm, const std::string& msg, const char* source, int line, int column) : Exception() {
            std::stringstream ss;
            ss << "Compile error at " << source << ":" << line << ":" << column << " " << msg;
            generate_message(vm, ss.str());
        }
    };
    /**
    * @brief Type exception thrown if casting between squirrel and C++ objects failed
    * @ingroup simplesquirrel
    */
    class TypeException: public Exception {
    public:
        TypeException(const std::string& msg, const char* expected, const char* got) : Exception() {
            std::stringstream ss;
            ss << "Type error " << msg << " expected: " << expected << " got: " << got;
            generate_message(nullptr, ss.str());
        }
    };
    /**
    * @brief Runtime exception thrown if something went wrong during execution
    * @ingroup simplesquirrel
    */
    class RuntimeException: public Exception {
    public:
        RuntimeException(HSQUIRRELVM vm, const std::string& msg) : Exception(vm, msg) {}
        RuntimeException(HSQUIRRELVM vm, const std::string& msg, const char* source, const char* func, int line) : Exception() {
            std::stringstream ss;
            ss << "Runtime error at (" << func << ") " << source << ":" << line << ": " << msg;
            generate_message(vm, ss.str());
        }
    };
}
