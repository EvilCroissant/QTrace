//
// Created by zgy on 2025/10/23.
//

#include "qbdihook.h"
#include "logger.h"
#include "HookUtils.h"
#include "jnitrace.h"
#include <cstdlib>
#include <cstring>

QBDIHookData*  _g_hook_data = nullptr;

void hook_base64(QBDI::VM *vm, QBDI::GPRState *gprState)
{
    size_t input = QBDI_GPR_GET(gprState,3);
    size_t len = QBDI_GPR_GET(gprState,4);
    char* hex = bytes_to_hex_string((char*)input,len);
    char* base64 = (char*) malloc(2 * len);
    memset(base64,0,2*len);
    base64_encode(base64,(uint8_t*)input,len);
    LOGE("base64:src:%p,raw:%s,res:%s",input,hex,base64);
    appendlogendl();
    appendformat("base64:src:%p,raw:%s,res:%s",input,hex,base64);
    appendlogendl();
    free(hex);
    free(base64);
}

void hook_0x71DD54(QBDI::VM *vm, QBDI::GPRState *gprState)
{
    hook_base64(vm, gprState);
}

void initHookData()
{
    _g_hook_data = new QBDIHookData();
    _g_hook_data->hookMap.reserve(10);
}

void addHook(size_t addr,TraceCallBack callback)
{
    auto* func_hook = new QBDIHookFunc();
    func_hook->callback = callback;
    func_hook->ignore = nullptr;
    addQBDIHook(addr,func_hook);
}

void addHook(size_t addr,TraceCallBack callback,const size_t * ignorefrom,int ignorenum)
{
    auto* func_hook = new QBDIHookFunc();
    func_hook->callback = callback;
    auto * ignores = (size_t *)malloc(sizeof(size_t)*ignorenum);
    for(int i=0;i<ignorenum;i++)
    {
        ignores[i] = ignorefrom[i];
    }
    func_hook->ignore = ignores;
    func_hook->ignorenum = ignorenum;
    addQBDIHook(addr,func_hook);
}

void addQBDIHook(size_t addr,QBDIHookFunc* callback)
{
    if(_g_hook_data == nullptr)
    {
        LOGE("qbdi hook: not init!");
        return;
    }
    auto it = _g_hook_data->hookMap.find(addr);
    if(it != _g_hook_data->hookMap.end())
    {
        LOGE("qbdi hook: %p has installed!",(void*)addr);
        return;
    }
    _g_hook_data->hookMap[addr] = callback;
    LOGE("qbdi hook: %p install",(void*)addr);
}

bool addNamedHook(size_t addr, const std::string& handler_name, std::string& error)
{
    TraceCallBack callback = nullptr;
    if (handler_name == "base64") {
        callback = hook_base64;
    }

    if (callback == nullptr) {
        error = "unsupported custom hook handler: " + handler_name;
        return false;
    }

    addHook(addr, callback);
    return true;
}
