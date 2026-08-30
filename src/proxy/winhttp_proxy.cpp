#include <windows.h>

#include "proxy/proxy_forward.h"

namespace {
fl4tout::proxy::ProxyLoader g_proxy_loader(L"winhttp.dll");
}

static_assert(sizeof(void*) == 8, "FlatOut 4 VR Mute proxy loaders require x64.");

FL4_PROXY_EXPORT0(DllCanUnloadNow)
FL4_PROXY_EXPORT3(DllGetClassObject)
FL4_PROXY_EXPORT0(DllRegisterServer)
FL4_PROXY_EXPORT0(DllUnregisterServer)
FL4_PROXY_EXPORT4(WinHttpAddRequestHeaders)
FL4_PROXY_EXPORT0(WinHttpCheckPlatform)
FL4_PROXY_EXPORT1(WinHttpCloseHandle)
FL4_PROXY_EXPORT4(WinHttpConnect)
FL4_PROXY_EXPORT4(WinHttpCrackUrl)
FL4_PROXY_EXPORT2(WinHttpCreateProxyResolver)
FL4_PROXY_EXPORT4(WinHttpCreateUrl)
FL4_PROXY_EXPORT2(WinHttpDetectAutoProxyConfigUrl)
FL4_PROXY_EXPORT1_VOID(WinHttpFreeProxyResult)
FL4_PROXY_EXPORT1_VOID(WinHttpFreeProxyResultEx)
FL4_PROXY_EXPORT1_VOID(WinHttpFreeProxySettings)
FL4_PROXY_EXPORT1(WinHttpGetDefaultProxyConfiguration)
FL4_PROXY_EXPORT1(WinHttpGetIEProxyConfigForCurrentUser)
FL4_PROXY_EXPORT4(WinHttpGetProxyForUrl)
FL4_PROXY_EXPORT4(WinHttpGetProxyForUrlEx)
FL4_PROXY_EXPORT6(WinHttpGetProxyForUrlEx2)
FL4_PROXY_EXPORT2(WinHttpGetProxyResult)
FL4_PROXY_EXPORT2(WinHttpGetProxyResultEx)
FL4_PROXY_EXPORT2(WinHttpGetProxySettingsVersion)
FL4_PROXY_EXPORT5(WinHttpIsHostInProxyBypassList)
FL4_PROXY_EXPORT5(WinHttpOpen)
FL4_PROXY_EXPORT7(WinHttpOpenRequest)
FL4_PROXY_EXPORT3(WinHttpQueryAuthParams)
FL4_PROXY_EXPORT4(WinHttpQueryAuthSchemes)
FL4_PROXY_EXPORT2(WinHttpQueryDataAvailable)
FL4_PROXY_EXPORT6(WinHttpQueryHeaders)
FL4_PROXY_EXPORT4(WinHttpQueryOption)
FL4_PROXY_EXPORT4(WinHttpReadData)
FL4_PROXY_EXPORT7(WinHttpReadProxySettings)
FL4_PROXY_EXPORT2(WinHttpReceiveResponse)
FL4_PROXY_EXPORT2(WinHttpResetAutoProxy)
FL4_PROXY_EXPORT7(WinHttpSendRequest)
FL4_PROXY_EXPORT6(WinHttpSetCredentials)
FL4_PROXY_EXPORT1(WinHttpSetDefaultProxyConfiguration)
FL4_PROXY_EXPORT4(WinHttpSetOption)
FL4_PROXY_EXPORT4(WinHttpSetStatusCallback)
FL4_PROXY_EXPORT5(WinHttpSetTimeouts)
FL4_PROXY_EXPORT2(WinHttpTimeFromSystemTime)
FL4_PROXY_EXPORT2(WinHttpTimeToSystemTime)
FL4_PROXY_EXPORT4(WinHttpWebSocketClose)
FL4_PROXY_EXPORT2(WinHttpWebSocketCompleteUpgrade)
FL4_PROXY_EXPORT5(WinHttpWebSocketQueryCloseStatus)
FL4_PROXY_EXPORT5(WinHttpWebSocketReceive)
FL4_PROXY_EXPORT4(WinHttpWebSocketSend)
FL4_PROXY_EXPORT4(WinHttpWebSocketShutdown)
FL4_PROXY_EXPORT4(WinHttpWriteData)
FL4_PROXY_EXPORT3(WinHttpWriteProxySettings)

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_proxy_loader.SetProxyModule(module);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
