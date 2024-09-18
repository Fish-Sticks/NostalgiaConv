#pragma once
extern "C" {
#include "../lua/lstate.h"
}

void clua_call(std::uintptr_t RS, lua_State* L, int nargs, int nresults);