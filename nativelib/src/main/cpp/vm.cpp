//
// Created by fang on 23-12-19.
//
#include "fstream"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <sstream>
#include <unordered_map>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <string>
#include <QBDI/Callback.h>
#include "vm.h"
#include "logger.h"
#include "qbdihook.h"
#include "jnitrace.h"
#include "libctrace.h"
#include "TraceLogger.h"
#include "TraceUtils.h"
#include "HookUtils.h"
#include "shadowhook.h"
using namespace std;
using namespace QBDI;
g_trace_data* _g_trace_data = nullptr;
int bufsize = 0x1000000;
bool debugInsn = false;
TraceFilterConfig g_trace_filter{};
func_arg_0 ori_arg0{};
func_arg_1 ori_arg1{};
func_arg_2 ori_arg2{};
func_arg_3 ori_arg3{};
func_arg_4 ori_arg4{};


void setBufferSize(int size)
{
    bufsize = size;
}

void enableDebugInsn(bool enable)
{
    debugInsn = enable;
}

void configureTraceFilter(const TraceFilterConfig& filter)
{
    g_trace_filter = filter;
}

void clearTraceFilter()
{
    g_trace_filter = {};
}

bool shouldTraceArguments(const size_t regs[], size_t argc)
{
    if (!g_trace_filter.enabled) {
        return true;
    }
    if (g_trace_filter.arg_index < 0 || static_cast<size_t>(g_trace_filter.arg_index) >= argc) {
        return false;
    }

    size_t current = regs[g_trace_filter.arg_index];
    switch (g_trace_filter.op) {
        case TraceFilterOp::Eq:
            return current == g_trace_filter.value;
        case TraceFilterOp::Ne:
            return current != g_trace_filter.value;
        case TraceFilterOp::Gt:
            return current > g_trace_filter.value;
        case TraceFilterOp::Ge:
            return current >= g_trace_filter.value;
        case TraceFilterOp::Lt:
            return current < g_trace_filter.value;
        case TraceFilterOp::Le:
            return current <= g_trace_filter.value;
        case TraceFilterOp::None:
        default:
            return true;
    }
}

void sync_regs(size_t* regs, size_t pc,QBDI::GPRState* qbdi_state)
{
    for(int i=0;i<31;i++)
    {
        QBDI_GPR_SET(qbdi_state,i,regs[i]);
    }
    qbdi_state->pc = pc;
}

static inline uintptr_t get_current_sp() {
    uintptr_t sp;
    __asm__ __volatile__("mov %0, sp" : "=r"(sp)); // 读取SP寄存器到sp变量
    return sp;
}

uintptr_t inline get_current_x30() {
    uintptr_t reg;
    __asm__ __volatile__("mov %0, x30" : "=r"(reg));
    return reg;
}

static void save_regs(size_t* regs)
{
    regs[0] = get_current_x0();
    regs[1] = get_current_x1();
    regs[2] = get_current_x2();
    regs[3] = get_current_x3();
    regs[4] = get_current_x4();
    regs[5] = get_current_x5();
    regs[6] = get_current_x6();
    regs[7] = get_current_x7();
    regs[8] = get_current_x8();
    regs[9] = get_current_x9();
    regs[10] = get_current_x10();
    regs[11] = get_current_x11();
    regs[12] = get_current_x12();
    regs[13] = get_current_x13();
    regs[14] = get_current_x14();
    regs[15] = get_current_x15();
    regs[16] = get_current_x16();
    regs[17] = get_current_x17();
    regs[18] = get_current_x18();
    regs[19] = get_current_x19();
    regs[20] = get_current_x20();
    regs[21] = get_current_x21();
    regs[22] = get_current_x22();
    regs[23] = get_current_x23();
    regs[24] = get_current_x24();
    regs[25] = get_current_x25();
    regs[26] = get_current_x26();
    regs[27] = get_current_x27();
    regs[28] = get_current_x28();
    regs[29] = get_current_x29();
    regs[30] = get_current_x30();
}

size_t trace(size_t regs[31])
{
    size_t function_address = _g_trace_data->start + _g_trace_data->target;
    LOGE("start trace:%p",(void*)function_address);
    shadowhook_unhook(_g_trace_data->hooktask);
    struct timeval start, end;
    gettimeofday(&start, nullptr);
    vm* vm_ = new vm();
    auto qvm = vm_->init(_g_trace_data->start,_g_trace_data->end);
    auto qbdi_state = qvm.getGPRState();
    initLogger(function_address);
    vm_->base = _g_trace_data->base;
    sync_regs(regs,function_address,qbdi_state);
    //在当前栈上开辟栈，不使用allocateVirtualStack，防止libart中Thread crash
    size_t sp = get_current_sp();
    size_t stack_size = 0x4000;
    QBDI_GPR_SET(qbdi_state, QBDI::REG_SP, sp - stack_size);
    QBDI_GPR_SET(qbdi_state, QBDI::REG_BP, QBDI_GPR_GET(qbdi_state, QBDI::REG_SP));
    QBDI::rword qbdi_retval;
    LOGE("trace begin");
    bool qbdi_success = qvm.call(&qbdi_retval, (uint64_t)function_address);
    LOGE("trace end");
    if (qbdi_success) {
        writelog();
        LOGE("trace completed successfully %s",_logger->logfile.c_str());
    } else {
        LOGE("trace failed");
    }
    gettimeofday(&end, nullptr);
    long elapsed_sec;
    elapsed_sec = (end.tv_sec - start.tv_sec) +
                  (end.tv_usec - start.tv_usec) / 1000000;
    LOGI("trace time cost:%lds",elapsed_sec);
    deleteLogger();
    delete vm_;
    return qbdi_retval;
}

size_t hook_and_trace_arg0()
{
    size_t regs[31] = {0};
    save_regs(regs);
    if (!shouldTraceArguments(regs, 0)) {
        return ori_arg0();
    }
    size_t result = trace(regs);
    _g_trace_data->hooktask = shadowhook_hook_func_addr((void*)(_g_trace_data->start + _g_trace_data->target),
                                                        (void*)(hook_and_trace_arg0),
                                                        (void**)&ori_arg0);
    return result;
}

size_t hook_and_trace_arg1(size_t x0)
{
    size_t regs[31] = {0};
    save_regs(regs);
    regs[0] = x0;
    if (!shouldTraceArguments(regs, 1)) {
        return ori_arg1(x0);
    }
    size_t result = trace(regs);
    _g_trace_data->hooktask = shadowhook_hook_func_addr((void*)(_g_trace_data->start + _g_trace_data->target),
                                                        (void*)(hook_and_trace_arg1),
                                                        (void**)&ori_arg1);
    return result;
}

size_t hook_and_trace_arg2(size_t x0,size_t x1)
{
    size_t regs[31] = {0};
    save_regs(regs);
    regs[0] = x0;
    regs[1] = x1;
    if (!shouldTraceArguments(regs, 2)) {
        return ori_arg2(x0, x1);
    }
    size_t result = trace(regs);
    _g_trace_data->hooktask = shadowhook_hook_func_addr((void*)(_g_trace_data->start + _g_trace_data->target),
                                                        (void*)(hook_and_trace_arg2),
                                                        (void**)&ori_arg2);
    return result;
}

size_t hook_and_trace_arg3(size_t x0,size_t x1,size_t x2)
{
    size_t regs[31] = {0};
    save_regs(regs);
    regs[0] = x0;
    regs[1] = x1;
    regs[2] = x2;
    if (!shouldTraceArguments(regs, 3)) {
        return ori_arg3(x0, x1, x2);
    }
    size_t result = trace(regs);
    _g_trace_data->hooktask = shadowhook_hook_func_addr((void*)(_g_trace_data->start + _g_trace_data->target),
                                                        (void*)(hook_and_trace_arg3),
                                                        (void**)&ori_arg3);
    return result;
}

size_t hook_and_trace_arg4(size_t x0,size_t x1,size_t x2,size_t x3)
{
    size_t regs[31] = {0};
    save_regs(regs);
    regs[0] = x0;
    regs[1] = x1;
    regs[2] = x2;
    regs[3] = x3;
    if (!shouldTraceArguments(regs, 4)) {
        return ori_arg4(x0, x1, x2, x3);
    }
    size_t result = trace(regs);
    _g_trace_data->hooktask = shadowhook_hook_func_addr((void*)(_g_trace_data->start + _g_trace_data->target),
                                                        (void*)(hook_and_trace_arg4),
                                                        (void**)&ori_arg4);
    return result;
}

size_t trace_jni_onload(size_t jni,size_t jobj)
{
    return hook_and_trace_arg2(jni,jobj);
}

bool checkAndCallHook(QBDI::VM *vm, QBDI::GPRState *gprState,size_t addr,size_t lastaddr)
{
    auto it = _g_hook_data->hookMap.find(addr);
    if(it != _g_hook_data->hookMap.end())
    {
        if(it->second->ignore != nullptr && it->second->ignorenum != 0)
        {
            for(int i=0;i<it->second->ignorenum;i++)
            {
                if(it->second->ignore[i] == lastaddr)
                {
                    return false;
                }
            }
        }
        it->second->callback(vm,gprState);
        return true;
    }
    return false;
}

bool checkLibcTrace_pre(QBDI::VM *vm, QBDI::GPRState *gprState,size_t target)
{
    auto it = _g_libc_trace->map.find(target);
    if(it != _g_libc_trace->map.end())
    {
        it->second->callback(vm,gprState);
        return true;
    }
    return false;
}

bool checkJniCall_pre(QBDI::VM *vm, QBDI::GPRState *gprState,size_t target)
{
    if(pJFunc == nullptr || _g_jni_trace == nullptr)
    {
        return false;
    }
    auto it = _g_jni_trace->map.find(target);
    if(it != _g_jni_trace->map.end())
    {
        it->second->callback(vm,gprState);
        return true;
    }
    return false;
}

static size_t lastAddr = 0;
// 显示指令执行前的寄存器状态
QBDI::VMAction showPreInstruction(QBDI::VM *vm, QBDI::GPRState *gprState, QBDI::FPRState *fprState, void *data)
{
    auto thiz = (class vm *)data;
    // 获取当前指令的分析信息
    const QBDI::InstAnalysis *instAnalysis = vm->getInstAnalysis(QBDI::ANALYSIS_INSTRUCTION | QBDI::ANALYSIS_DISASSEMBLY | QBDI::ANALYSIS_OPERANDS);
    appendlog("0x");
    appendformat("%lx",(instAnalysis->address-thiz->base));
    appendlog(": ");
    appendlog(instAnalysis->disassembly);
    bool hasCheck = false;
    //执行前hook
    hasCheck = checkAndCallHook(vm,gprState,(instAnalysis->address-thiz->base),(lastAddr - thiz->base));

    if(!hasCheck)
    {
        //检查blr
        if(strstr(instAnalysis->disassembly,"blr"))
        {
            for (int i = 0; i < instAnalysis->numOperands; ++i)
            {
                auto op = instAnalysis->operands[i];
                if (op.regAccess == QBDI::REGISTER_READ || op.regAccess == REGISTER_READ_WRITE)
                {
                    if (op.regCtxIdx != -1 && op.type == OPERAND_GPR && op.regCtxIdx != 31)
                    {
                        uint64_t regValue = QBDI_GPR_GET(gprState, op.regCtxIdx);
                        hasCheck = checkJniCall_pre(vm,gprState,regValue);
                        break;
                    }
                }
            }
        }
    }

    if(!hasCheck)
    {
        //检查br
        if(strstr(instAnalysis->disassembly,"br") && hasLibctrace())
        {
            for (int i = 0; i < instAnalysis->numOperands; ++i)
            {
                auto op = instAnalysis->operands[i];
                if (op.regAccess == QBDI::REGISTER_READ || op.regAccess == REGISTER_READ_WRITE)
                {
                    if (op.regCtxIdx != -1 && op.type == OPERAND_GPR && op.regCtxIdx != 31)
                    {
                        uint64_t regValue = QBDI_GPR_GET(gprState, op.regCtxIdx);
                        //br可能是libc调用，也可能是jni调用
                        if(!checkLibcTrace_pre(vm,gprState,regValue))
                        {
                            checkJniCall_pre(vm,gprState,regValue);
                        }
                        break;
                    }
                }
            }
        }
    }

    std::stringstream output;
    // 遍历操作数并记录读取的寄存器状态
    for (int i = 0; i < instAnalysis->numOperands; ++i)
    {
        auto op = instAnalysis->operands[i];
        if (op.regAccess == QBDI::REGISTER_READ || op.regAccess == REGISTER_READ_WRITE)
        {
            if (op.regCtxIdx != -1 && op.type == OPERAND_GPR)
            {
                uint64_t regValue = QBDI_GPR_GET(gprState, op.regCtxIdx);
                // 将寄存器名称和值添加到输出流
                output << op.regName << "=0x" << std::hex << regValue << " ";
                output.flush();
            }
        }
    }
    // 如果有读取的寄存器信息，格式化输出
    if (!output.str().empty())
    {
        appendlog("\t[");
        appendlog(output.str().c_str());
        appendlog("]");
        if(debugInsn)
        {
            LOGE("trace:0x%lx:%s [%s]",(instAnalysis->address-thiz->base) ,instAnalysis->disassembly,output.str().c_str());
        }
    }
    if(sdslen(_logger->buf) > bufsize)
    {
        writelog();
    }
    lastAddr = instAnalysis->address;
    return QBDI::VMAction::CONTINUE;
}

// 显示指令执行后的寄存器状态 打印字符串 hexdump
QBDI::VMAction showPostInstruction(QBDI::VM *vm, QBDI::GPRState *gprState, QBDI::FPRState *fprState, void *data)
{
    auto thiz = (class vm *)data;

    // 获取当前指令的分析信息，包括指令、符号、操作数等
    const QBDI::InstAnalysis *instAnalysis = vm->getInstAnalysis(QBDI::ANALYSIS_INSTRUCTION | QBDI::ANALYSIS_DISASSEMBLY | QBDI::ANALYSIS_OPERANDS);

    std::stringstream output;
    std::stringstream regOutput;

    // 遍历操作数并记录写入的寄存器状态
    for (int i = 0; i < instAnalysis->numOperands; ++i)
    {
        auto op = instAnalysis->operands[i];
        if (op.regAccess == REGISTER_WRITE || op.regAccess == REGISTER_READ_WRITE)
        {
            if (op.regCtxIdx != -1 && op.type == OPERAND_GPR)
            {
                // 获取寄存器值
                uint64_t regValue = QBDI_GPR_GET(gprState, op.regCtxIdx);
                // 输出寄存器名称和值
                output << op.regName << "=0x" << std::hex << regValue << " ";
                output.flush();
            }
        }
    }

    // 如果有写入的寄存器信息，格式化输出；否则，仅换行
    if (!output.str().empty())
    {
        appendlog("\t => [");
        appendlog(output.str().c_str());
        appendlog("]");
        appendlogendl();
    }
    else
    {
        appendlogendl();
        appendlog(regOutput.str().c_str());
    }
    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction showMemoryAccess(QBDI::VM *vm, QBDI::GPRState *gprState, QBDI::FPRState *fprState, void *data)
{
    auto thiz = (class vm *)data;
    if (vm->getInstMemoryAccess().empty())
    {
        appendlogendl();
    }
    for (const auto &acc : vm->getInstMemoryAccess())
    {
        std::ostringstream logStream;

        if (acc.type == MEMORY_READ)
        {
            logStream << "mem[r]: 0x" << std::hex << acc.accessAddress << " size: " << acc.size
                      << " value: 0x" << acc.value;
        }
        else if (acc.type == MEMORY_WRITE)
        {
            logStream << "mem[w]: 0x" << std::hex << acc.accessAddress << " size: " << acc.size
                      << " value: 0x" << acc.value;
        }
        else
        { // MEMORY_READ_WRITE
            logStream << "mem[rw]: 0x" << std::hex << acc.accessAddress << " size: " << acc.size
                      << " value: 0x" << acc.value;
        }
        // 输出日志
        appendlog(logStream.str().c_str());
        appendlogendl();
    }
    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction showSyscall(QBDI::VM *vm, QBDI::GPRState *gprState, QBDI::FPRState *fprState, void *data)
{
    const QBDI::InstAnalysis *instAnalysis = vm->getInstAnalysis(QBDI::ANALYSIS_INSTRUCTION);
    if (instAnalysis->mnemonic && strcasecmp(instAnalysis->mnemonic, "svc") == 0)
    {

    }
    return QBDI::VMAction::CONTINUE;
}

QBDI::VM vm::init(size_t start,size_t end)
{
    uint32_t cid;
    QBDI::GPRState *state;
    QBDI::VM qvm{};
    state = qvm.getGPRState();
    loadMemoryRanges();
    assert(state != nullptr);
    qvm.recordMemoryAccess(QBDI::MEMORY_READ_WRITE);

    //指令前hook
    cid = qvm.addCodeCB(QBDI::PREINST, showPreInstruction, this);
    assert(cid != QBDI::INVALID_EVENTID);

    //指令后hook
    cid = qvm.addCodeCB(QBDI::POSTINST, showPostInstruction, this);
    assert(cid != QBDI::INVALID_EVENTID);

    //TODO:syscall trace
    //cid = qvm.addCodeCB(QBDI::PREINST, showSyscall, this);
    //assert(cid != QBDI::INVALID_EVENTID);

    //读写trace
    cid = qvm.addMemAccessCB(MEMORY_READ_WRITE, showMemoryAccess, this);
    assert(cid != QBDI::INVALID_EVENTID);

    bool ret = qvm.addInstrumentedModuleFromAddr(reinterpret_cast<QBDI::rword>(start));
    if(!ret)
    {
        LOGE("init vm fail");
        assert(ret == true);
    }
    LOGE("init vm success");
    return qvm;
}
