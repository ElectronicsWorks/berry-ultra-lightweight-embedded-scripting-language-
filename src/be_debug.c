#include "be_debug.h"
#include "be_func.h"
#include "be_opcode.h"
#include "be_string.h"
#include "be_class.h"
#include "be_vm.h"
#include "be_vector.h"
#include "be_strlib.h"
#include "be_exec.h"
#include <stdio.h>
#include <string.h>

#ifndef INST_BUF_SIZE
#define INST_BUF_SIZE   96
#endif

#define logbuf(...)     snprintf(__lbuf, sizeof(__lbuf), __VA_ARGS__)

#define logfmt(...)                     \
    do {                                \
        char __lbuf[INST_BUF_SIZE];     \
        logbuf(__VA_ARGS__);            \
        be_writestring(__lbuf);         \
    } while (0)

#if BE_DEBUG_DUMP_LEVEL >= 1
static void print_inst(binstruction ins, int pc)
{
    char __lbuf[INST_BUF_SIZE];
    bopcode op = IGET_OP(ins);

    logbuf("%4d: ", pc);
    be_writestring(__lbuf);
    switch (op) {
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_LT: case OP_LE: case OP_EQ:
    case OP_NE:  case OP_GT:  case OP_GE: case OP_CONNECT:
    case OP_GETMBR: case OP_SETMBR:  case OP_GETMET:
    case OP_GETIDX: case OP_SETIDX: case OP_AND:
    case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR:
        logbuf("%s\tR%d\tR%d\tR%d", be_opcode2str(op), IGET_RA(ins), IGET_RKB(ins), IGET_RKC(ins));
        break;
    case OP_GETGBL: case OP_SETGBL:
        logbuf("%s\tR%d\tG:%d", be_opcode2str(op), IGET_RA(ins), IGET_Bx(ins));
        break;
    case OP_MOVE: case OP_SETSUPER: case OP_NEG: case OP_FLIP: case OP_IMPORT:
        logbuf("%s\tR%d\tR%d", be_opcode2str(op), IGET_RA(ins), IGET_RKB(ins));
        break;
    case OP_JMP:
        logbuf("%s\t\t[%d]", be_opcode2str(op), IGET_sBx(ins) + pc + 1);
        break;
    case OP_JMPT: case OP_JMPF:
        logbuf("%s\tR%d\t[%d]", be_opcode2str(op), IGET_RA(ins), IGET_sBx(ins) + pc + 1);
        break;
    case OP_LDINT:
        logbuf("%s\tR%d\t%d", be_opcode2str(op), IGET_RA(ins), IGET_sBx(ins));
        break;
    case OP_LDBOOL:
        logbuf("%s\tR%d\t%d\t%d", be_opcode2str(op),  IGET_RA(ins), IGET_RKB(ins), IGET_RKC(ins));
        break;
    case OP_RET:
        logbuf("%s\t%d\tR%d", be_opcode2str(op), IGET_RA(ins), IGET_RKB(ins));
        break;
    case OP_GETUPV: case OP_SETUPV:
        logbuf("%s\tR%d\tU:%d", be_opcode2str(op), IGET_RA(ins), IGET_Bx(ins));
        break;
    case OP_CALL:
        logbuf("%s\tR%d\t%d", be_opcode2str(op), IGET_RA(ins), IGET_RKB(ins));
        break;
    case OP_CLOSURE:
        logbuf("%s\tR%d\tP:%d", be_opcode2str(op), IGET_RA(ins), IGET_Bx(ins));
        break;
    case OP_CLOSE:
        logbuf("%s\t%d", be_opcode2str(op), IGET_RA(ins));
        break;
    case OP_RAISE:
        logbuf("%s\t%d\tR%d\tR%d", be_opcode2str(op), IGET_RA(ins), IGET_RKB(ins), IGET_RKC(ins));
        break;
    case OP_EXBLK:
        if (IGET_RA(ins)) {
            logbuf("%s\t%d\t%d", be_opcode2str(op), IGET_RA(ins), IGET_Bx(ins));
        } else {
            logbuf("%s\t%d\t[%d]", be_opcode2str(op), IGET_RA(ins), IGET_sBx(ins) + pc + 1);
        }
        break;
    case OP_CATCH:
        logbuf("%s\tR%d\t%d\t%d", be_opcode2str(op), IGET_RA(ins), IGET_RKB(ins), IGET_RKC(ins));
        break;
    default:
        logbuf("%s", be_opcode2str(op));
        break;
    }
    be_writestring(__lbuf);
    be_writenewline();
}
#endif

#if BE_DEBUG_DUMP_LEVEL >= 1
void be_dumpclosure(bclosure *cl)
{
    int pc;
    bproto *proto = cl->proto;
    binstruction *code = proto->code;
#if BE_DEBUG_RUNTIME_INFO
    blineinfo *lineinfo = proto->lineinfo;
#endif
    logfmt("source '%s', ", str(proto->source));
    logfmt("function '%s':\n", str(proto->name));
    for (pc = 0; pc < proto->codesize; pc++) {
#if BE_DEBUG_RUNTIME_INFO
        if (lineinfo && pc == lineinfo->endpc) {
            logfmt("; line: %d\n", lineinfo->linenumber);
            ++lineinfo;
        }
#endif
        print_inst(*code++, pc);
    }
}
#endif

static const char* sourceinfo(bvm *vm, char *buf, int depth)
{
#if BE_DEBUG_RUNTIME_INFO
    bstack *stack = &vm->tracestack;
    int size = be_stack_count(stack);
    bcallsnapshot *cf = be_vector_at(stack, size + depth);
    bproto *proto = cast(bclosure*, var_toobj(&cf->func))->proto;
    be_assert(proto != NULL);
    if (proto->lineinfo && proto->nlineinfo) {
        blineinfo *start = proto->lineinfo;
        blineinfo *it = start + proto->nlineinfo - 1;
        int pc = cast_int(cf->ip - proto->code);
        while (it > start && it->endpc > pc) {
            --it;
        }
        sprintf(buf, "%s:%d:", str(proto->source), it->linenumber);
        return buf;
    }
    return "<unknow source>:";
#else
    (void)vm; (void)buf; (void)depth;
    return "<unknow source>:";
#endif
}

static void patch_native(bvm *vm, int depth)
{
    int size = be_stack_count(&vm->tracestack);
    bcallsnapshot *cf = be_vector_at(&vm->tracestack, size + depth);
    if (depth == -1) {
        cf->ip = vm->ip;
    } else {
        cf->ip = cf[1].ip;
    }
}

static void tracestack(bvm *vm)
{
    bstack *stack = &vm->tracestack;
    int depth, size = be_stack_count(stack);
    be_writestring("stack traceback:\n");
    for (depth = 1; depth <= size; ++depth) {
        bcallsnapshot *cf = be_vector_at(stack, size - depth);
        if (var_isclosure(&cf->func)) {
            char buf[100];
            bclosure *cl = var_toobj(&cf->func);
            be_writestring("\t");
            be_writestring(sourceinfo(vm, buf, -depth));
            be_writestring(" in function `");
            be_writestring(str(cl->proto->name));
            be_writestring("`\n");
        } else {
            patch_native(vm, -depth);
            be_writestring("\t<native>: in native function\n");
        }
    }
}

void be_tracestack(bvm *vm)
{
    if (be_stack_count(&vm->tracestack)) {
        tracestack(vm);
    }
}

#if BE_DEBUG_DUMP_LEVEL >= 3
void be_debug_dump_stack(bvm *vm, int depth)
{

    bvalue *v = vm->top - 1;
    bvalue *end = depth > 0 ? v - depth : vm->stack - 1;
    while (v > end) {
        logfmt("S[%3d]: <0x%.8x>, type(%d)\n",
            v - vm->top, (int)var_toint(v), var_type(v));
        --v;
    }
}
#endif
