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

// Hard-coded file name for demonstration purposes.
#define FILE_PATH1 L"C:\\Program Files\\Dianji\\GKE\\lava.exe"




BOOLEAN InstallDriver(_In_ SC_HANDLE SchSCManager ,_In_ LPCWSTR DriverName, _In_ LPCWSTR ServiceExe);

BOOLEAN RemoveDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName);

BOOLEAN StartDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName);

BOOLEAN StopDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName);


#define USAGE()         {\
        printf("MonitorService <0/1>\n");\
        printf("\t 0 for Run service and 1 for Close the service.\n");\
}

BOOLEAN InstallDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName, _In_ LPCWSTR ServiceExe)
{
	SC_HANDLE schService;
	DWORD err;
	//Create a new service object.
	schService = CreateServiceW(SchSCManager,           // handle of service control manager database
								DriverName,             // address of name of service to start
								DriverName,             // address of display name
								SERVICE_ALL_ACCESS,     // type of access to service
								SERVICE_KERNEL_DRIVER,  // type of service
								SERVICE_DEMAND_START,   // when to start service
								SERVICE_ERROR_NORMAL,   // severity if service fails to start
								ServiceExe,             // address of name of binary file
								NULL,                   // service does not belong to a group
								NULL,                   // no tag requested
								NULL,                   // no dependency names
								NULL,                   // use LocalSystem account
								NULL                    // no password for service account
		);
	if (schService == NULL)
	{
		err = GetLastError();
		if (err == ERROR_SERVICE_EXISTS)
		{
			printf("Warning: Previous instance of the service is existed\n");
			return TRUE;
		}
		else if (err == ERROR_SERVICE_MARKED_FOR_DELETE)
		{
			printf("Previous instance of the service is not fully deleted. Try again...\n");
			return FALSE;
		}
		else
		{
			printf("Create Service failed with status %d\n", err);
			return FALSE;
		}
	}

	if (schService)
	{
		CloseServiceHandle(schService);
	}
	return TRUE;
}
//ServiceName: .sys文件的路径
BOOLEAN ManageDriver(_In_ LPCWSTR DriverName, _In_ LPCWSTR ServiceName, _In_ USHORT Function)
{
	SC_HANDLE schSCManager;
	BOOLEAN bCode = TRUE;
	if (!DriverName || !ServiceName) 
	{
		printf("Invalid Driver or Service provided to ManageDriver() \n");
		return FALSE;
	}
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!schSCManager) 
	{
		printf("Open SC Manager failed with status %d \n", GetLastError());
		return FALSE;
	}
	switch (Function)
	{
	case DRIVER_FUNC_INSTALL:
		if (InstallDriver(schSCManager, DriverName, ServiceName))
		{
			bCode = StartDriver(schSCManager, DriverName);
		}
		else
		{
			bCode = FALSE;
		}
		break;
	case DRIVER_FUNC_REMOVE:
		StopDriver(schSCManager, DriverName);
		RemoveDriver(schSCManager, DriverName);
		bCode = TRUE;
		break;
	default:
		printf("Unknown ManageDriver() function. \n");
		bCode = FALSE;
		break;
	}

	if (schSCManager)
	{
		CloseServiceHandle(schSCManager);
	}

	return bCode;
}


BOOLEAN RemoveDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName)
{
	SC_HANDLE schService;
	BOOLEAN bCode = TRUE;

	schService = OpenServiceW(SchSCManager, DriverName, SERVICE_ALL_ACCESS);
	if (schService == NULL)
	{
		printf("Open Service failed with status %d\n", GetLastError());
		return FALSE;
	}
	if (DeleteService(schService))
	{
		bCode = TRUE;
	}
	else
	{
		printf("Delete Service failed with status %d.\n", GetLastError());
		bCode = FALSE;
	}

	if (schService)
	{
		CloseServiceHandle(schService);
	}
	return bCode;
}

BOOLEAN StartDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName)
{
	SC_HANDLE schService;
	DWORD errCode;
	schService = OpenServiceW(SchSCManager, DriverName, SERVICE_ALL_ACCESS);
	if (schService == NULL)
	{
		printf("Open Service failed with status %d\n", GetLastError());
		return FALSE;
	}
	if (!StartService(schService, 0, NULL))
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_ALREADY_RUNNING)
		{
			return TRUE;
		}
		else
		{
			printf("Start Service failed with status %d\n", errCode);
			return FALSE;
		}
	}

	if (schService)
	{
		CloseServiceHandle(schService);
	}
	return TRUE;
}

BOOLEAN StopDriver(_In_ SC_HANDLE SchSCManager, _In_ LPCWSTR DriverName)
{
	BOOLEAN bCode = TRUE;
	SC_HANDLE schService;
	SERVICE_STATUS serviceStatus;

	schService = OpenServiceW(SchSCManager, DriverName, SERVICE_ALL_ACCESS);
	if (schService == NULL)
	{
		printf("Open Service failed with status %d\n", GetLastError());
		return FALSE;
	}
	if (ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus))
	{
		bCode = TRUE;
	}
	else
	{
		printf("Control Servcive failed with status %d\n", GetLastError());
		bCode = FALSE;
	}

	if (schService)
	{
		CloseServiceHandle(schService);
	}
	return bCode;
}


//获取当前目录下sys文件的路径
BOOLEAN SetupDriverName(_Inout_updates_bytes_all_(BufferLength) PWCHAR DriverLocation, _In_ ULONG BufferLength)
{
	HANDLE fileHandle;
	DWORD driverLocLen = 0;

	driverLocLen = GetCurrentDirectoryW(BufferLength, DriverLocation);
	if (driverLocLen == 0)
	{
		printf("GetCurrentDirectory failed with status %d\n", GetLastError());
		return FALSE;
	}
	if (FAILED(StringCbCatW(DriverLocation, BufferLength, L"\\" DRIVER_NAME L".sys")))
	{
		return FALSE;
	}

	if ((fileHandle = CreateFileW(DriverLocation, GENERIC_READ, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{
		printf("%s.sys is not loaded.\n", (LPCTSTR)DRIVER_NAME);
		return FALSE;
	}

	if (fileHandle)
	{
		CloseHandle(fileHandle);
	}
	return TRUE;
}

int __cdecl wmain(_In_ int argc, _In_reads_(argc) PCWSTR argv[])
{
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);

	HANDLE hDevice;
	DWORD errCode = NO_ERROR;
	WCHAR driverLocation[MAX_PATH] = { 0 };
	UINT type = 0;

	if (argc < 2 || argv[1] == NULL)
	{
		USAGE();
		exit(1);
	}
	if (swscanf_s(argv[1], L"%d", &type) == 0)
	{
		printf("swscanf_s failed\n");
		exit(1);
	}

	hDevice = CreateFileW(MONITOR_DOS_NAME, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

	if (hDevice == INVALID_HANDLE_VALUE && type == 0)
	{
		errCode = GetLastError();
		if (errCode != ERROR_FILE_NOT_FOUND)
		{
			printf("CreateFile failed with status %d\n", errCode);
			return 0;
		}

		if (!SetupDriverName( driverLocation, sizeof(driverLocation) ))
		{
			return 0;
		}
		if (!ManageDriver(DRIVER_NAME, driverLocation, DRIVER_FUNC_INSTALL))
		{
			printf("Unable to install driver.\n");
			ManageDriver(DRIVER_NAME, driverLocation, DRIVER_FUNC_REMOVE);
			return 0;
		}
		hDevice = CreateFileW(MONITOR_DOS_NAME, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

		if (hDevice == INVALID_HANDLE_VALUE)
		{
			printf("Create File Failed eventually with status %d\n", GetLastError());
			return 0;
		}
		wprintf(L"Successfully Install and Start " DRIVER_NAME L" service\n");
		CloseHandle(hDevice);
	}
	else if (hDevice != INVALID_HANDLE_VALUE && type == 1)
	{
		if (CloseHandle(hDevice))
		{
			if (ManageDriver(DRIVER_NAME, driverLocation, DRIVER_FUNC_REMOVE))
			{
				wprintf(L"Successfully Stop and Remove " DRIVER_NAME L" service\n");
			}
		}
	}
	else
	{
		printf("Unknow or Confilct action!!!\n");
		if (hDevice != INVALID_HANDLE_VALUE)
		{
			printf("\tService status: Actived\n");
			CloseHandle(hDevice);
		}
		else
		{
			printf("\tService status: Inactived\n");
		}
		printf("\tAction type: %s\n", type == 0 ? "Run" : "Close");
	}
	return 0;
}