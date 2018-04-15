#pragma once

#include <windows.h>
#include <winioctl.h>
#include <strsafe.h>

#ifndef _CTYPE_DISABLE_MACROS
#define _CTYPE_DISABLE_MACROS
#endif

#include <fwpmu.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "Common.h"

#pragma comment(lib, "Fwpuclnt.lib")

DWORD SetupDriverName(_Inout_updates_bytes_all_(BufferLength) PWCHAR DriverLocation, _In_ ULONG BufferLength);

DWORD FileExistedOrNot(_In_ LPCWSTR DriverDosName);

DWORD InstallDriver(_In_ LPCWSTR DriverName);

DWORD CustomOpenService(_In_ LPCWSTR ServiceName);

DWORD CustomCloseService(_In_ LPCWSTR ServiceName);

DWORD UnloadDriver(_In_ LPCWSTR DriverName);

