#include <Windows.h>
#include <thread>
#include <iostream>
#include <string>
#include <mutex>
#include <vector>

#include "update.hpp"
#include "conversion/conversion.hpp"
#include "vm/vm.hpp"
#include "window.hpp"

#define USING_CLVM 1

std::mutex scriptListMtx; // Mutex since multiple threads (command thread, and Roblox thread) are accessing script list
std::vector<std::string> scriptList; // List of scripts pending to run

std::uintptr_t ScanScriptContext() {
    for (std::uintptr_t currentPage = robloxBase; currentPage < 0x7FFFFFFF; currentPage++) { // Scan for script context!
        MEMORY_BASIC_INFORMATION regionInfo{};
        if (!VirtualQuery((PVOID)currentPage, &regionInfo, sizeof(regionInfo)))
            continue;

        if ((regionInfo.Protect & PAGE_NOACCESS) || (regionInfo.Protect & PAGE_GUARD)) {
            currentPage = (std::uintptr_t)regionInfo.BaseAddress + regionInfo.RegionSize + 1;
            continue;
        }

        // Make sure committed memory, and can RW
        if ((regionInfo.Protect & PAGE_READWRITE) && (regionInfo.State & MEM_COMMIT)) {
            for (std::uintptr_t address = (std::uintptr_t)regionInfo.BaseAddress; address < ((std::uintptr_t)regionInfo.BaseAddress + regionInfo.RegionSize) - sizeof(std::uintptr_t); address++) {
                if (*(std::uintptr_t*)(address) == scriptContextVTable) {

                    MEMORY_BASIC_INFORMATION scInfo{};
                    if (!VirtualQuery((PVOID)(address + 0x28), &scInfo, sizeof(scInfo))) // Make sure we can read name
                        continue;

                    if (!((scInfo.Protect & PAGE_READWRITE) && (scInfo.State & MEM_COMMIT)))
                        continue;

                    std::uintptr_t namePtr = *(std::uintptr_t*)(address + 0x28); // Got name now need to query again to make sure this is safe

                    if (!VirtualQuery((PVOID)(namePtr), &scInfo, sizeof(scInfo))) // Make sure we can read name
                        continue;

                    if (!((scInfo.Protect & PAGE_READWRITE) && (scInfo.State & MEM_COMMIT))) // This can still crash due to size issues but idrc
                        continue;

                    if (!strcmp((const char*)namePtr, "Script Context")) {
                        return address;
                    }
                }
            }
        }

        currentPage = (std::uintptr_t)regionInfo.BaseAddress + regionInfo.RegionSize + 1;
    }

    std::printf("Failed to find script context!\n");
    return 0;
}

LPTOP_LEVEL_EXCEPTION_FILTER robloxFilter = 0;
LONG WINAPI CustomExceptionFilter(PEXCEPTION_POINTERS exceptInfo) { // Not the best verbosity but good enough.
    std::uintptr_t addr = (std::uintptr_t)exceptInfo->ExceptionRecord->ExceptionAddress;
    std::uint32_t code = exceptInfo->ExceptionRecord->ExceptionCode;

    std::printf("Exception occured at 0x%p | Code: 0x%08X (Rel: 0x%08X)\n", addr, code, addr - (std::uintptr_t)GetModuleHandleA(NULL));

    std::uintptr_t return1 = *(std::uintptr_t*)(exceptInfo->ContextRecord->Esp); // If there is no stack frame in func
    std::uintptr_t return2 = *(std::uintptr_t*)(exceptInfo->ContextRecord->Ebp + 4); // If there is stack frame in func

    std::printf("Return (no stack frame): 0x%08X | Return (stack frame): 0x%08X\n", return1, return2);

    std::printf("EAX: 0x%08X\n", exceptInfo->ContextRecord->Eax);
    std::printf("EBX: 0x%08X\n", exceptInfo->ContextRecord->Ebx);
    std::printf("ECX: 0x%08X\n", exceptInfo->ContextRecord->Ecx);
    std::printf("EDX: 0x%08X\n", exceptInfo->ContextRecord->Edx);
    std::printf("ESI: 0x%08X\n", exceptInfo->ContextRecord->Esi);
    std::printf("EDI: 0x%08X\n", exceptInfo->ContextRecord->Edi);
    std::printf("EIP: 0x%08X\n", exceptInfo->ContextRecord->Eip);
    std::printf("EBP: 0x%08X\n", exceptInfo->ContextRecord->Ebp);
    std::printf("ESP: 0x%08X\n", exceptInfo->ContextRecord->Esp);

    
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        std::printf("Attempt to ");
        if (exceptInfo->ExceptionRecord->ExceptionInformation[0] == 1)
            std::printf("WRITE");
        else
            std::printf("READ");

        std::printf(" invalid address: 0x%08X\n", exceptInfo->ExceptionRecord->ExceptionInformation[1]);
    }
    else {
        for (std::uint32_t i = 0; i < exceptInfo->ExceptionRecord->NumberParameters; i++) {
            std::printf("Exception info %d | 0x%p\n", i, exceptInfo->ExceptionRecord->ExceptionInformation[i]);
        }
    }


    return robloxFilter(exceptInfo);
}

void RegisterUnhandledFilter() {
    using SUEH_T = LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
    SUEH_T SetUnhandledExceptionFilterReal = (SUEH_T)GetProcAddress(LoadLibraryA("KERNELBASE.dll"), "SetUnhandledExceptionFilter");

    // When we set our own exception filter we still wanna call Roblox's in case they do any weird stuff.
    std::uintptr_t encryptedFilter = *(std::uintptr_t*)((std::uintptr_t)(LoadLibraryA("KERNELBASE.dll")) + 0x001E75D4);
    std::uintptr_t RBXExceptionFilter = (std::uintptr_t)DecodePointer((PVOID)encryptedFilter); // Get Roblox's exception filter.

    robloxFilter = (LPTOP_LEVEL_EXCEPTION_FILTER)RBXExceptionFilter;
    SetUnhandledExceptionFilterReal(&CustomExceptionFilter);
}

// Run all lua / luac code here, since this function is scheduled.
lua_State* vState = nullptr;

int LoadstringFunc(std::uintptr_t myThread) {
    std::string script{};

    std::uint32_t len = 0;
    const char* str = rbx_tostring(myThread, -1, &len);

    script.resize(len, 0);
    std::memcpy(&script[0], str, len);

    conversion::ProtoConversion(myThread, vState, script); // Race condition
    return 1;
}

int normalFunc(lua_State* L) {
    std::printf("normal function called!\n");
    return 0;
}

int RenderSteppedFunc(std::uintptr_t myThread) {
    if (vState) {
        std::lock_guard myGuard{ scriptListMtx };
        for (std::string script : scriptList) {
            scriptList.pop_back(); // Copy & pop
            *(std::uintptr_t*)(myThread + 0x10) = *(std::uintptr_t*)(myThread + 0x1C);

            std::uintptr_t scriptThread = rbx_newthread(myThread); // Each script will run on its own thread
            rbxL_ref(myThread, LUA_REGISTRYINDEX); // Store inside Roblox registry too 

            std::uintptr_t currTop = *(std::uintptr_t*)(scriptThread + 0x10);

            rbx_getfield(scriptThread, LUA_GLOBALSINDEX, "game");
            rbx_getfield(scriptThread, -1, "GetService");
            rbx_pushvalue(scriptThread, -2);
            rbx_pushstring(scriptThread, "Players");
            rbx_pcall(scriptThread, 2, 1, 0);
            rbx_getfield(scriptThread, -1, "LocalPlayer");
            rbx_getfield(scriptThread, -1, "PlayerGui"); // Maybe remove since can crash

            rbx_getfield(scriptThread, LUA_GLOBALSINDEX, "Instance");
            rbx_getfield(scriptThread, -1, "new");
            rbx_pushstring(scriptThread, "LocalScript");
            rbx_pushvalue(scriptThread, -4);
            rbx_pcall(scriptThread, 2, 1, 0); // Create new script with parent of PlayerGui
            rbx_setfield(scriptThread, LUA_GLOBALSINDEX, "script"); // Set as script

            rbx_pushcclosure(scriptThread, (std::uintptr_t)&LoadstringFunc, 0);
            rbx_setfield(scriptThread, LUA_GLOBALSINDEX, "loadstring");

            *(std::uintptr_t*)(scriptThread + 0x10) = currTop; // Restore stack

            if (USING_CLVM) {
                if (vm::LaunchVM(scriptThread, script.c_str())) {
                    rbx_spawn(scriptThread);
                    std::printf("Executed script!\n");
                }
            }
            else {
                if (conversion::ProtoConversion(scriptThread, vState, script.c_str())) {
                    rbx_spawn(scriptThread);
                    std::printf("Executed script!\n");
                }
            }
        }

        scriptList.clear();
    }

    return 0;
}

void ScheduleCode(std::uintptr_t rbxState) { // If we are not in the code scheduler (anywhere in the scheduled VM) then our code will clash with other things, most notably the garbage collector.
    // I did this for fun but in practice you should hook gettop or a job, this code has a chance of crashing on setup and will break on teleports.

    std::uintptr_t myThread = rbx_newthread(rbxState); // Make thread so we don't interfere with lua state
    rbx_setfield(rbxState, LUA_REGISTRYINDEX, "cheatThread"); // Store it incase it gets GC, it will also sit on the main state stack, wtv

    std::printf("My thread: 0x%08X\n", myThread);

    rbx_getfield(myThread, LUA_GLOBALSINDEX, "game");
    rbx_getfield(myThread, -1, "GetService");
    rbx_pushvalue(myThread, -2);
    rbx_pushstring(myThread, "RunService"); // Using GetService incase the game changed the RunService name to something else.
    rbx_call(myThread, 2, 1);
    rbx_getfield(myThread, -1, "RenderStepped");
    rbx_getfield(myThread, -1, "Connect");
    rbx_pushvalue(myThread, -2); // Self
    rbx_pushcclosure(myThread, (std::uintptr_t)&RenderSteppedFunc, 0);
    rbx_call(myThread, 2, 0);
}

void ExecuteFunction(const char* script) {
    std::lock_guard myGuard{ scriptListMtx };
    scriptList.push_back(script);
}

void MainThread() {
    MakeConsole("Nostalgia Conv");
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    std::printf("\"Conversion exploits are piss easy to make. Why was this a flex?\" - fishy\n");

    RegisterUnhandledFilter();

    std::uintptr_t scriptContext = ScanScriptContext(); // Gotta do it the classic way
    std::printf("Script Context: 0x%08X\n", scriptContext);

    if (!scriptContext)
        return;

    std::uintptr_t rbxState = rbx_getstate(scriptContext, 0);
    std::printf("Roblox state: 0x%08X\n", rbxState);

    vState = luaL_newstate();
    ScheduleCode(rbxState);

    SetupWindow(&ExecuteFunction);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        std::thread(MainThread).detach();
        break;
    }
    return TRUE;
}

/*
NOSTALGIACONV | PROTO CONVERSION EXPLOIT.

    THIS SOURCE DOES NOT INCLUDE CALLCHECK / RETCHECK BYPASS, MOST REVIVALS HAVE IT BROKEN ANYWAYS. IF YOU DO ENCOUNTER IT YOU CAN CLONE FUNC AND REMOVE IT (ETERNALS METHOD),
    OR YOU CAN SPOOF IT VIA SOME ASSEMBLY. I WILL NOT GO OVER HOW TO DO THAT, OR HOW TO UPDATE THIS CHEAT BUT IT SHOULD BE SELF EXPLANATORY. THIS SOURCE WAS MADE FOR FUN, AND DOESN'T
    NECESSARILY MEAN IT IS A GOOD CHEAT BASE TO USE, BUT IT CAN BE MADE BETTER BY ADDING SCHEDULER. I DID SCANNING BECAUSE OF NOSTALGIA FROM WHEN I USED TO SCAN FOR THE SCRIPT CONTEXT.
    THERE IS NO DEBUG INFORMATION IN THIS EITHER, MAINLY DUE TO NOT WANTING TO UPDATE THAT.

    A COUPLE THINGS WOULD NEED TO BE MODIFIED TO MAKE THIS A NEAR 100% STABILITY RATE. RIGHT NOW IT HAS INSTABILITY BUT ONLY ON INJECTION, NOT ON EXECUTION. READ MY COMMENTS TO UNDERSTAND
    HOW THIS SOURCE WORKS. THE ENCRYPTION ROBLOX USES I DON'T FULLY UNDERSTAND. I SIMPLY MADE SOME CONSTRAINTS AND BRUTEFORCE IT, IN AN EFFICIENT WAY BUT IT'S STILL SLOW. SOMEBODY ACTUALLY
    POSTED ABOUT ROBLOX'S ENCRYPTION ON A MATH FORUM A LONG TIME AGO (2016). I SOLVED THE ENCRYPTION MYSELF WITHOUT ANYTHING FOR FUN AND TO LEARN A BIT ABOUT THE MATH. AFTERWARDS, I DECIDED
    TO READ ABOUT THE ENCRYPTION. THIS LINK CONTAINS THE DETAILED MATH BEHIND IT: https://math.stackexchange.com/questions/1884500/how-to-solve-this-equation-for-x-with-xor-involved

    IF YOU ARE CURIOUS ABOUT THEIR INSTRUCTION ENCRYPTION AND HOW THEY CAN MULTIPLY WITHOUT A DIVIDE, IT'S CALLED A MODULAR MULTIPLICATIVE INVERSE, IT'S QUITE AN INTERESTING TOPIC. I MAINLY MADE
    THIS CHEAT TO ACTUALLY LEARN THE MATH BEHIND THEIR OLD ENCRYPTION BECAUSE I THINK IT'S VERY COOL.

    IF YOU NEED HELP UPDATING THE LUAC FUNCTIONS HERE IS A HELPFUL RESOURCE: https://pastebin.com/raw/4bRhbNR0

    THIS ENTIRE CHEAT WAS WRITTEN FROM SCRATCH BY ME, NOTHING WAS REFERENCED AND NO OTHER SOURCES WERE USED BESIDES THE LUA SOURCE, WHICH I MODIFIED TO WORK WITH THE CLIENTS INSTRUCTION FORMAT.
    THE WINDOW CODE WAS MADE COURTESY OF CHATGPT SINCE WIN32 API IS HORRIBLE TO USE. I DON'T RECOMMEND YOU USE IT, CONSIDERING HOW LIMITED IT IS AND HOW MUCH EFFORT IT IS FOR A SIMPLE WINDOW.
    THE UNHANDLED EXCEPTION FILTER USES A WINDOWS SPECIFIC OFFSET, YOU WILL NEED TO REVERSE KERNELBASE.SetUnhandledExceptionFilter FOR THIS OFFSET.

    THIS SOURCE IS NO LONGER USEFUL IN THE MODERN YEAR UNLESS YOU ARE CHEATING ON AN OLD REVIVAL CLIENT (2013-2019 I believe). THIS ONE WAS SPECIFICALLY MADE FOR 2016. IT IS DESIGNED AS A
    LEARNING MATERIAL AND SOMETHING TO TINKER WITH. LOUKA WROTE SOME EDUCATIONAL DOCUMENTS ON PROTO CONVERSION, AND A COUPLE OTHER EXECUTION METHODS HERE: https://github.com/Fish-Sticks/LoukaPDF

    THERE IS NO ELEVATED IDENTITY, I'LL LEAVE IT AS AN EXERCISE FOR YOU!

    LUA5.1 DOCUMENTATION: https://www.lua.org/manual/5.1/
    A GUIDE ON THE LUA VM INTERNALS, THIS WILL HELP YOU UNDERSTAND PROTO CONVERSION: https://archive.org/details/a-no-frills-intro-to-lua-5.1-vm-instructions

    BY READING ALL THESE RESOURCES AND SOME REVERSE ENGINEERING YOU WILL COMPLETELY UNDERSTAND HOW TO DO PROTO CONVERSION ON YOUR OWN, NOT ONLY FOR THIS CLIENT BUT FOR ANY LUA GAME YOU WANT.
    BACK IN 2016 A CHEAT LIKE THIS WOULD COST ~$40 USD.

    YOU MIGHT BE WONDERING WHY I'D SPEND SO MUCH TIME MAKING A CHEAT FOR AN OLDER CLIENT? WELL I CODED THIS IN A DAY, IT TOOK ME MANY HOURS OF THE DAY BUT STILL ONLY A DAY. IT PROBABLY WON'T
    BE UPDATED UNLESS YOU PAY ME, SINCE I DON'T ENJOY UPDATING. I MADE THIS CHEAT BECAUSE I NEVER GOT TO EXPERIENCE CHEATING ON THE OLD VM. MANY TOLD ME IT WAS VERY HARD, AND THAT LUAU IS A
    LOT EASIER TO CHEAT ON. WHILE THIS IS TRUE, MY SKILL IS SIMPLY WAY BETTER THAN IT WAS BEFORE AND I DIDN'T STRUGGLE WITH THIS VM AT ALL. I ALWAYS WANTED TO EXPERIENCE CHEATING ON THE OLD
    VM AND I HAVE FINALLY GOT THE CHANCE TO DO IT. I'VE ALSO WRITTEN LUAU TRANSPILERS BEFORE LUAU WAS OPEN SOURCE, AND THAT WAS QUITE FUN TOO, BUT A LITTLE MORE DIFFICULT SINCE WE HAD TO DEAL
    WITH DIFFERENT INSTRUCTIONS. THIS WAS A FUN EXPERIENCE AND FUN TO MAKE, IF YOU ARE INTERESTED IN MAKING UR OWN TRY OUT MAKING A CLVM, IT'S A LITTLE HARDER BUT MORE FUN. I'VE NEVER ENJOYED
    MAKING BYTECODE CONVERSION EXPLOITS BECAUSE I LIKE TO GET LOW INTO THE VM AND HAVE CONTROL OVER EVERYTHING START TO FINISH.

    HAVE FUN!


    THE CLVM IS A COMPLETELY DIFFERENT BEAST, IT'S HARD TO DOCUMENT HOW IT WORKS DUE TO ITS DELICATE AND COMPLEX INTERNALS. FIRST OF ALL, YOU MUST AVOID YOUR SHIT BEING GC'D ON BOTH STATES.
    SECONDLY, YOU MUST ENSURE CORRECT SYNC BETWEEN YOUR VM AND ROBLOX'S, YOU MUST HAVE YOUR CALL CONTEXT SETUP, YOU MUST HANDLE ERRORS, AND YOU MUST HANDLE EVERYTHING :)
*/