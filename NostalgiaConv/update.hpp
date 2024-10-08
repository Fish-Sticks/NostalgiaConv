#pragma once
#include <Windows.h>
#include <cstdint>
#include <iostream>

extern "C" {
#include "lua/lobject.h"
}

// Literally going to never need to update but wtv


#define VCLEAR_OPCODE(x) (x & 0x3FFFFFF)
#define VSET_OPCODE(x, i) ((VCLEAR_OPCODE(x)) | ((std::uint32_t)(i & 0x3F) << 26))
#define VGET_OPCODE(x) (std::uint8_t)(x >> 26)

static std::uint32_t deobfuscateInstruction(std::uint32_t obfInstruction, std::uint32_t index) {
    std::uint32_t opcode = VGET_OPCODE(obfInstruction);
    std::uint32_t decodedWithoutOp = ((0x1451AFB * obfInstruction - 0x1A7D575) ^ (index - (0x1C6B438 * obfInstruction)));
    return decodedWithoutOp & 0x3FFFFFF | (opcode << 26);
}

// Use rewritten one I did with math applied!
static std::uint32_t obfuscateInstructionSlow(std::uint32_t instruction, std::uint32_t index) {
    // Cracking it with given constraints just like SMT solver except way worse.
    // Time only takes 0-40ms now.

    std::uint32_t startRange = ((std::uint32_t)VGET_OPCODE(instruction) << 26);
    std::uint32_t syntheticOpInstr = VCLEAR_OPCODE(instruction); // By masking out the changing bytes which don't matter (opcode) we can bruteforce extremely fast.
    for (std::uint32_t i = startRange; i <= (startRange + 0x3FFFFFF); i++) { // Only need to bruteforce call instructions.
        if (VCLEAR_OPCODE(syntheticOpInstr ^ (0x1451AFB * i - 0x1A7D575)) == VCLEAR_OPCODE(index - (0x1C6B438 * i))) {
            return i;
        }
    }

    return 0;
}

static std::uint32_t obfuscateInstructionFast(std::uint32_t instruction, std::uint32_t index) {
    std::uint32_t obfuscated = (VGET_OPCODE(instruction) << 26); // These bytes won't change
    for (std::uint32_t mask = 1; !((mask >> 26) & 1); mask <<= 1) { // Only need to obfuscate 26 bits
        std::uint32_t T1 = (0x1451AFB * obfuscated - 0x1A7D575); // Compute affine transformation 1.
        std::uint32_t T2 = (index - 0x1C6B438 * obfuscated); // Compute affine transformation 2.
        if (((T1 ^ T2) & mask) != (instruction & mask)) { // If even xor odd isn't equal then we know it's a bit that needs to be set.
            obfuscated |= mask;
        }
    }
    return obfuscated;
}


#define RBX_TYPE_NIL 0
#define RBX_TYPE_LIGHTUSERDATA 1
#define RBX_TYPE_NUMBER 2
#define RBX_TYPE_BOOLEAN 3
#define RBX_TYPE_STRING 4
#define RBX_TYPE_THREAD 5
#define RBX_TYPE_FUNCTION 6
#define RBX_TYPE_TABLE 7
#define RBX_TYPE_USERDATA 8
#define RBX_TYPE_PROTO 9
#define RBX_TYPE_UPVAL 10

#define RBX_OP_LOADBOOL 0x00
#define RBX_OP_GETTABLE 0x01
#define RBX_OP_GETUPVAL 0x02
#define RBX_OP_SETGLOBAL 0x03
#define RBX_OP_LOADK 0x04
#define RBX_OP_SETUPVAL 0x05
#define RBX_OP_MOVE 0x06
#define RBX_OP_LOADNIL 0x07
#define RBX_OP_GETGLOBAL 0x08
#define RBX_OP_SELF 0x09
#define RBX_OP_DIV 0x0A
#define RBX_OP_SUB 0x0B
#define RBX_OP_MOD 0x0C
#define RBX_OP_NEWTABLE 0x0D
#define RBX_OP_POW 0x0E
#define RBX_OP_SETTABLE 0x0F
#define RBX_OP_ADD 0x10
#define RBX_OP_MUL 0x11
#define RBX_OP_LEN 0x12
#define RBX_OP_LT 0x13
#define RBX_OP_JMP 0x14
#define RBX_OP_LE 0x15
#define RBX_OP_NOT 0x16
#define RBX_OP_TEST 0x17 // TEST
#define RBX_OP_UNM 0x18
#define RBX_OP_CONCAT 0x19
#define RBX_OP_EQ 0x1A
#define RBX_OP_TAILCALL 0x1B
#define RBX_OP_TFORLOOP 0x1C
#define RBX_OP_FORLOOP 0x1D
#define RBX_OP_SETLIST 0x1E
#define RBX_OP_CALL 0x1F
#define RBX_OP_CLOSE 0x20
#define RBX_OP_TESTSET 0x21 // TEST
#define RBX_OP_RETURN 0x22
#define RBX_OP_FORPREP 0x23
#define RBX_OP_VARARG 0x24
#define RBX_OP_CLOSURE 0x25


const std::uintptr_t robloxBase = (std::uintptr_t)GetModuleHandleA(NULL);

static double xorConst = *(double*)(robloxBase + 0x1246BC0);
const std::uintptr_t scriptContextVTable = (robloxBase + 0xB95D30);

using rbx_getfield_t = void(__cdecl*)(std::uintptr_t state, std::int32_t index, const char* field);
static rbx_getfield_t rbx_getfield = (rbx_getfield_t)(robloxBase + 0x173BF0);

using rbx_setfield_t = void(__cdecl*)(std::uintptr_t state, std::int32_t index, const char* field);
static rbx_setfield_t rbx_setfield = (rbx_setfield_t)(robloxBase + 0x174EB0);

using rbx_pushnumber_t = void(__cdecl*)(std::uintptr_t state, double number);
static rbx_pushnumber_t rbx_pushnumber = (rbx_pushnumber_t)(robloxBase + 0x174770);

using rbx_newthread_t = std::uintptr_t(__cdecl*)(std::uintptr_t state);
static rbx_newthread_t rbx_newthread = (rbx_newthread_t)(robloxBase + 0x1740A0);

using rbx_getstate_t = std::uintptr_t(__thiscall*)(std::uintptr_t scriptContext, std::int32_t stateIndex);
static rbx_getstate_t rbx_getstate = (rbx_getstate_t)(robloxBase + 0x154E70);

using rbx_spawn_t = void(__cdecl*)(std::uintptr_t state);
static rbx_spawn_t rbx_spawn = (rbx_spawn_t)(robloxBase + 0x160670);

using rbx_call_t = std::int32_t(__cdecl*)(std::uintptr_t state, std::int32_t narg, std::int32_t nres, std::int32_t nerr);
static rbx_call_t rbx_pcall = (rbx_call_t)(robloxBase + 0x174330);

#define rbx_call(s, a, b) rbx_pcall(s, a, b, 0)

using rbx_fnewproto_t = std::uintptr_t(__cdecl*)(std::uintptr_t state);
static rbx_fnewproto_t rbx_fnewproto = (rbx_fnewproto_t)(robloxBase + 0x176540);

using rbx_fnewlclosure_t = std::uintptr_t(__cdecl*)(std::uintptr_t state, std::uint8_t nups, std::uintptr_t environment);
static rbx_fnewlclosure_t rbx_fnewlclosure = (rbx_fnewlclosure_t)(robloxBase + 0x1764E0);

using rbx_snewlstr_t = std::uintptr_t(__cdecl*)(std::uintptr_t state, const char* str, std::size_t len);
static rbx_snewlstr_t rbx_snewlstr = (rbx_snewlstr_t)(robloxBase + 0x175EA0);

using rbx_malloc_t = std::uintptr_t(__cdecl*)(std::uintptr_t state, std::uintptr_t a, std::uintptr_t b, std::uintptr_t len);
static rbx_malloc_t rbx_malloc = (rbx_malloc_t)(robloxBase + 0x175E30);

using rbx_pushcclosure_t = void(__cdecl*)(std::uintptr_t state, std::uintptr_t closure, std::uint8_t nups);
static rbx_pushcclosure_t rbx_pushcclosure = (rbx_pushcclosure_t)(robloxBase + 0x174440);

using rbx_pushstring_t = void(__cdecl*)(std::uintptr_t state, const char* str);
static rbx_pushstring_t rbx_pushstring = (rbx_pushstring_t)(robloxBase + 0x1747E0);

using rbx_pushvalue_t = void(__cdecl*)(std::uintptr_t state, std::int32_t index);
static rbx_pushvalue_t rbx_pushvalue = (rbx_pushvalue_t)(robloxBase + 0x1748D0);

using rbx_tostring_t = const char* (__cdecl*)(std::uintptr_t state, std::int32_t index, std::uint32_t* len);
static rbx_tostring_t rbx_tostring = (rbx_tostring_t)(robloxBase + 0x175320);

// CLVM:
using rbxV_gettable_t = void(__cdecl*)(std::uintptr_t state, const TValue* t, TValue* key, StkId val);
static rbxV_gettable_t rbxV_gettable = (rbxV_gettable_t)(robloxBase + 0x3A8120);

using rbxV_settable_t = void(__cdecl*)(std::uintptr_t state, const TValue* t, TValue* key, StkId val);
static rbxV_settable_t rbxV_settable = (rbxV_settable_t)(robloxBase + 0x3A82A0);

using rbxV_precall_t = int(__cdecl*)(std::uintptr_t state, StkId func, std::int32_t nresults);
static rbxV_precall_t rbxV_precall = (rbxV_precall_t)(robloxBase + 0x176E10);

using rbxG_runerror_t = void(__cdecl*)(std::uintptr_t state, const char* fmt, ...);
static rbxG_runerror_t rbxG_runerror = (rbxG_runerror_t)(robloxBase + 0x370870);

using rbxD_call_t = void(__cdecl*)(std::uintptr_t state, StkId func, std::int32_t nResults);
static rbxD_call_t rbxD_call = (rbxD_call_t)(robloxBase + 0x176AD0);

using rbx_touserdata_t = void* (__cdecl*)(std::uintptr_t state, std::int32_t index);
static rbx_touserdata_t rbx_touserdata = (rbx_touserdata_t)(robloxBase + 0x175660);

using rbx_tonumber_t = double(__cdecl*)(std::uintptr_t state, std::int32_t index);
static rbx_tonumber_t rbx_tonumber = (rbx_tonumber_t)(robloxBase + 0x175580);

using rbxL_ref_t = std::int32_t(__cdecl*)(std::uintptr_t state, std::int32_t index);
static rbxL_ref_t rbxL_ref = (rbxL_ref_t)(robloxBase + 0x3723B0);

using rbx_yield_t = std::int32_t(__cdecl*)(std::uintptr_t state, std::int32_t nresults);
static rbx_yield_t rbx_yield = (rbx_yield_t)(robloxBase + 0x177470);

using rbxd_poscall_t = int(__cdecl*)(std::uintptr_t state, StkId firstResult);
static rbxd_poscall_t rbxD_poscall = (rbxd_poscall_t)(robloxBase + 0x176D70);