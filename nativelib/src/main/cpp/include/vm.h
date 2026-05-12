//
// Created by fang on 23-12-19.
//

#ifndef QBDIRECORDER_VM_H
#define QBDIRECORDER_VM_H
#include "QBDI.h"
#include "QBDI/State.h"
#include "TraceConfig.h"
#include "logger.h"
#include <sstream>
#include "shadowhook.h"

typedef void (*TraceCallBack)(QBDI::VM *vm, QBDI::GPRState *gprState);
typedef bool (*TraceFilter)(size_t regs[]);

struct TraceFunc {
    TraceCallBack callback;
};

class vm {
public:
    QBDI::VM init(size_t start,size_t end);
    size_t base;
private:

};
typedef void (*orig_func_t)(void*);

struct g_trace_data{
    size_t base;
    size_t start;
    size_t end;
    size_t target;
    void* hooktask;
    orig_func_t orig_addr;
};

extern g_trace_data* _g_trace_data;
extern int bufsize ;
extern bool debugInsn;
void setBufferSize(int);
void enableDebugInsn(bool);
void configureTraceFilter(const TraceFilterConfig& filter);
void clearTraceFilter();
bool shouldTraceArguments(const size_t regs[], size_t argc);

void sync_regs(size_t* regs,size_t pc,QBDI::GPRState* qbdi_state);

size_t trace_jni_onload(size_t jni,size_t jobj);

typedef size_t (*func_arg_0)();
extern func_arg_0 ori_arg0;
size_t hook_and_trace_arg0();

typedef size_t (*func_arg_1)(size_t x0);
extern func_arg_1 ori_arg1;
size_t hook_and_trace_arg1(size_t x0);

typedef size_t (*func_arg_2)(size_t x0,size_t x1);
extern func_arg_2 ori_arg2;
size_t hook_and_trace_arg2(size_t x0,size_t x1);

typedef size_t (*func_arg_3)(size_t x0,size_t x1,size_t x2);
extern func_arg_3 ori_arg3;
size_t hook_and_trace_arg3(size_t x0,size_t x1,size_t x2);

typedef size_t (*func_arg_4)(size_t x0,size_t x1,size_t x2,size_t x3);
extern func_arg_4 ori_arg4;
size_t hook_and_trace_arg4(size_t x0,size_t x1,size_t x2,size_t x3);


#endif //QBDIRECORDER_VM_H
