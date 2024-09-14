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

std::uintptr_t TranspileProto(std::uintptr_t rbxState, Proto* vProto, 
	std::unordered_map<Proto*, std::uintptr_t> protoCache, std::unordered_map<std::uint32_t, std::uintptr_t> stringCache) {
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
				double num = pConst->value.n;
				double xored = _mm_cvtsd_f64(_mm_xor_pd(_mm_load_sd(&num), _mm_load_pd(&xorConst)));

				*(double*)(currConst) = xored;
				*(std::int32_t*)(currConst + 8) = RBX_TYPE_NUMBER;
				break;
			}
			case LUA_TSTRING: {
				if (const auto& found = stringCache.find(pConst->value.gc->ts.tsv.hash); found != stringCache.end()) {
					*(std::uintptr_t*)(currConst) = found->second;
					*(std::int32_t*)(currConst + 8) = RBX_TYPE_STRING;
					break;
				}

				const char* str = svalue(pConst);
				std::uint32_t len = tsvalue(pConst)->len;

				*(std::uintptr_t*)(currConst) = rbx_snewlstr(rbxState, str, len);
				*(std::int32_t*)(currConst + 8) = RBX_TYPE_STRING;

				stringCache[pConst->value.gc->ts.tsv.hash] = *(std::uintptr_t*)(currConst);
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
			break;
		}
		case OP_GETTABLE: {
			SET_OPCODE(transpiledInstr, RBX_OP_GETTABLE);
			break;
		}
		case OP_SETGLOBAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETGLOBAL);
			break;
		}
		case OP_SETTABLE: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETTABLE);
			break;
		}
		case OP_LOADK: {
			SET_OPCODE(transpiledInstr, RBX_OP_LOADK);
			break;
		}
		case OP_CALL: {
			SET_OPCODE(transpiledInstr, RBX_OP_CALL);
			break;
		}
		case OP_SELF: {
			SET_OPCODE(transpiledInstr, RBX_OP_SELF);
			break;
		}
		case OP_DIV: {
			SET_OPCODE(transpiledInstr, RBX_OP_DIV);
			break;
		}
		case OP_SUB: {
			SET_OPCODE(transpiledInstr, RBX_OP_SUB);
			break;
		}
		case OP_LEN: {
			SET_OPCODE(transpiledInstr, RBX_OP_LEN);
			break;
		}
		case OP_LT: {
			SET_OPCODE(transpiledInstr, RBX_OP_LT);
			break;
		}
		case OP_JMP: {
			SET_OPCODE(transpiledInstr, RBX_OP_JMP);
			break;
		}
		case OP_NEWTABLE: {
			SET_OPCODE(transpiledInstr, RBX_OP_NEWTABLE);
			break;
		}
		case OP_MOD: {
			SET_OPCODE(transpiledInstr, RBX_OP_MOD);
			break;
		}
		case OP_ADD: {
			SET_OPCODE(transpiledInstr, RBX_OP_ADD);
			break;
		}
		case OP_MUL: {
			SET_OPCODE(transpiledInstr, RBX_OP_MUL);
			break;
		}
		case OP_NOT: {
			SET_OPCODE(transpiledInstr, RBX_OP_NOT);
			break;
		}
		case OP_TEST: {
			SET_OPCODE(transpiledInstr, RBX_OP_TEST);
			break;
		}
		case OP_TAILCALL: {
			SET_OPCODE(transpiledInstr, RBX_OP_TAILCALL);
			break;
		}
		case OP_TFORLOOP: {
			SET_OPCODE(transpiledInstr, RBX_OP_TFORLOOP);
			break;
		}
		case OP_SETLIST: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETLIST);
			break;
		}
		case OP_CLOSE: {
			SET_OPCODE(transpiledInstr, RBX_OP_CLOSE);
			break;
		}
		case OP_TESTSET: {
			SET_OPCODE(transpiledInstr, RBX_OP_TESTSET);
			break;
		}
		case OP_VARARG: {
			SET_OPCODE(transpiledInstr, RBX_OP_VARARG);
			break;
		}
		case OP_CLOSURE: {
			SET_OPCODE(transpiledInstr, RBX_OP_CLOSURE);
			break;
		}
		case OP_GETUPVAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_GETUPVAL);
			break;
		}
		case OP_SETUPVAL: {
			SET_OPCODE(transpiledInstr, RBX_OP_SETUPVAL);
			break;
		}
		case OP_EQ: {
			SET_OPCODE(transpiledInstr, RBX_OP_EQ);
			break;
		}
		case OP_UNM: {
			SET_OPCODE(transpiledInstr, RBX_OP_UNM);
			break;
		}
		case OP_CONCAT: {
			SET_OPCODE(transpiledInstr, RBX_OP_CONCAT);
			break;
		}
		case OP_POW: {
			SET_OPCODE(transpiledInstr, RBX_OP_POW);
			break;
		}
		case OP_LE: {
			SET_OPCODE(transpiledInstr, RBX_OP_LE);
			break;
		}
		case OP_RETURN: {
			SET_OPCODE(transpiledInstr, RBX_OP_RETURN);
			break;
		}
		case OP_FORPREP: {
			SET_OPCODE(transpiledInstr, RBX_OP_FORPREP);
			break;
		}
		case OP_FORLOOP: {
			SET_OPCODE(transpiledInstr, RBX_OP_FORLOOP);
			break;
		}
		case OP_MOVE: {
			SET_OPCODE(transpiledInstr, RBX_OP_MOVE);
			break;
		}
		case OP_LOADNIL: {
			SET_OPCODE(transpiledInstr, RBX_OP_LOADNIL);
			break;
		}
		case OP_LOADBOOL: {
			SET_OPCODE(transpiledInstr, RBX_OP_LOADBOOL);
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

		switch (getOpMode(vOpcode)) {
			case iABC: { // iABC = iACB
				SETARG_A(transpiledInstr, GETARG_A(vInstr));
				SETARG_B(transpiledInstr, GETARG_C(vInstr));
				SETARG_C(transpiledInstr, GETARG_B(vInstr));

				if (vOpcode == OP_MOVE)
					SETARG_B(transpiledInstr, 1); // Prevent the stack clear
				break;
			}
			case iABx: { // No change
				SETARG_A(transpiledInstr, GETARG_A(vInstr));
				SETARG_Bx(transpiledInstr, GETARG_Bx(vInstr));
				break;
			}
			case iAsBx: { // No change
				SETARG_A(transpiledInstr, GETARG_A(vInstr));
				SETARG_sBx(transpiledInstr, GETARG_sBx(vInstr));
				break;
			}
		}

		switch (vOpcode) { // Special obfuscation
		case OP_CALL:
		case OP_TAILCALL:
		case OP_CLOSURE:
		case OP_RETURN: {
			transpiledInstr = obfuscateInstruction(transpiledInstr, currIndex);
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
			*(std::uintptr_t*)(currChild) = TranspileProto(rbxState, vProto->p[currIndex], protoCache, stringCache);
		}
	}

	// Lineinfo and upvalues can be commented out, they are just for debug info. I don't have locvars included.
	if (vProto->sizelineinfo) {
		std::uintptr_t lineInfo = rbx_malloc(rbxState, 0, 0, 4 * vProto->sizelineinfo);
		*(std::uintptr_t*)(proto + 0x24) = lineInfo - (proto + 0x24);
		*(std::uint32_t*)(proto + 0x40) = vProto->sizelineinfo;

		for (std::uint32_t currIdx = 0; currIdx < vProto->sizelineinfo; currIdx++) {
			std::uintptr_t currLineInfo = lineInfo + (4 * currIdx);

			*(int*)(currLineInfo) = vProto->lineinfo[currIdx] ^ (currIdx << 8); // Lineinfo is encrypted lmao
		}
	}

	if (vProto->sizeupvalues) {
		std::uintptr_t upvalues = rbx_malloc(rbxState, 0, 0, 4 * vProto->sizeupvalues);
		*(std::uint32_t*)(proto + 0x28) = upvalues - (proto + 0x28);
		*(std::uint32_t*)(proto + 0x14) = vProto->sizeupvalues;

		for (std::uint32_t currIndex = 0; currIndex < vProto->sizeupvalues; currIndex++) {
			std::uintptr_t currUpvalue = upvalues + (4 * currIndex);

			TString* upvalName = vProto->upvalues[currIndex];
			const char* rawName = getstr(upvalName);
			std::size_t rawLen = upvalName->tsv.len;

			*(std::uintptr_t*)(currUpvalue) = rbx_snewlstr(rbxState, rawName, rawLen);
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

	std::unordered_map<std::uint32_t, std::uintptr_t> stringCache{};
	std::unordered_map<Proto*, std::uintptr_t> protoCache{};
	std::uintptr_t transpiledProto = TranspileProto(rbxState, vMainProto, protoCache, stringCache);

	// After proto transpile we transpile closure
	std::uintptr_t globalEnv = *(std::uintptr_t*)(rbxState + 0x68);

	std::uintptr_t closure = rbx_fnewlclosure(rbxState, 0, globalEnv);
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