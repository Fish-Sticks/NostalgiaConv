#pragma once
#include <string>
extern "C" {
#include "../lua/lstate.h"
#include "../lua/lauxlib.h"
}

// Shared in between Roblox and our Lua to keep track of states
struct SharedVMState_t {
	std::uintptr_t RS;
	lua_State* L;
	std::uint32_t funcIndex; // Index into OUR LUA REGISTRY, not Roblox's (We may have to make a Roblox one too if GC gets bad)
};

void clua_call(std::uintptr_t RS, lua_State* L, int nargs, int nresults);


namespace vm {
	bool LaunchVM(std::uintptr_t RS, const std::string& script);
}