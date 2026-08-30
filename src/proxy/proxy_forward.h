#pragma once

#include <windows.h>

#include <cstdint>

#include "proxy/proxy_loader.h"

namespace fl4tout::proxy {
using ProxyWord = std::uintptr_t;
}

#define FL4_PROXY_RESOLVE(name, type) \
    static const auto function = reinterpret_cast<type>(g_proxy_loader.Resolve(#name)); \
    if (function == nullptr) { \
        SetLastError(ERROR_PROC_NOT_FOUND); \
        return 0; \
    }

#define FL4_PROXY_EXPORT0(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name() { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(); \
    }

#define FL4_PROXY_EXPORT1(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1); \
    }


#define FL4_PROXY_EXPORT1_VOID(name) \
    extern "C" void WINAPI name(fl4tout::proxy::ProxyWord a1) { \
        using Fn = void(WINAPI*)(fl4tout::proxy::ProxyWord); \
        static const auto function = reinterpret_cast<Fn>(g_proxy_loader.Resolve(#name)); \
        if (function == nullptr) { \
            SetLastError(ERROR_PROC_NOT_FOUND); \
            return; \
        } \
        function(a1); \
    }

#define FL4_PROXY_EXPORT2(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2); \
    }

#define FL4_PROXY_EXPORT3(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2, fl4tout::proxy::ProxyWord a3) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2, a3); \
    }

#define FL4_PROXY_EXPORT4(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2, fl4tout::proxy::ProxyWord a3, fl4tout::proxy::ProxyWord a4) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2, a3, a4); \
    }

#define FL4_PROXY_EXPORT5(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2, fl4tout::proxy::ProxyWord a3, fl4tout::proxy::ProxyWord a4, fl4tout::proxy::ProxyWord a5) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2, a3, a4, a5); \
    }

#define FL4_PROXY_EXPORT6(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2, fl4tout::proxy::ProxyWord a3, fl4tout::proxy::ProxyWord a4, fl4tout::proxy::ProxyWord a5, fl4tout::proxy::ProxyWord a6) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2, a3, a4, a5, a6); \
    }

#define FL4_PROXY_EXPORT7(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2, fl4tout::proxy::ProxyWord a3, fl4tout::proxy::ProxyWord a4, fl4tout::proxy::ProxyWord a5, fl4tout::proxy::ProxyWord a6, fl4tout::proxy::ProxyWord a7) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2, a3, a4, a5, a6, a7); \
    }

#define FL4_PROXY_EXPORT8(name) \
    extern "C" fl4tout::proxy::ProxyWord WINAPI name(fl4tout::proxy::ProxyWord a1, fl4tout::proxy::ProxyWord a2, fl4tout::proxy::ProxyWord a3, fl4tout::proxy::ProxyWord a4, fl4tout::proxy::ProxyWord a5, fl4tout::proxy::ProxyWord a6, fl4tout::proxy::ProxyWord a7, fl4tout::proxy::ProxyWord a8) { \
        using Fn = fl4tout::proxy::ProxyWord(WINAPI*)(fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord, fl4tout::proxy::ProxyWord); \
        FL4_PROXY_RESOLVE(name, Fn); \
        return function(a1, a2, a3, a4, a5, a6, a7, a8); \
    }
