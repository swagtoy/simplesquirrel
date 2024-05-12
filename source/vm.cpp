#include <squirrel.h>
#include <sqstdstring.h>
#include <sqstdsystem.h>
#include <sqstdmath.h>
#include <sqstdaux.h>
#include <sqstdblob.h>
#include <sqstdio.h>
#include <forward_list>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <iostream>

#include "simplesquirrel/object.hpp"
#include "simplesquirrel/enum.hpp"
#include "simplesquirrel/vm.hpp"

static SQInteger squirrel_istream_read_char(SQUserPointer stream)
{
  std::istream* in = reinterpret_cast<std::istream*>(stream);

  const SQInteger c = in->get();
  if (in->eof())
    return 0;

  return c;
}

namespace ssq {
    VM* VM::get(HSQUIRRELVM vm) {
        SQUserPointer ptr = sq_getforeignptr(vm);
        if (!ptr) return nullptr;
        return static_cast<VM*>(ptr);
    }

    VM& VM::getMain(HSQUIRRELVM vm) {
        SQUserPointer ptr = sq_getsharedforeignptr(vm);
        assert(ptr);
        return *static_cast<VM*>(ptr);
    }

    VM::VM():Table(), foreignPtr(nullptr) {

    }

    VM::VM(size_t stackSize, uint32_t flags):Table(), foreignPtr(nullptr) {
        vm = sq_open(stackSize);
        sq_setforeignptr(vm, this);
        sq_setsharedforeignptr(vm, this);

        registerStdlib(flags);

        setPrintFunc(&VM::defaultPrintFunc, &VM::defaultErrorFunc);
        setRuntimeErrorFunc(&VM::defaultRuntimeErrorFunc);
        setCompileErrorFunc(&VM::defaultCompilerErrorFunc);

        sq_resetobject(&threadObj);
        sq_resetobject(&obj);
        sq_pushroottable(vm);
        sq_getstackobj(vm, -1, &obj);
        sq_addref(vm, &obj);
        sq_pop(vm, 1);
    }

    VM::VM(HSQUIRRELVM thread):Table(), foreignPtr(nullptr) {
        assert(VM::getMain(thread).getHandle() != thread); // Assert this is not the main VM

        vm = thread;

        sq_resetobject(&threadObj);
        sq_resetobject(&obj);
        sq_pushroottable(vm);
        sq_getstackobj(vm, -1, &obj);
        sq_addref(vm, &obj);
        sq_pop(vm, 1);
    }

    VM::VM(HSQOBJECT thread):Table(), foreignPtr(nullptr) {
        assert(thread._type == OT_THREAD);

        threadObj = thread;
        vm = thread._unVal.pThread;
        sq_setforeignptr(vm, this);

        sq_resetobject(&obj);
        sq_pushroottable(vm);
        sq_getstackobj(vm, -1, &obj);
        sq_addref(vm, &obj);
        sq_pop(vm, 1);
    }

    void VM::destroy() {
        classMap.clear();
        if (vm != nullptr) {
            sq_resetobject(&obj);

            HSQUIRRELVM main_vm = VM::getMain(vm).getHandle();
            if (vm == main_vm) // This is the main VM
                sq_close(vm);
            else if (!sq_isnull(threadObj)) // This is a thread, originating from simplesquirrel
                sq_release(main_vm, &threadObj);
        }
        vm = nullptr;
    }

    VM::~VM() {
        destroy();
    }

    void VM::swap(VM& other) NOEXCEPT {
        if (vm)
        {
          sq_setforeignptr(vm, &other);
          if (sq_getsharedforeignptr(vm) == this)
            sq_setsharedforeignptr(vm, &other);
        }
        if (other.vm)
        {
          sq_setforeignptr(other.vm, this);
          if (sq_getsharedforeignptr(other.vm) == &other)
            sq_setsharedforeignptr(other.vm, this);
        }

        using std::swap;
        Object::swap(other);
        swap(threadObj, other.threadObj);
        swap(runtimeException, other.runtimeException);
        swap(compileException, other.compileException);
        swap(classMap, other.classMap);
        swap(foreignPtr, other.foreignPtr);
    }
        
    VM::VM(VM&& other) NOEXCEPT :Table(), foreignPtr(nullptr) {
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

    void VM::setRootTable(Table& table) {
        sq_resetobject(&obj);
        sq_pushobject(vm, table.getRaw());
        sq_getstackobj(vm, -1, &obj);
        sq_addref(vm, &obj);
        sq_setroottable(vm);
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

    void VM::setStdErrorFunc() {
        sqstd_seterrorhandlers(vm);
    }

    void VM::setForeignPtr(void* ptr) {
        foreignPtr = ptr;
    }

    void* VM::getForeignPtr() const {
        return foreignPtr;
    }

    bool VM::isThread() const {
        return VM::getMain(vm).getHandle() != vm;
    }

    SQInteger VM::getState() const {
        return sq_getvmstate(vm);
    }

    SQInteger VM::getTop() const {
        return sq_gettop(vm);
    }

    Script VM::compileSource(const char* source, const char* name) {
        Script script(vm);
        if (SQ_FAILED(sq_compilebuffer(vm, source, strlen(source), name, true))) {
            if (!compileException)throw CompileException(vm, "Source cannot be compiled!");
            throw *compileException;
        }

        sq_getstackobj(vm,-1,&script.getRaw());
        sq_addref(vm, &script.getRaw());
        sq_pop(vm, 1);
        return script;
    }

    Script VM::compileSource(std::istream& source, const char* name) {
        Script script(vm);
        if (SQ_FAILED(sq_compile(vm, squirrel_istream_read_char, &source, name, SQTrue))) {
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

    void VM::run(const Script& script) {
        if (script.isEmpty()) {
            throw RuntimeException(vm, "Empty script object.");
        }

        const SQInteger old_top = sq_gettop(vm);
        sq_pushobject(vm, script.getRaw());
        sq_pushroottable(vm);
        try {
            if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
                if (!runtimeException)
                    throw RuntimeException(vm, "Error running script!");
                throw *runtimeException;
            }

            // Root table may've changed
            sq_resetobject(&obj);
            sq_pushroottable(vm);
            sq_getstackobj(vm, -1, &obj);
            sq_addref(vm, &obj);
            sq_pop(vm, 1);

            if (sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
                sq_settop(vm, old_top);
            }
        }
        catch (const RuntimeException&) {
            sq_settop(vm, old_top);
            throw;
        }
        catch (...) {
            sq_settop(vm, old_top);
            throw RuntimeException(vm, "Running script failed!");
        }
    }

    Object VM::runAndReturn(const Script& script) {
        if (script.isEmpty()) {
            throw RuntimeException(vm, "Empty script object.");
        }

        const SQInteger old_top = sq_gettop(vm);
        sq_pushobject(vm, script.getRaw());
        sq_pushroottable(vm);
        try {
            if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
                if (!runtimeException)
                    throw RuntimeException(vm, "Error running script!");
                throw *runtimeException;
            }

            // Root table may've changed
            sq_resetobject(&obj);
            sq_pushroottable(vm);
            sq_getstackobj(vm, -1, &obj);
            sq_addref(vm, &obj);
            sq_pop(vm, 1);

            Object ret = detail::pop<Object>(vm, -1);
            sq_settop(vm, old_top);
            return ret;
        }
        catch (const RuntimeException&) {
            sq_settop(vm, old_top);
            throw;
        }
        catch (...) {
            sq_settop(vm, old_top);
            throw RuntimeException(vm, "Running script failed!");
        }
    }

    VM VM::newThread(size_t stackSize) const {
        HSQUIRRELVM thread = sq_newthread(vm, stackSize);
        if (!thread)
            throw RuntimeException(vm, "Failed to create thread!");

        HSQOBJECT threadObject;
        sq_resetobject(&threadObject);
        if (SQ_FAILED(sq_getstackobj(vm, -1, &threadObject)))
          throw RuntimeException(vm, "Failed to get Squirrel thread from stack!");
        sq_addref(vm, &threadObject);

        sq_pop(vm, 1); // pop thread
        return VM(threadObject);
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
        if(SQ_FAILED(sq_call(vm, 1 + nparams, SQTrue, SQTrue))) {
            sq_settop(vm, top);
            if (!runtimeException)
                throw RuntimeException(vm, "Error running script!");
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

        VM* ssq_vm = VM::get(vm);
        if (!ssq_vm) return 0;
        ssq_vm->runtimeException.reset(new RuntimeException(
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
        VM* ssq_vm = VM::get(vm);
        if (!ssq_vm) return;
        ssq_vm->compileException.reset(new CompileException(
            vm,
            desc,
            source,
            line,
            column
        ));
    }

    void VM::pushArgs() {

    }

    std::unordered_map<size_t, HSQOBJECT> VM::classMap = {};

    void VM::addClassObj(size_t hashCode, const HSQOBJECT& obj) {
        classMap[hashCode] = obj;
    }

    const HSQOBJECT& VM::getClassObj(size_t hashCode) {
        return classMap.at(hashCode);
    }

    namespace detail {
        void addClassObj(size_t hashCode, const HSQOBJECT& obj) {
            VM::addClassObj(hashCode, obj);
        }

        const HSQOBJECT& getClassObj(size_t hashCode) {
            return VM::getClassObj(hashCode);
        }
    }
}
