#pragma once

#include <Windows.h>
#include <psapi.h>
#include <cstddef>

HMODULE Module;
DWORD64 GameImageBase;
MODULEINFO GameModuleInfo;

// Search for signature memory
DWORD64 FindPattern(DWORD64 base, DWORD scan_size, const char* pattern, const char* mask) {
    std::size_t len = strlen(mask);

    for (DWORD64 cursor = 0; cursor < scan_size - len; cursor++) {
        for (int i = 0; i < len; i++) {
            if (pattern[i] != *(char*)(base + cursor + i) && mask[i] != '?') break;
            if (i == len - 1) return cursor + base;
        }
    }

    return 0;
}

// update the base address of the module
void GetProcessInfo() {
    GameImageBase = (DWORD64)GetModuleHandle(NULL);
    GetModuleInformation(GetCurrentProcess(), (HMODULE)GameImageBase, &GameModuleInfo, sizeof(MODULEINFO));
}