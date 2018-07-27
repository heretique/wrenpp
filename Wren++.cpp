#include "Wren++.h"

#include <cassert>
#include <cstdlib>  // for malloc
#include <cstring>  // for strcmp, memcpy
#include <iostream>

namespace
{
struct BoundState
{
    std::unordered_map<std::size_t, WrenForeignMethodFn>     methods{};
    std::unordered_map<std::size_t, WrenForeignClassMethods> classes{};
};

WrenForeignMethodFn foreignMethodProvider(
    WrenVM* vm, const char* module, const char* className, bool isStatic, const char* signature)
{
    auto* boundState = static_cast<BoundState*>(wrenGetUserData(vm));
    auto  it = boundState->methods.find(wrenpp::detail::hashMethodSignature(module, className, isStatic, signature));
    if (it == boundState->methods.end())
    {
        return nullptr;
    }

    return it->second;
}

WrenForeignClassMethods foreignClassProvider(WrenVM* vm, const char* m, const char* c)
{
    auto* boundState = static_cast<BoundState*>(wrenGetUserData(vm));
    auto  it         = boundState->classes.find(wrenpp::detail::hashClassSignature(m, c));
    if (it == boundState->classes.end())
    {
        return WrenForeignClassMethods{nullptr, nullptr};
    }

    return it->second;
}

inline const char* errorTypeToString(WrenErrorType type)
{
    switch (type)
    {
        case WREN_ERROR_COMPILE:
            return "WREN_ERROR_COMPILE";
        case WREN_ERROR_RUNTIME:
            return "WREN_ERROR_RUNTIME";
        case WREN_ERROR_STACK_TRACE:
            return "WREN_ERROR_STACK_TRACE";
        default:
            assert(false);
            return "";
    }
}

char* loadModuleFnWrapper(WrenVM* /*vm*/, const char* mod)
{
    return wrenpp::VM::loadModuleFn(mod);
}

void writeFnWrapper(WrenVM* /*vm*/, const char* text)
{
    wrenpp::VM::writeFn(text);
}

void errorFnWrapper(WrenVM*, WrenErrorType type, const char* module, int line, const char* message)
{
    wrenpp::VM::errorFn(type, module, line, message);
}

void* reallocateFnWrapper(void* memory, std::size_t newSize)
{
    return wrenpp::VM::reallocateFn(memory, newSize);
}
}

namespace wrenpp
{
namespace detail
{
    void registerFunction(WrenVM*             vm,
                          const std::string&  mod,
                          const std::string&  cName,
                          bool                isStatic,
                          std::string         sig,
                          WrenForeignMethodFn function)
    {
        BoundState* boundState = static_cast<BoundState*>(wrenGetUserData(vm));
        std::size_t hash       = detail::hashMethodSignature(mod.c_str(), cName.c_str(), isStatic, sig.c_str());
        boundState->methods.insert(std::make_pair(hash, function));
    }

    void registerClass(WrenVM* vm, const std::string& mod, std::string cName, WrenForeignClassMethods methods)
    {
        BoundState* boundState = static_cast<BoundState*>(wrenGetUserData(vm));
        std::size_t hash       = detail::hashClassSignature(mod.c_str(), cName.c_str());
        boundState->classes.insert(std::make_pair(hash, methods));
    }
}

Value null = Value();

Value::Value(bool val)
    : _type{WREN_TYPE_BOOL}
    , _string{nullptr}
{
    set(val);
}

Value::Value(float val)
    : _type{WREN_TYPE_NUM}
    , _string{nullptr}
{
    set(val);
}

Value::Value(double val)
    : _type{WREN_TYPE_NUM}
    , _string{nullptr}
{
    set(val);
}

Value::Value(int val)
    : _type{WREN_TYPE_NUM}
    , _string{nullptr}
{
    set(val);
}

Value::Value(unsigned int val)
    : _type{WREN_TYPE_NUM}
    , _string{nullptr}
{
    set(val);
}

Value::Value(const char* str)
    : _type{WREN_TYPE_STRING}
    , _string{nullptr}
{
    _string = static_cast<char*>(VM::reallocateFn(nullptr, std::strlen(str) + 1));
    std::strcpy(_string, str);
}

Value::~Value()
{
    if (_string)
    {
        VM::reallocateFn(_string, 0u);
    }
}

Method::Method(VM* vm, WrenHandle* variable, WrenHandle* method)
    : _vm(vm)
    , _method(method)
    , _variable(variable)
{
}

Method::Method(Method&& other)
    : _vm(other._vm)
    , _method(other._method)
    , _variable(other._variable)
{
    other._vm       = nullptr;
    other._method   = nullptr;
    other._variable = nullptr;
}

Method::~Method()
{
    if (_vm)
    {
        assert(_method && _variable);
        wrenReleaseHandle(_vm->ptr(), _method);
        wrenReleaseHandle(_vm->ptr(), _variable);
    }
}

Method::operator bool() const
{
    return _variable != nullptr && _method != nullptr;
}

Method& Method::operator=(Method&& rhs)
{
    if (&rhs != this)
    {
        _vm           = rhs._vm;
        _method       = rhs._method;
        _variable     = rhs._variable;
        rhs._vm       = nullptr;
        rhs._method   = nullptr;
        rhs._variable = nullptr;
    }

    return *this;
}

ClassContext ModuleContext::beginClass(std::string c)
{
    return ClassContext(c, *this);
}

void ModuleContext::endModule()
{
}

ModuleContext& ClassContext::endClass()
{
    return _module;
}

ClassContext::ClassContext(std::string c, ModuleContext& mod)
    : _module(mod)
    , _class(c)
{
}

ClassContext& ClassContext::bindCFunction(bool isStatic, std::string signature, WrenForeignMethodFn function)
{
    detail::registerFunction(_module._vm, _module._name, _class, isStatic, signature, function);
    return *this;
}

/*
 * Returns the source as a heap-allocated string.
 * Uses malloc, because our reallocateFn is set to default:
 * it uses malloc, realloc and free.
 * */
LoadModuleFn VM::loadModuleFn = [](const char* mod) -> char* {
    std::string path(mod);
    path += ".wren";
    std::string source;
    try
    {
        source = wrenpp::detail::fileToString(path);
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
    char* buffer = static_cast<char*>(malloc(source.size()));
    assert(buffer != nullptr);
    memcpy(buffer, source.c_str(), source.size());
    return buffer;
};

WriteFn VM::writeFn = [](const char* text) -> void { std::cout << text; };

ErrorFn VM::errorFn = [](WrenErrorType type, const char* module_name, int line, const char* message) -> void {
    const char* typeStr = errorTypeToString(type);
    if (module_name)
    {
        std::cout << typeStr << " in " << module_name << ":" << line << "> " << message << std::endl;
    }
    else
    {
        std::cout << typeStr << "> " << message << std::endl;
    }
};

ReallocateFn VM::reallocateFn = std::realloc;

std::size_t VM::initialHeapSize = 0xA00000u;

std::size_t VM::minHeapSize = 0x100000u;

int VM::heapGrowthPercent = 50;

std::size_t VM::chunkSize = 0x500000u;

VM::VM()
    : _vm{nullptr}
{
    WrenConfiguration configuration{};
    wrenInitConfiguration(&configuration);
    configuration.reallocateFn        = reallocateFnWrapper;
    configuration.initialHeapSize     = initialHeapSize;
    configuration.minHeapSize         = minHeapSize;
    configuration.heapGrowthPercent   = heapGrowthPercent;
    configuration.bindForeignMethodFn = foreignMethodProvider;
    configuration.loadModuleFn        = loadModuleFnWrapper;
    configuration.bindForeignClassFn  = foreignClassProvider;
    configuration.writeFn             = writeFnWrapper;
    configuration.errorFn             = errorFnWrapper;
    configuration.userData            = new BoundState();

    _vm = wrenNewVM(&configuration);
}

VM::VM(VM&& other)
    : _vm{other._vm}
{
    other._vm = nullptr;
}

VM& VM::operator=(VM&& rhs)
{
    if (&rhs != this)
    {
        _vm     = rhs._vm;
        rhs._vm = nullptr;
    }
    return *this;
}

VM::~VM()
{
    if (_vm != nullptr)
    {
        delete static_cast<BoundState*>(wrenGetUserData(_vm));
        wrenFreeVM(_vm);
    }
}

Result VM::executeModule(const std::string& mod)
{
    const std::string source(loadModuleFn(mod.c_str()));
    auto              res = wrenInterpret(_vm, mod.c_str(), source.c_str());

    if (res == WrenInterpretResult::WREN_RESULT_COMPILE_ERROR)
    {
        return Result::CompileError;
    }

    if (res == WrenInterpretResult::WREN_RESULT_RUNTIME_ERROR)
    {
        return Result::RuntimeError;
    }

    return Result::Success;
}

Result VM::executeString(const std::string& module, const std::string& str)
{
    auto res = wrenInterpret(_vm, module.c_str(), str.c_str());

    if (res == WrenInterpretResult::WREN_RESULT_COMPILE_ERROR)
    {
        return Result::CompileError;
    }

    if (res == WrenInterpretResult::WREN_RESULT_RUNTIME_ERROR)
    {
        return Result::RuntimeError;
    }

    return Result::Success;
}

void VM::collectGarbage()
{
    wrenCollectGarbage(_vm);
}

Method VM::method(const std::string& mod, const std::string& var, const std::string& signature)
{
    wrenEnsureSlots(_vm, 1);
    wrenGetVariable(_vm, mod.c_str(), var.c_str(), 0);
    WrenHandle* variable = wrenGetSlotHandle(_vm, 0);
    WrenHandle* handle   = wrenMakeCallHandle(_vm, signature.c_str());
    return Method(this, variable, handle);
}

Method VM::method(WrenHandle* variable, const std::string& signature)
{
    WrenHandle* handle = wrenMakeCallHandle(_vm, signature.c_str());
    return Method(this, variable, handle);
}

ModuleContext VM::beginModule(std::string name)
{
    return ModuleContext(_vm, name);
}
}
