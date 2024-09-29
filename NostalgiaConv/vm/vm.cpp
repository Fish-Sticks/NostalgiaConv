#include <iostream>
#include <intrin.h>
#include <chrono>
#include "../update.hpp"
#include "vm.hpp"
extern "C" {
#include "../lua/lua.h"
#include "../lua/ldebug.h"
#include "../lua/ldo.h"
#include "../lua/lfunc.h"
#include "../lua/lgc.h"
#include "../lua/lmem.h"
#include "../lua/lobject.h"
#include "../lua/lopcodes.h"
#include "../lua/lparser.h"
#include "../lua/lstate.h"
#include "../lua/lstring.h"
#include "../lua/ltable.h"
#include "../lua/ltm.h"
#include "../lua/lundump.h"
#include "../lua/lvm.h"
#include "../lua/lzio.h"
}

// clua = custom lua

static CallInfo* cgrowCI(lua_State* L) {
    if (L->size_ci > LUAI_MAXCALLS)  /* overflow while handling overflow? */
        luaD_throw(L, LUA_ERRERR);
    else {
        luaD_reallocCI(L, 2 * L->size_ci);
        if (L->size_ci > LUAI_MAXCALLS)
            luaG_runerror(L, "stack overflow");
    }
    return ++L->ci;
}

static StkId cadjust_varargs(lua_State* L, Proto* p, int actual) {
    int i;
    int nfixargs = p->numparams;
    Table* htab = NULL;
    StkId base, fixed;
    for (; actual < nfixargs; ++actual)
        setnilvalue(L->top++);

    /* move fixed parameters to final position */
    fixed = L->top - actual;  /* first fixed argument */
    base = L->top;  /* final position of first argument */
    for (i = 0; i < nfixargs; i++) {
        setobjs2s(L, L->top++, fixed + i);
        setnilvalue(fixed + i);
    }
    /* add `arg' parameter */
    if (htab) {
        sethvalue(L, L->top++, htab);
        lua_assert(iswhite(obj2gco(htab)));
    }
    return base;
}

#define api_checknelems(L, n)	api_check(L, (n) <= (L->top - L->base))
#define api_checkvalidindex(L, i)	api_check(L, (i) != luaO_nilobject)
#define api_incr_top(L)   {api_check(L, L->top < L->ci->top); L->top++;}
#define adjustresults(L,nres) \
    { if (nres == LUA_MULTRET && L->top >= L->ci->top) L->ci->top = L->top; }
#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)))


#define inc_ci(L) \
  ((L->ci == L->end_ci) ? cgrowCI(L) : \
   (condhardstacktests(luaD_reallocCI(L, L->size_ci)), ++L->ci))


// Every read/write MUST be replicated to Roblox for perfect emulation. This is TOUGH.
// I need to include C functions too since, this VM is SHARED
int cluaD_precall(lua_State* L, StkId func, int nresults) {
    LClosure* cl;
    ptrdiff_t funcr;
    if (!ttisfunction(func)) /* `func' is not a function? */
        return 0;

    funcr = savestack(L, func);
    cl = &clvalue(func)->l;
    L->ci->savedpc = L->savedpc;
    if (!cl->isC) {  /* Lua function? prepare its call */
        CallInfo* ci;
        StkId st, base;
        Proto* p = cl->p;
        luaD_checkstack(L, p->maxstacksize);
        func = restorestack(L, funcr);
        if (!p->is_vararg) {  /* no varargs? */
            base = func + 1;
            if (L->top > base + p->numparams)
                L->top = base + p->numparams;
        }
        else {  /* vararg function */
            int nargs = cast_int(L->top - func) - 1;
            base = cadjust_varargs(L, p, nargs);
            func = restorestack(L, funcr);  /* previous call may change the stack */
        }
        ci = inc_ci(L);  /* now `enter' new function */
        ci->func = func;
        L->base = ci->base = base;
        ci->top = L->base + p->maxstacksize;
        lua_assert(ci->top <= L->stack_last);
        L->savedpc = p->code;  /* starting point */
        ci->tailcalls = 0;
        ci->nresults = nresults;
        for (st = L->top; st < ci->top; st++)
            setnilvalue(st);
        L->top = ci->top;
        if (L->hookmask & LUA_MASKCALL) {
            L->savedpc++;  /* hooks assume 'pc' is already incremented */
            luaD_callhook(L, LUA_HOOKCALL, -1);
            L->savedpc--;  /* correct 'pc' */
        }
        return PCRLUA;
    }
    else {  /* if is a C function, call it */
        CallInfo* ci;
        int n;
        luaD_checkstack(L, LUA_MINSTACK);  /* ensure minimum stack size */
        ci = inc_ci(L);  /* now `enter' new function */
        ci->func = restorestack(L, funcr);
        L->base = ci->base = ci->func + 1;
        ci->top = L->top + LUA_MINSTACK;
        lua_assert(ci->top <= L->stack_last);
        ci->nresults = nresults;
        if (L->hookmask & LUA_MASKCALL)
            luaD_callhook(L, LUA_HOOKCALL, -1);
        lua_unlock(L);
        n = (*curr_func(L)->c.f)(L);  /* do the actual call */
        lua_lock(L);
        if (n < 0)  /* yielding? */
            return PCRYIELD;
        else {
            luaD_poscall(L, L->top - n);
            return PCRC;
        }
    }
}

void cluaV_execute(std::uintptr_t RS, lua_State* L, int nexeccalls);
void cluaD_call(std::uintptr_t RS, lua_State* L, StkId func, int nResults) {
    if (++L->nCcalls >= LUAI_MAXCCALLS) {
        if (L->nCcalls == LUAI_MAXCCALLS)
            luaG_runerror(L, "(CLVM) C stack overflow");
        else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS >> 3)))
            luaD_throw(L, LUA_ERRERR);  /* error while handing stack error */
    }
    if (cluaD_precall(L, func, nResults) == PCRLUA)  /* is a Lua function? */
        cluaV_execute(RS, L, 1);  /* call it */
    L->nCcalls--;
    luaC_checkGC(L);
}

void clua_call(std::uintptr_t RS, lua_State* L, int nargs, int nresults) {
    StkId func;
    lua_lock(L);
    api_checknelems(L, nargs + 1);
    checkresults(L, nargs, nresults);
    func = L->top - (nargs + 1);
    cluaD_call(RS, L, func, nresults);
    adjustresults(L, nresults);
    lua_unlock(L);
}

// Most important values:
// Stack frame, depth, also how the fuck am I going to fully do this without having a Roblox proto?
// Simpler way: Proto conversion and only CLVM the instructions, not the whole thing. It's a little bit (cheating) I guess, but I believe Synapse X did it this way too.
#define runtime_check(L, c)	{ if (!(c)) break; }

#define RA(i)	(base+GETARG_A(i))
/* to be used after possible stack reallocation */
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))
#define KBx(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, k+GETARG_Bx(i))

#define dojump(L,pc,i)	{(pc) += (i); luai_threadyield(L);}

#include <math.h>
#define luai_numadd(a,b)	((a)+(b))
#define luai_numsub(a,b)	((a)-(b))
#define luai_nummul(a,b)	((a)*(b))
#define luai_numdiv(a,b)	((a)/(b))
#define luai_nummod(a,b)	((a) - floor((a)/(b))*(b))
#define luai_numpow(a,b)	(pow(a,b))
#define luai_numunm(a)		(-(a))
#define luai_numeq(a,b)		((a)==(b))
#define luai_numlt(a,b)		((a)<(b))
#define luai_numle(a,b)		((a)<=(b))
#define luai_numisnan(a)	(!luai_numeq((a), (a)))

// Roblox has special encryption on numbers (a simple XOR) to prevent CE scans.
double numberXor(double num) {
    return _mm_cvtsd_f64(_mm_xor_pd(_mm_load_sd(&num), _mm_load_pd(&xorConst)));;
}

TValue transpileConstant(std::uintptr_t rbxState, TValue* pConst) {
    TValue output{};

    std::printf("Transpiling constant: %s\n", lua_typename(nullptr, pConst->tt));

    switch (pConst->tt) {
        case LUA_TNIL: {
            output.value.n = 0;
            output.tt = RBX_TYPE_NIL;
            break;
        }
        case LUA_TBOOLEAN: {
            output.value.b = pConst->value.b;
            output.tt = RBX_TYPE_BOOLEAN;
            break;
        }
        case LUA_TNUMBER: {
            output.value.n = numberXor(pConst->value.n);
            output.tt = RBX_TYPE_NUMBER;
            break;
        }
        case LUA_TSTRING: {
            const char* str = svalue(pConst);
            std::uint32_t len = tsvalue(pConst)->len;

            output.value.p = (void*)rbx_snewlstr(rbxState, str, len);
            output.tt = RBX_TYPE_STRING;

            break;
        }
    }

    return output;
}

// TODO: Implement for Roblox!
#define Protect(x)	{ L->savedpc = pc; {x;}; base = L->base; }

bool convertNumber(TValue* value) {
    if (value->tt == RBX_TYPE_NUMBER) return true;
    lua_Number num;
    if ((value->tt == RBX_TYPE_STRING) && luaO_str2d(svalue(value), &num)) {
        value->value.n = numberXor(num);
        value->tt = RBX_TYPE_NUMBER;
        return true;
    }

    return false;
}

std::int32_t ResumeVMClosure(std::uintptr_t RS);

// Heart of the CLVM
// TODO: Pretranspile constants
// Write better macro (RB, RC, RKC, RKB)
// Save encrypted PC macro
void cluaV_execute(std::uintptr_t RS, lua_State* L, int nexeccalls) {
    LClosure* cl;
    StkId base;
    TValue* k;
    const Instruction* pc;
reentry:  /* entry point */
    pc = L->savedpc;
    cl = &clvalue(L->ci->func)->l;
    base = L->base;
    k = cl->p->k;

    StkId robloxBase = *(StkId*)(RS + 0x1C);

    /* main loop of interpreter */
    for (;;) {
        const Instruction i = *pc++;
        TValue* ra = robloxBase + GETARG_A(i);

        switch (GET_OPCODE(i)) {
            case OP_GETGLOBAL: { // Something MIGHT be wrong in here (or intiailization just breaks first opcode automatically lmao)
                TValue rb = transpileConstant(RS, KBx(i));
                TValue* g = (TValue*)(RS + 0x68); // Global environment for now (since I don't have a calling closure / proto)

                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;

                rbxV_gettable(RS, g, &rb, ra);
                robloxBase = *(StkId*)(RS + 0x1C);
                continue;
            }
            case OP_SETGLOBAL: {
                TValue* g = (TValue*)(RS + 0x68); // Global environment for now (since I don't have a calling closure / proto)
                TValue rb = transpileConstant(RS, KBx(i));

                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;

                rbxV_settable(RS, g, &rb, ra);
                robloxBase = *(StkId*)(RS + 0x1C);
                continue;
            }
            case OP_GETTABLE: {
                TValue value;
                if (ISK(GETARG_C(i))) {
                    value = transpileConstant(RS, (k + INDEXK(GETARG_C(i))));
                }
                else {
                    value = *(robloxBase + GETARG_C(i));
                }

                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;

                rbxV_gettable(RS, (robloxBase + GETARG_B(i)), &value, ra);
                robloxBase = *(StkId*)(RS + 0x1C);
                continue;
            }
            case OP_SETTABLE: {
                TValue cValue, bValue;
                if (ISK(GETARG_B(i))) {
                    bValue = transpileConstant(RS, (k + INDEXK(GETARG_B(i))));
                }
                else {
                    bValue = *(robloxBase + GETARG_B(i));
                }
                if (ISK(GETARG_C(i))) {
                    cValue = transpileConstant(RS, (k + INDEXK(GETARG_C(i))));
                }
                else {
                    cValue = *(robloxBase + GETARG_C(i));
                }

                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;
                rbxV_settable(RS, ra, &bValue, &cValue);
                robloxBase = *(StkId*)(RS + 0x1C);
                break;
            }
            case OP_CALL: { // Something broken, probably an internal value
                int b = GETARG_B(i);
                int nresults = GETARG_C(i) - 1;
                if (b != 0) *(TValue**)(RS + 0x10) = ra + b;  /* else previous instruction set top */
                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;

                switch (rbxV_precall(RS, ra, nresults)) {
                    case PCRLUA: {
                        std::printf("We are entering a foreign state closure! CLVM will crash.\n");
                        nexeccalls++;
                        goto reentry;  /* restart luaV_execute over new Lua function */
                    }
                    case PCRC: {
                        /* it was a C function (`precall' called it); adjust results */
                        if (nresults >= 0) *(std::uint32_t*)(RS + 0x10) = *(std::uint32_t*)(*(std::uint32_t*)(RS + 0xC) + 8); // L->top = L->ci->top;
                        robloxBase = *(StkId*)(RS + 0x1C); // Restore base
                        continue;
                    }
                    default: { // Won't run because when it tries to yield it will error and go to my yield handler, preforming the action of returning anyways.
                        return;  /* yield */
                    }
                }
            }
            case OP_SELF: {
                StkId rb = robloxBase + GETARG_B(i);
                setobjs2s(L, ra + 1, rb);

                TValue value;
                if (ISK(GETARG_C(i))) {
                    value = transpileConstant(RS, (k + INDEXK(GETARG_C(i))));
                }
                else {
                    value = *(robloxBase + GETARG_C(i));
                }

                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;

                rbxV_gettable(RS, rb, &value, ra);
                robloxBase = *(StkId*)(RS + 0x1C);
                break;
            }
            case OP_FORPREP: {
                TValue* init = ra;
                TValue* plimit = ra + 1;
                TValue* pstep = ra + 2;

                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                L->savedpc = pc;

                if (!convertNumber(init))
                    rbxG_runerror(RS, LUA_QL("for") " initial value must be a number");
                else if (!convertNumber(plimit))
                    rbxG_runerror(RS, LUA_QL("for") " limit must be a number");
                else if (!convertNumber(pstep))
                    rbxG_runerror(RS, LUA_QL("for") " step must be a number");

                ra->value.n = numberXor(luai_numsub(numberXor(ra->value.n), numberXor(pstep->value.n)));
                ra->tt = RBX_TYPE_NUMBER;

                dojump(L, pc, GETARG_sBx(i));
                continue;
            }
            case OP_FORLOOP: {
                lua_Number step = numberXor(nvalue(ra + 2));
                lua_Number idx = luai_numadd(numberXor(nvalue(ra)), step); /* increment index */
                lua_Number limit = numberXor(nvalue(ra + 1));

                if (luai_numlt(0, step) ? luai_numle(idx, limit)
                    : luai_numle(limit, idx)) {
                    dojump(L, pc, GETARG_sBx(i));  /* jump back */

                    ra->value.n = numberXor(idx); // Internal index
                    ra->tt = RBX_TYPE_NUMBER;

                    (ra + 3)->value.n = numberXor(idx); // External index
                    (ra + 3)->tt = RBX_TYPE_NUMBER;
                }
                continue;
            }
            case OP_TFORLOOP: {
                StkId cb = ra + 3;  /* call base */
                setobjs2s(RS, cb + 2, ra + 2);
                setobjs2s(RS, cb + 1, ra + 1);
                setobjs2s(RS, cb, ra);

                *(TValue**)(RS + 0x10) = cb + 3;  /* func. + 2 args (state and index) */
                *(std::uint32_t*)(RS + 0x14) = (std::uint32_t)pc - (RS + 0x14); // // Save encrypted PC
                rbxD_call(RS, cb, GETARG_C(i)); // TODO: MAKE IT SO THIS CALLS INTO MY OWN FUNCTION TO RESUME MY (OWN) VM! (GENIUS, USING C FUNCTIONS TO AVOID CLVM ISSUES.)
                robloxBase = *(StkId*)(RS + 0x1C); // Restore base
                *(std::uint32_t*)(RS + 0x10) = *(std::uint32_t*)(*(std::uint32_t*)(RS + 0xC) + 8); // L->top = L->ci->top;

                cb = (robloxBase + GETARG_A(i)) + 3;  /* previous call may change the stack */
                if (!(cb->tt == RBX_TYPE_NIL)) {  /* continue loop? */
                    setobjs2s(RS, cb - 1, cb);  /* save control variable */
                    dojump(L, pc, GETARG_sBx(*pc));  /* jump back */
                }
                pc++;
                continue;
            }
            case OP_JMP: {
                dojump(L, pc, GETARG_sBx(i));
                continue;
            }
            case OP_NOT: {
                StkId value = robloxBase + GETARG_B(i);
                int res = (value->tt == RBX_TYPE_NIL) || ((value->tt == RBX_TYPE_BOOLEAN) && (value->value.b) == 0);
                ra->value.b = res;
                ra->tt = RBX_TYPE_BOOLEAN;
                break;
            }
            case OP_LOADBOOL: {
                ra->value.b = GETARG_B(i);
                ra->tt = RBX_TYPE_BOOLEAN;

                if (GETARG_C(i)) pc++;  /* skip next instruction (if C) */
                continue;
            }
            case OP_LOADNIL: {
                TValue* rb = robloxBase + GETARG_B(i);
                do {
                    (rb--)->tt = RBX_TYPE_NIL;
                } while (rb >= ra);
                continue;
            }
            case OP_MOVE: {
                setobj(RS, ra, (robloxBase + GETARG_B(i)));
                continue;
            }
            case OP_LOADK: {
                TValue transpiledConst = transpileConstant(RS, KBx(i));
                TValue* ra = robloxBase + GETARG_A(i);
                setobj(RS, ra, &transpiledConst);
                continue;
            }
            case OP_RETURN: {
                std::printf("(CLVM TERMINATE): Return encountered, call depth NOT supported.\n");
                return;
            }
            default: {
                std::printf("(CLVM TERMINATE): Unknown opcode: %s\n", luaP_opnames[GET_OPCODE(i)]);
                return;
            }
        }
    }

    
}

// Must be considered for garbage collection, this memleaks (can fix with userdata)
// Technically I can have the function sit on the stack, it doesn't necessarily need to be an index?
std::int32_t CreateSharedVMState(std::uintptr_t RS, lua_State* L, std::uint32_t funcIndex) {
    SharedVMState_t* newVMState = new SharedVMState_t{ .RS = RS, .L = L, .funcIndex = funcIndex };
    lua_pushlightuserdata(L, newVMState); // Store it inside our lua state, and hold the index inside Roblox, similar to a wrappers logic.
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static lua_State* gL = luaL_newstate(); // Dedicated CLVM state


void PrintStack(std::uintptr_t state) {
    std::printf("STACK DUMP:\n");
    TValue* current = *(TValue**)(state + 0x1C);
    while (current < *(TValue**)(state + 0x10)) {
        switch (current->tt) {
        case RBX_TYPE_NIL: {
            std::printf("NIL\n");
            break;
        }
        case RBX_TYPE_NUMBER: {
            std::printf("Number: %.2f\n", numberXor(nvalue(current)));
            break;
        }
        case RBX_TYPE_STRING: {
            std::printf("String: %s\n", svalue(current));
            break;
        }
        case RBX_TYPE_BOOLEAN: {
            std::printf("Boolean: %s\n", (bvalue(current) ? "TRUE" : "FALSE"));
            break;
        }
        case RBX_TYPE_LIGHTUSERDATA: {
            std::printf("LUD: 0x%P\n", pvalue(current));
            break;
        }
        default: {
            std::printf("%d - 0x%p\n", current->tt, pvalue(current));
            break;
        }
        }
        ++current;
    }
}

// Since C functions can't properly yield (assembly and lua are not complient), you must make another closure for the resuming.
// Recieves 2 time args
// Roblox fucks up my error handling when it calls this, so I need another func to call this one with correct error handling setup.
// When using PCALL we need a new thread to be able to persist the error handling mechanism. I need to manually set errfunc and errjmp myself here so that I can operate on the same state.
std::int32_t ResumeVMClosureInternal(std::uintptr_t RS) {
    std::int32_t index = (std::int32_t)rbx_tonumber(RS, lua_upvalueindex(1)); // Get first upvalue
    lua_rawgeti(gL, LUA_REGISTRYINDEX, index);
    SharedVMState_t* information = (SharedVMState_t*)lua_topointer(gL, -1);
    lua_pop(gL, 1);


    StkId firstArg = *(StkId*)(RS + 0x1C); // L->base (since nothing else should be on stack)
    if (rbxD_poscall(RS, firstArg)) {
        *(std::uint32_t*)(RS + 16) = *(std::uint32_t*)(*(std::uint32_t*)(RS + 12) + 8); // Correct top
    }

    std::printf("calling execute!\n");
    // TODO: Call in luaD_rawrunprotected so any errors come back to us again! (Can be achieved by wrapping this just like callvmclosure)
    // Also set errfunc manually.
    cluaV_execute(RS, information->L, 1); // Jank for now
    return 0;
}

// TODO: RUN ON A NEW THREAD!
std::int32_t ResumeVMClosure(std::uintptr_t RS) { // This is the shared state
    std::uintptr_t resumeThread = rbx_newthread(RS);
    rbx_setfield(RS, LUA_GLOBALSINDEX, "RESUMETHREAD"); // FIX LATER

    std::printf("RESUME HANDLER CALLED!\n");
    std::printf("Stack size: 0x%08X\n", *(std::uintptr_t*)(RS + 0x10) - *(std::uintptr_t*)(RS + 0x1C));

    // Size is 2
    PrintStack(RS);

    std::int32_t index = (std::int32_t)rbx_tonumber(RS, lua_upvalueindex(1)); // Get first upvalue
    rbx_pushnumber(resumeThread, (double)index);
    rbx_pushcclosure(resumeThread, (std::uintptr_t)&ResumeVMClosureInternal, 1);

    if (rbx_pcall(resumeThread, 0, 0, 0)) {
        std::printf("RESUME ERROR: %s\n", rbx_tostring(resumeThread, -1, nullptr));
        if (!strcmp(rbx_tostring(resumeThread, -1, nullptr), "attempt to yield across metamethod/C-call boundary")) { // Here we check if the call actually errored, or if it yielded.
            // (gettop) to get all yield args besides error ones
            std::uintptr_t yieldArgs = *(std::uintptr_t*)(resumeThread + 0x10) - *(std::uintptr_t*)(resumeThread + 0x1C) - 0x20;
            std::printf("Yield args: 0x%08X\n", yieldArgs);

            PrintStack(resumeThread);

            *(std::uintptr_t*)(resumeThread + 0x10) = *(std::uintptr_t*)(resumeThread + 0x1C); // Pop bool and string from stack (AND CLEAR STACK FOR NOW)
            rbx_pushnumber(resumeThread, (double)index); // Tracker for state
            rbx_pushcclosure(resumeThread, (std::uintptr_t)&ResumeVMClosure, 1); // Write resume handler to top

            std::printf("After fixing:\n");
            PrintStack(resumeThread);
        }
    }
    else {
        rbx_pushnumber(RS, 1); // dummy value
    }

    return 1; // Return the closure we pushed
}

// This is the actual function which calls the VM since I need error handling and yielding I can't do it in the lauch func
std::int32_t CallVMClosure(std::uintptr_t RS) {
    std::printf("Resuming closure!\n");

    std::int32_t index = (std::int32_t)rbx_tonumber(RS, lua_upvalueindex(1)); // Get first upvalue
    lua_rawgeti(gL, LUA_REGISTRYINDEX, index);
    SharedVMState_t* information = (SharedVMState_t*)lua_topointer(gL, -1);
    lua_pop(gL, 1);

    lua_rawgeti(information->L, LUA_REGISTRYINDEX, information->funcIndex); // Push function onto shared state
    clua_call(information->RS, information->L, 0, 0); // IF THE VM ERRORS / YIELDS THIS WILL BE LAST LINE THAT RUNS
    return 0;
}

// TODO: Add errfunc
// TODO: Wrap types (xmove) over to function context RS on return of the VM

/*
    // This is currently calling with a diff lua state so it's trying to rsume the other one not this one.
    // clua_call(RS, information->L, 0, 0); // Call on our VM (We don't use our thread state because this one might have info on it for coroutine)

    // We need to yield if Roblox state is yielding. Check RS->Status for yield, if it is then yield here with rbx_yield
    // If I were to yield without this push, then all they know is they called my function and it popped and there's nothing left to call back.
    // That is how normal function execution works, but since we want to be resumed, we yield with a closure sent back for them to resume.

    // Append this state into yielding states (won't get gcd)

    // Return the func we want to be yielded (it will sit on stack, and overwrite previous yield
*/

// This is technically the resume handler!
std::int32_t LaunchVMClosure(std::uintptr_t RS) { // This state is the call context state, the closure will operate on ANOTHER state (the VM state)
    std::printf("Called from Roblox! Preforming call and launching VM from Lua...\n");
    std::int32_t index = (std::int32_t)rbx_tonumber(RS, lua_upvalueindex(1)); // Get first upvalue
    lua_rawgeti(gL, LUA_REGISTRYINDEX, index);
    SharedVMState_t* information = (SharedVMState_t*)lua_topointer(gL, -1);
    lua_pop(gL, 1);

    rbx_pushnumber(information->RS, (double)index);
    rbx_pushcclosure(information->RS, (std::uintptr_t)&CallVMClosure, 1);

    // BEFORE: THIS WASN'T WORKING BECAUSE WAIT ERRORED AND NEVER ENDED UP ACTUALLY QUEUEING US SINCE THE ERROR. BY USING PCALL WE AVOID THIS!
    // If the code tries to yield it actually ends up erroring since we are in a C function. The thread never really does yield, we just pretend it does. Roblox always requeues the script
    // before calling yield, so that's why it's rescheduled.
    // This also works because yields must be at the end of a cclosures execution.
    if (rbx_pcall(information->RS, 0, 0, 0)) {
        std::printf("ERROR: %s\n", rbx_tostring(information->RS, -1, nullptr));
        if (!strcmp(rbx_tostring(information->RS, -1, nullptr), "attempt to yield across metamethod/C-call boundary")) { // Here we check if the call actually errored, or if it yielded.
            // (gettop) to get all yield args besides error ones
            std::uintptr_t yieldArgs = *(std::uintptr_t*)(information->RS + 0x10) - *(std::uintptr_t*)(information->RS + 0x1C) - 0x20;
            std::printf("Yield args: 0x%08X\n", yieldArgs);

            PrintStack(information->RS);

            *(std::uintptr_t*)(information->RS + 0x10) = *(std::uintptr_t*)(information->RS + 0x1C); // Pop bool and string from stack (AND CLEAR STACK FOR NOW)
            rbx_pushnumber(information->RS, (double)index); // Tracker for state
            rbx_pushcclosure(information->RS, (std::uintptr_t)&ResumeVMClosure, 1); // Write resume handler to top

            std::printf("After fixing:\n");
            PrintStack(information->RS);
        }
    }

    return 0;
}

bool vm::LaunchVM(std::uintptr_t RS, const std::string& script) {
    lua_State* L = lua_newthread(gL); // Create a dedicated thread for this script
    luaL_ref(gL, LUA_REGISTRYINDEX); // Store in registry so it doesn't get GC

    if (!luaL_loadbuffer(L, script.c_str(), script.size(), "NostalgiaVM")) { // Load into a cclosure to be called by Roblox side (which sets up call context)
        std::int32_t funcIndex = luaL_ref(L, LUA_REGISTRYINDEX);
        std::int32_t vmStateIndex = CreateSharedVMState(RS, L, funcIndex);
        rbx_pushnumber(RS, (double)vmStateIndex); // Upvalue holds index into our lua state for information
        rbx_pushcclosure(RS, (std::uintptr_t)&LaunchVMClosure, 1); // When this function is called on Roblox it'll launch our VM
        std::printf("Pushed VM closure!\n");
        return true;
    }
    else { // Can't load this script cause syntax error
        const char* err = lua_tostring(L, -1);
        std::printf("Script error: %s\n", err);
        lua_close(L); // Not sure this unreferences it...
        return false;
    }
}


/*
    THINGS TO NOTE:
    YIELDING RIGHT NOW BREAKS A SMALL RULE OF LUA TO ACHIEVE THE ABILITY OF YIELDING IN A CCLOSURE

    THE CCLOSURE LIMIT IS 200 WHILE LUAS LIMIT IS 20,000. MY CLVM MUST ACCOUNT FOR CALL DEPTH AND KEEP TRACK OF WHATS A CLVM FUNCTION, AND WHAT'S A CCLOSURE.
    THINK OF IT LIKE A THIRD TYPE OF FUNCTION, NOT A CCLOSURE, AND NOT A LUA FUNCTION. IT'S A CLVM FUNCTION.
*/