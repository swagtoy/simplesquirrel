#include "simplesquirrel/object.hpp"
#include "simplesquirrel/enum.hpp"
#include "simplesquirrel/vm.hpp"
#include <squirrel.h>
#include <sqstdstring.h>
#include <sqstdsystem.h>
#include <sqstdmath.h>
#include <sqstdblob.h>
#include <sqstdio.h>
#include <forward_list>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <iostream>

namespace ssq {
    VM* VM::get(HSQUIRRELVM vm) {
        SQUserPointer ptr = sq_getsharedforeignptr(vm);
        assert(ptr);
        return static_cast<VM*>(ptr);
    }

    VM::VM(size_t stackSize, uint32_t flags):Table() {
        vm = sq_open(stackSize);
        sq_resetobject(&obj);
        sq_setsharedforeignptr(vm, this);

        registerStdlib(flags);

        setPrintFunc(&VM::defaultPrintFunc, &VM::defaultErrorFunc);
        setRuntimeErrorFunc(&VM::defaultRuntimeErrorFunc);
        setCompileErrorFunc(&VM::defaultCompilerErrorFunc);

        sq_pushroottable(vm);
        sq_getstackobj(vm,-1,&obj);
        sq_addref(vm, &obj);
        sq_pop(vm, 1);
    }

    void VM::destroy() {
        classMap.clear();
        if (vm != nullptr) {
            sq_resetobject(&obj);
            sq_close(vm);
        }
        vm = nullptr;
    }

    VM::~VM() {
        destroy();
    }

    void VM::swap(VM& other) NOEXCEPT {
        using std::swap;
        Object::swap(other);
        swap(runtimeException, other.runtimeException);
        swap(compileException, other.compileException);
        swap(classMap, other.classMap);
    }
        
    VM::VM(VM&& other) NOEXCEPT :Table() {
        swap(other);
    }

    void VM::registerStdlib(uint32_t flags) {
        if (flags == 0)return;
        sq_pushroottable(vm);
        if(flags & ssq::Libs::IO)
            sqstd_register_iolib(vm);
        if(flags & ssq::Libs::BLOB)
            sqstd_register_bloblib(vm);
        if(flags & ssq::Libs::MATH)
            sqstd_register_mathlib(vm);
        if(flags & ssq::Libs::SYSTEM)
            sqstd_register_systemlib(vm);
        if(flags & ssq::Libs::STRING)
            sqstd_register_stringlib(vm);
        sq_pop(vm, 1);
    }

    void VM::setPrintFunc(SqPrintFunc printFunc, SqErrorFunc errorFunc) {
        sq_setprintfunc(vm, printFunc, errorFunc);
    }

    void VM::setRuntimeErrorFunc(SqRuntimeErrorFunc runtimeErrorFunc) {
        sq_newclosure(vm, runtimeErrorFunc, 0);
        sq_seterrorhandler(vm);
    }

    void VM::setCompileErrorFunc(SqCompileErrorFunc compileErrorFunc) {
        sq_setcompilererrorhandler(vm, compileErrorFunc);
    }

    void VM::setForeignPtr(void* ptr) {
        foreignPtr = ptr;
    }

    void* VM::getForeignPtr() const {
        return foreignPtr;
    }

    SQInteger VM::getTop() const {
        return sq_gettop(vm);
    }

    Script VM::compileSource(const char* source, const char* name) {
        Script script(vm);
        if(SQ_FAILED(sq_compilebuffer(vm, source, strlen(source), name, true))){
            if (!compileException)throw CompileException(vm, "Source cannot be compiled!");
            throw *compileException;
        }

        sq_getstackobj(vm,-1,&script.getRaw());
        sq_addref(vm, &script.getRaw());
        sq_pop(vm, 1);
        return script;
    }

    Script VM::compileFile(const char* path) {
        Script script(vm);
        if (SQ_FAILED(sqstd_loadfile(vm, path, true))) {
            if (!compileException)throw CompileException(vm, "File not found or cannot be read!");
            throw *compileException;
        }

        sq_getstackobj(vm, -1, &script.getRaw());
        sq_addref(vm, &script.getRaw());
        sq_pop(vm, 1);
        return script;
    }

    void VM::run(const Script& script) const {
        if(!script.isEmpty()) {
            SQInteger top = sq_gettop(vm);
            sq_pushobject(vm, script.getRaw());
            sq_pushroottable(vm);
            SQRESULT result = sq_call(vm, 1, false, true);
            sq_settop(vm, top);
            if(SQ_FAILED(result)){
                throw *runtimeException;
            }
        }
        else {
            throw RuntimeException(vm, "Empty script object");
        }
    }

    Enum VM::addEnum(const char* name) {
        Enum enm(vm);
        sq_pushconsttable(vm);
        sq_pushstring(vm, name, strlen(name));
        detail::push<Object>(vm, enm);
        if(SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
            throw RuntimeException(vm, "Failed to add enumerator '" + std::string(name) + "'!");
        }
        sq_pop(vm,1); // pop table
        return std::move(enm);
    }

    VM& VM::operator = (VM&& other) NOEXCEPT {
        if(this != &other) {
            swap(other);
        }
        return *this;
    }

    Object VM::callAndReturn(SQUnsignedInteger nparams, SQInteger top) const {
        if(SQ_FAILED(sq_call(vm, 1 + nparams, true, true))){
            sq_settop(vm, top);
            if (runtimeException == nullptr)
                throw RuntimeException(vm, "Unknown squirrel runtime error");
            throw *runtimeException;
        }
            
        Object ret(vm);
        sq_getstackobj(vm, -1, &ret.getRaw());
        sq_addref(vm, &ret.getRaw());
        sq_settop(vm, top);
        return ret;
    }

    void VM::debugStack() const {
        auto top = getTop();
        while(top >= 0) {
            SQObjectType objectType = sq_gettype(vm, top);
            Type type = Type(objectType);
            std::cout << "stack index: " << top << " type: " << typeToStr(type) << std::endl;
            top--;
        }
    }

    void VM::defaultPrintFunc(HSQUIRRELVM vm, const SQChar *s, ...){
        va_list vl;
        va_start(vl, s);
        vprintf(s, vl);
        printf("\n");
        va_end(vl);
    }

    void VM::defaultErrorFunc(HSQUIRRELVM vm, const SQChar *s, ...){
        va_list vl;
        va_start(vl, s);
        fprintf(stderr, s, vl);
        fprintf(stderr, "\n");
        va_end(vl);
    }

    SQInteger VM::defaultRuntimeErrorFunc(HSQUIRRELVM vm) {
        SQStackInfos si;
        sq_stackinfos(vm, 1, &si);

        auto source = (si.source != nullptr ? si.source : "null");
        auto funcname = (si.funcname != nullptr ? si.funcname : "unknown");

        const SQChar *sErr = 0;
        if(sq_gettop(vm) >= 1){
            if(SQ_FAILED(sq_getstring(vm, 2, &sErr))){
                sErr = "unknown error";
            }
        }

        VM::get(vm)->runtimeException.reset(new RuntimeException(
            vm,
            sErr,
            source,
            funcname,
            si.line
        ));
        return 0;
    }

    void VM::defaultCompilerErrorFunc(
        HSQUIRRELVM vm,
        const SQChar* desc,
        const SQChar* source,
        SQInteger line,
        SQInteger column) {
        VM::get(vm)->compileException.reset(new CompileException(
            vm,
            desc,
            source,
            line,
            column
        ));
    }

    void VM::pushArgs() {

    }

	void VM::addClassObj(size_t hashCode, const HSQOBJECT& obj) {
		classMap[hashCode] = obj;
	}

	const HSQOBJECT& VM::getClassObj(size_t hashCode) {
		return classMap.at(hashCode);
	}

	namespace detail {
    void addClassObj(HSQUIRRELVM vm, size_t hashCode, const HSQOBJECT& obj) {
        VM::get(vm)->addClassObj(hashCode, obj);
    }

		const HSQOBJECT& getClassObj(HSQUIRRELVM vm, size_t hashCode) {
        return VM::get(vm)->getClassObj(hashCode);
    }
  }
}
