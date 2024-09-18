#pragma once
#include <string>
#include <cstdint>

extern "C" {
#include "../lua/lapi.h"
#include "../lua/lauxlib.h"
#include "../lua/lopcodes.h"
#include "../lua/lobject.h"
#include "../lua/lstate.h"
}

namespace conversion {
	// Creates a closure and pushes it to top of (Roblox) stack using proto conversion.
	bool ProtoConversion(std::uintptr_t rbxState, lua_State* vState, const std::string& script);
}