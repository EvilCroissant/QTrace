//
// Created by zgy on 2025/10/23.
//

#ifndef QBDI_QBDIHOOK_H
#define QBDI_QBDIHOOK_H

#include "vm.h"
#include <string>
#include <unordered_map>

typedef void (*TraceCallBack)(QBDI::VM *vm, QBDI::GPRState *gprState);

struct QBDIHookFunc {
    TraceCallBack callback;
    size_t *ignore;
    int ignorenum;
};

struct QBDIHookData {
    std::unordered_map<size_t, QBDIHookFunc*> hookMap;
};

extern QBDIHookData* _g_hook_data;

void initHookData();

void addHook(size_t addr,TraceCallBack callback);
void addHook(size_t addr,TraceCallBack callback,const size_t * ignorefrom,int ignorenum);
bool addNamedHook(size_t addr, const std::string& handler_name, std::string& error);

void addQBDIHook(size_t addr,QBDIHookFunc* callback);

// 兼容旧示例的别名
void hook_0x71DD54(QBDI::VM *vm, QBDI::GPRState *gprState);
void hook_base64(QBDI::VM *vm, QBDI::GPRState *gprState);
#endif //QBDI_QBDIHOOK_H
