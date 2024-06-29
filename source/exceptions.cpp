#include "simplesquirrel/exceptions.hpp"

#include <iostream>

namespace ssq {
    void Exception::generate_message(HSQUIRRELVM vm, const std::string& msg) {
        std::ostringstream out;
        out << "A Squirrel exception occurred!";

        if (!vm) {
            out << " " << msg << " (no detailed info)";
            message = out.str();
            return;
        }

        out << "\nCall stack (most recent first):";

        SQStackInfos info;
        int i = 1;
        while (true) {
            if (SQ_FAILED(sq_stackinfos(vm, i, &info))) {
                // We have reached the end of the call stack
                break;
            } else {
                out << "\n" << info.source << " - " << info.funcname << " - " << info.line;
            }

            i++;
        }

        out << "\n" << msg << " (";

        const char* lasterr;
        sq_getlasterror(vm);
        if (sq_gettype(vm, -1) != OT_STRING)
            lasterr = "no detailed info";
        else
            sq_getstring(vm, -1, &lasterr);
        sq_pop(vm, 1);

        out << lasterr << ")";

        message = out.str();
    }
}
