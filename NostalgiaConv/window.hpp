#pragma once
#include <string>

using executefn_t = void(*)(const char* text);

void SetupWindow(executefn_t executefn);
void MakeConsole(const std::string& title);