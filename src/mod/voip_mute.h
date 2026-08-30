#pragma once

#include <windows.h>

namespace fl4tout {

DWORD WINAPI VoipMuteWorker(LPVOID module_parameter);
void RequestVoipMuteStop();

}  // namespace fl4tout
