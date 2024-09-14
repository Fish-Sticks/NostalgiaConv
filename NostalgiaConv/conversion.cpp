#include "conversion.hpp"
#include "update.hpp"
#include <intrin.h>
#include <unordered_map>

std::uint32_t CrackEncodeKey(std::uint32_t decodeKey) {
	std::printf("Cracking encode key!\n");
	for (std::uint32_t i = 0; i < 0xFFFFFFFF; i++) {
		if (i * decodeKey == 1)
			return i;
	}

	std::printf("Failed to crack encode key!\n");
	return 0;
}

std::uintptr_t TranspileProto(std::uintptr_t rbxState, Proto* vProto, std::unordered_map<Proto*, std::uintptr_t> protoCache) {
	if (const auto& found = protoCache.find(vProto); found != protoCache.end()) {
		return found->second;
	}

	std::uintptr_t proto = rbx_fnewproto(rbxState);
	const char* src = getstr(vProto->source);

	std::uintptr_t chunkName = rbx_snewlstr(rbxState, src, vProto->source->tsv.len);
	*(std::uintptr_t*)(proto + 0xC) = chunkName - (proto + 0xC);

	// Can find these in luaD_precall
	*(std::uint8_t*)(proto + 0x48) = vProto->maxstacksize;
	*(std::uint8_t*)(proto + 0x4B) = vProto->is_vararg;
	*(std::uint8_t*)(proto + 0x4A) = vProto->nups;
	*(std::uint8_t*)(proto + 0x49) = vProto->numparams;

	std::uintptr_t constantTable = rbx_malloc(rbxState, 0, 0, 16 * vProto->sizek);
	*(std::uint32_t*)(proto + 0x1C) = vProto->sizek;
	*(std::uintptr_t*)(proto + 0x20) = constantTable - (proto + 0x20);

	// Transpile constants
	for (std::uint32_t currIndex = 0; currIndex < vProto->sizek; currIndex++) {
		TValue* pConst = &vProto->k[currIndex];
		std::uintptr_t currConst = constantTable + (16 * currIndex);

		switch (pConst->tt) {
			case LUA_TNIL: {
				*(std::uintptr_t*)(currConst) = 0;
				*(std::int32_t*)(currConst + 8) = RBX_TYPE_NIL;
				break;
			}
			case LUA_TBOOLEAN: {
				*(bool*)(currConst) = pConst->value.b;
				*(std::int32_t*)(currConst + 8) = RBX_TYPE_BOOLEAN;
				break;
			}
			case LUA_TNUMBER: { // Roblox has special encryption on numbers (a simple XOR) to prevent CE scans.
				double xored = _mm_cvtsd_f64(_mm_xor_pd(_mm_load_sd(&pConst->value.n), _mm_load_pd(&xorConst)));

				*(double*)(currConst) = xored;
				*(std::int32_t*)(currConst + 8) = RBX_TYPE_NUMBER;
				break;
			}
			case LUA_TSTRING: { // For now make new tstring, later add cache
				const char* str = svalue(pConst);
				std::uint32_t len = tsvalue(pConst)->len;

				*(std::uintptr_t*)(currConst) = rbx_snewlstr(rbxState, str, len);
				*(std::int32_t*)(currConst + 8) = RBX_TYPE_STRING;
				break;
			}
		}
	}

	std::uintptr_t pDecodeKey = *(std::uintptr_t*)(rbxState + 8) + 0x24;
	std::uintptr_t decodeKey = rbxState + pDecodeKey + *(std::uintptr_t*)(rbxState + pDecodeKey);

	static std::uintptr_t encodeKey = CrackEncodeKey(decodeKey);

	std::uintptr_t instructionTable = rbx_malloc(rbxState, 0, 0, 4 * vProto->sizecode);
	*(std::uint32_t*)(proto + 0x34) = vProto->sizecode;
	*(std::uintptr_t*)(proto + 0x18) = instructionTable - (proto + 0x18);

	for (std::uint32_t currIndex = 0; currIndex < vProto->sizecode; currIndex++) {
		std::uint32_t currInstr = instructionTable + (4 * currIndex);

		Instruction vInstr = vProto->code[currIndex];
		Instruction transpiledInstr = 0;

		std::uint8_t vOpcode = GET_OPCODE(vInstr);

		switch (vOpcode) {
		case OP_GETGLOBAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_GETGLOBAL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_Bx(transpiledInstr, GETARG_Bx(vInstr));
			break;
		}
		case OP_GETTABLE: {
			SET_OPCODE(transpiledInstr, RBX_OP_GETTABLE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_SETGLOBAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETGLOBAL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_Bx(transpiledInstr, GETARG_Bx(vInstr));
			break;
		}
		case OP_SETTABLE: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETTABLE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_LOADK: {
			SET_OPCODE(transpiledInstr, RBX_OP_LOADK);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_Bx(transpiledInstr, GETARG_Bx(vInstr));
			break;
		}
		case OP_CALL: { // B and C flipped
			SET_OPCODE(transpiledInstr, RBX_OP_CALL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));

			transpiledInstr = obfuscateInstruction(transpiledInstr, currIndex);
			break;
		}
		case OP_SELF: {
			SET_OPCODE(transpiledInstr, RBX_OP_SELF);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_DIV: {
			SET_OPCODE(transpiledInstr, RBX_OP_DIV);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_SUB: {
			SET_OPCODE(transpiledInstr, RBX_OP_SUB);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_LEN: {
			SET_OPCODE(transpiledInstr, RBX_OP_LEN);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_LT: {
			SET_OPCODE(transpiledInstr, RBX_OP_LT);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_JMP: {
			SET_OPCODE(transpiledInstr, RBX_OP_JMP);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_sBx(transpiledInstr, GETARG_sBx(vInstr));
			break;
		}
		case OP_NEWTABLE: {
			SET_OPCODE(transpiledInstr, RBX_OP_NEWTABLE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_MOD: {
			SET_OPCODE(transpiledInstr, RBX_OP_MOD);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_ADD: {
			SET_OPCODE(transpiledInstr, RBX_OP_ADD);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_MUL: {
			SET_OPCODE(transpiledInstr, RBX_OP_MUL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_NOT: {
			SET_OPCODE(transpiledInstr, RBX_OP_NOT);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_TEST: {
			SET_OPCODE(transpiledInstr, RBX_OP_TEST);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_TAILCALL: {
			SET_OPCODE(transpiledInstr, RBX_OP_TAILCALL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));

			transpiledInstr = obfuscateInstruction(transpiledInstr, currIndex);
			break;
		}
		case OP_TFORLOOP: {
			SET_OPCODE(transpiledInstr, RBX_OP_TFORLOOP);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_SETLIST: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETLIST);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_CLOSE: {
			SET_OPCODE(transpiledInstr, RBX_OP_CLOSE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_TESTSET: {
			SET_OPCODE(transpiledInstr, RBX_OP_TESTSET);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_VARARG: {
			SET_OPCODE(transpiledInstr, RBX_OP_VARARG);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_CLOSURE: {
			SET_OPCODE(transpiledInstr, RBX_OP_CLOSURE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_Bx(transpiledInstr, GETARG_Bx(vInstr));

			transpiledInstr = obfuscateInstruction(transpiledInstr, currIndex);
			break;
		}
		case OP_GETUPVAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_GETUPVAL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_SETUPVAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETUPVAL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_EQ: {
			SET_OPCODE(transpiledInstr, RBX_OP_EQ);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_UNM: {
			SET_OPCODE(transpiledInstr, RBX_OP_UNM);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_CONCAT: {
			SET_OPCODE(transpiledInstr, RBX_OP_CONCAT);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_POW: {
			SET_OPCODE(transpiledInstr, RBX_OP_POW);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_LE: {
			SET_OPCODE(transpiledInstr, RBX_OP_LE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_RETURN: {
			SET_OPCODE(transpiledInstr, RBX_OP_RETURN);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));

			transpiledInstr = obfuscateInstruction(transpiledInstr, currIndex);
			break;
		}
		case OP_FORPREP: {
			SET_OPCODE(transpiledInstr, RBX_OP_FORPREP);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_sBx(transpiledInstr, GETARG_sBx(vInstr));
			break;
		}
		case OP_FORLOOP: {
			SET_OPCODE(transpiledInstr, RBX_OP_FORLOOP);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_sBx(transpiledInstr, GETARG_sBx(vInstr));
			break;
		}
		case OP_MOVE: {
			SET_OPCODE(transpiledInstr, RBX_OP_MOVE);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr)); // Set to 0 since we don't want to clear stack! I assume this was a Roblox optimization.
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_LOADNIL: { // We did it right unlike calamari
			SET_OPCODE(transpiledInstr, RBX_OP_LOADNIL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		case OP_LOADBOOL: {
			SET_OPCODE(transpiledInstr, RBX_OP_LOADBOOL);
			SETARG_A(transpiledInstr, GETARG_A(vInstr));
			SETARG_B(transpiledInstr, GETARG_C(vInstr));
			SETARG_C(transpiledInstr, GETARG_B(vInstr));
			break;
		}
		default: {
			const char* opName = luaP_opnames[vOpcode];
			std::printf("Missing %s (", opName);

			switch (getOpMode(vOpcode)) {
			case iABC: {
				std::uint8_t a = GETARG_A(vInstr);
				std::uint8_t b = GETARG_B(vInstr);
				std::uint8_t c = GETARG_C(vInstr);

				std::printf("0x%02X | 0x%02X | 0x%02X)\n", a, b, c);
				break;
			}
			case iABx: {
				std::uint8_t a = GETARG_A(vInstr);
				std::uint16_t bx = GETARG_Bx(vInstr);

				std::printf("0x%02X | 0x%04X)\n", a, bx);
				break;
			}
			case iAsBx: {
				std::uint8_t a = GETARG_A(vInstr);
				std::int16_t sbx = GETARG_sBx(vInstr);

				std::printf("0x%02X | 0x%04X)\n", a, sbx);
				break;
			}
			}
			break;
		}
		}

		*(std::uint32_t*)(currInstr) = transpiledInstr * encodeKey;
	}

	if (vProto->sizep) {
		std::uintptr_t childTable = rbx_malloc(rbxState, 0, 0, 4 * vProto->sizep);
		*(std::uintptr_t*)(proto + 8) = childTable - (proto + 8);
		*(std::uint32_t*)(proto + 0x2C) = vProto->sizep;

		for (std::uint32_t currIndex = 0; currIndex < vProto->sizep; currIndex++) {
			std::uintptr_t currChild = childTable + (4 * currIndex);
			*(std::uintptr_t*)(currChild) = TranspileProto(rbxState, vProto->p[currIndex], protoCache);
		}
	}

	protoCache[vProto] = proto;
	return proto;
}

bool conversion::ProtoConversion(std::uintptr_t rbxState, lua_State* vState, const std::string& script) {
	if (luaL_loadbuffer(vState, script.c_str(), script.size(), "NostalgiaConv")) {
		const char* err = lua_tostring(vState, -1);
		std::printf("Error in script: %s\n", err);
		lua_pop(vState, 1);
		return false;
	}

	// GARBAGE COLLECTOR MUST BE PAUSED OTHERWISE IT WILL COLLECT MY PROTO BEFORE ITS MADE!

	*(std::uint32_t*)(*(std::uintptr_t*)(rbxState + 8) + rbxState + 0x54) = MAX_LUMEM; // Set GCthreshold to max so it can't collect.

	LClosure* vClosure = (LClosure*)lua_topointer(vState, -1);
	Proto* vMainProto = vClosure->p;

	std::unordered_map<Proto*, std::uintptr_t> protoCache{};
	std::uintptr_t transpiledProto = TranspileProto(rbxState, vMainProto, protoCache);

	// After proto transpile we transpile closure
	std::uintptr_t globalEnv = *(std::uintptr_t*)(rbxState + 0x68);

	std::uintptr_t closure = rbx_fnewlclosure(rbxState, vClosure->nupvalues, globalEnv); // Not initializing upvalues rn!
	*(std::uintptr_t*)(closure + 0x10) = transpiledProto - closure - 0x10;

	// Push transpiled closure onto stack
	std::uintptr_t* pStackTop = (std::uintptr_t*)(rbxState + 0x10);
	*(std::uintptr_t*)(*pStackTop) = closure;
	*(int*)(*pStackTop + 8) = RBX_TYPE_FUNCTION;
	*pStackTop += 0x10;

	// Allow collecting now (GCthreshold = totalbytes)
	*(std::uint32_t*)(*(std::uintptr_t*)(rbxState + 8) + rbxState + 0x54) = *(std::uint32_t*)(*(std::uintptr_t*)(rbxState + 8) + rbxState + 0x60);

	lua_pop(vState, 1);
	return true;
}