#pragma once
#include <Windows.h>
#include <cstdint>
#include <iostream>

// Literally going to never need to update but wtv


#define VCLEAR_OPCODE(x) (x & 0x3FFFFFF)
#define VSET_OPCODE(x, i) ((VCLEAR_OPCODE(x)) | ((std::uint32_t)(i & 0x3F) << 26))
#define VGET_OPCODE(x) (std::uint8_t)(x >> 26)

static std::uint32_t deobfuscateInstruction(std::uint32_t obfInstruction, std::uint32_t index) {
    std::uint32_t opcode = VGET_OPCODE(obfInstruction);
    std::uint32_t decodedWithoutOp = ((0x1451AFB * obfInstruction - 0x1A7D575) ^ (index - (0x1C6B438 * obfInstruction)));
    return decodedWithoutOp & 0x3FFFFFF | (opcode << 26);
}

static std::uint32_t obfuscateInstruction(std::uint32_t instruction, std::uint32_t index) {
    // Cracking it with given constraints just like SMT solver except way shittier.
    // We can minimize the loop to be only looping over the right opcode for a way faster crack!
    // Took 92 seconds to obfuscate! (AFTER MINIMIZING IT TAKES 1 SECOND)
    // Time only takes 0-40ms now, this encryption has been COOKED.

    std::uint32_t startRange = ((std::uint32_t)VGET_OPCODE(instruction) << 26);
    std::uint32_t syntheticOpInstr = VCLEAR_OPCODE(instruction); // By masking out the changing bytes which don't matter (opcode) we can bruteforce extremely fast.
    for (std::uint32_t i = startRange; i <= (startRange + 0x3FFFFFF); i++) { // Only need to bruteforce call instructions.
        if (VCLEAR_OPCODE(syntheticOpInstr ^ (0x1451AFB * i - 0x1A7D575)) == VCLEAR_OPCODE(index - (0x1C6B438 * i))) {
            return i;
        }
    }

    return 0;
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

using rbx_call_t = void(__cdecl*)(std::uintptr_t state, std::int32_t narg, std::int32_t nres);
static rbx_call_t rbx_call = (rbx_call_t)(robloxBase + 0x173710);

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