#include "InstallService.h"

LPWSTR ConvertErrorCodeToString(DWORD errCode)
{
	HLOCAL LocalAddress = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, errCode, 0, (LPWSTR)&LocalAddress, 0, NULL);
	return (LPWSTR)LocalAddress;
}


DWORD FileExistedOrNot(_In_ LPCWSTR DriverDosName)
{
	HANDLE hDevice;
	DWORD errCode;
	hDevice = CreateFileW(DriverDosName, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		errCode = GetLastError();
		if (errCode == ERROR_FILE_NOT_FOUND)
		{
			return ERROR_FILE_NOT_FOUND;
		}
		printf("CreateFile For Device failed with status %d\n", errCode);
		return errCode;
	}


	CloseHandle(hDevice);
	return NO_ERROR;
}

DWORD InstallDriver(_In_ LPCWSTR DriverName)
{
	SC_HANDLE SchSCManager;
	SC_HANDLE schService;
	DWORD errCode = NO_ERROR;
	WCHAR driverLocation[MAX_PATH] = { 0 };

	SchSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (!SchSCManager)
	{
		errCode = GetLastError();
		printf("Open SC Manager failed with status %d\n", errCode);
		return errCode;
	}

	errCode = SetupDriverName(driverLocation, sizeof(driverLocation));
	if (errCode != NO_ERROR)
	{
		goto Cleanup;
	}

	//Create a new service object.
	schService = CreateServiceW(SchSCManager,           // handle of service control manager database
		DriverName,             // address of name of service to start
		DriverName,             // address of display name
		SERVICE_ALL_ACCESS,     // type of access to service
		SERVICE_KERNEL_DRIVER,  // type of service
		SERVICE_DEMAND_START,   // when to start service
		SERVICE_ERROR_NORMAL,   // severity if service fails to start
		driverLocation,         // address of name of binary file
		NULL,                   // service does not belong to a group
		NULL,                   // no tag requested
		NULL,                   // no dependency names
		NULL,                   // use LocalSystem account
		NULL                    // no password for service account
		);

	if (!schService)
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_EXISTS)
		{
			printf("Previous instance of the service is existed\n");
		}
		else if (errCode == ERROR_SERVICE_MARKED_FOR_DELETE)
		{
			printf("Previous instance of the service is not fully deleted. Try again...\n");
		}
		else
		{
			printf("Create Service failed with status %d\n", errCode);
		}
	}
	else
	{
		printf("Create Service Successfully\n");
	}

Cleanup:


	if (schService)
	{
		CloseServiceHandle(schService);
	}

	if (SchSCManager)
	{
		CloseServiceHandle(SchSCManager);
	}
	return errCode;
}

DWORD SetupDriverName(_Inout_updates_bytes_all_(BufferLength) PWCHAR DriverLocation, _In_ ULONG BufferLength)
{
	HANDLE fileHandle;
	DWORD driverLocLen = 0;
	DWORD errCode = NO_ERROR;
	driverLocLen = GetCurrentDirectoryW(BufferLength, DriverLocation);
	if (driverLocLen == 0)
	{
		errCode = GetLastError();
		printf("GetCurrentDirectory failed with status %d\n", errCode);
		return errCode;
	}
	if (FAILED(StringCbCatW(DriverLocation, BufferLength, L"\\" DRIVER_NAME L".sys")))
	{
		return ERROR_OPERATION_ABORTED;
	}

	if ((fileHandle = CreateFileW(DriverLocation, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{
		printf("%s.sys is not loaded.\n", (LPCTSTR)DRIVER_NAME);
		return ERROR_FILE_NOT_FOUND;
	}

	if (fileHandle)
	{
		CloseHandle(fileHandle);
	}
	return errCode;
}

//DRIVER_NAME
DWORD CustomOpenService(_In_ LPCWSTR ServiceName)
{
	DWORD errCode = NO_ERROR;
	SC_HANDLE schSCManager = NULL;
	SC_HANDLE schService = NULL;

	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (!schSCManager)
	{
		errCode = GetLastError();
		printf("Open SC Manager failed with status %d\n", errCode);
		goto Cleanup;
	}

	schService = OpenServiceW(schSCManager, ServiceName, SERVICE_ALL_ACCESS);

	if (!schService)
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_DOES_NOT_EXIST)
		{
			printf("The specified Service does not exist, Please install first\n");
		}
		else
		{
			printf("Open Service failed with status %d\n", errCode);
		}
			
		goto Cleanup;
	}

	if (!StartService(schService, 0, NULL))
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_ALREADY_RUNNING)
		{
			printf("The specified Service is already running.\n");
		}
		else
		{
			printf("Start Service failed with status %d\n", errCode);
		}
	}
	else
	{
		printf("Start Service successfully\n");
	}

Cleanup:

	if (schService)
	{
		CloseServiceHandle(schService);
	}

	if (schSCManager)
	{
		CloseServiceHandle(schSCManager);
	}

	return errCode;
}

DWORD CustomCloseService(_In_ LPCWSTR ServiceName)
{
	DWORD errCode = NO_ERROR;
	SC_HANDLE SchSCManager;
	SC_HANDLE schService;
	SERVICE_STATUS serviceStatus;
	UNREFERENCED_PARAMETER(serviceStatus);

	SchSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!SchSCManager)
	{
		errCode = GetLastError();
		printf("Open SC Manager failed with status %d\n", errCode);
		return errCode;
	}

	schService = OpenServiceW(SchSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (!schService)
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_DOES_NOT_EXIST)
		{
			printf("The specified Service does not exist\n");
		}
		else
		{
			printf("Open Service failed with status %d\n", errCode);
		}
		
		goto Cleanup;
	}

	if (ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus))
	{
		printf("Stop Service successfully\n");
	}
	else
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_NOT_ACTIVE)
		{
			printf("The specified Service has not been started\n");
		}
		else
		{
			printf("Stop Service failed with status %d\n", errCode);
		}
	}

Cleanup:

	if (schService)
	{
		CloseServiceHandle(schService);
	}

	if (SchSCManager)
	{
		CloseServiceHandle(SchSCManager);
	}

	return errCode;
}

DWORD UnloadDriver(_In_ LPCWSTR DriverName)
{
	DWORD errCode = NO_ERROR;
	SC_HANDLE schService = NULL;
	SC_HANDLE SchSCManager = NULL;
	SERVICE_STATUS serviceStatus;
	UNREFERENCED_PARAMETER(serviceStatus);

	SchSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (!SchSCManager)
	{
		errCode = GetLastError();
		printf("Open SC Manager failed with status %d\n", errCode);
		goto Cleanup;
	}

	schService = OpenServiceW(SchSCManager, DriverName, SERVICE_ALL_ACCESS);
	if (!schService)
	{
		errCode = GetLastError();
		printf("Open Service failed with status %d\n", errCode);
		goto Cleanup;
	}
	//The service control manager deletes the service by deleting the service key and its subkeys from the registry.
	if (!DeleteService(schService))
	{
		errCode = GetLastError();
		if (errCode == ERROR_SERVICE_MARKED_FOR_DELETE)
		{
			printf("The specified service has already been marked for deletion.\n");
		}
		else
		{
			printf("Delete Service failed with status %d\n", errCode);
		}
	}
	else
	{
		printf("Delete the specified Service successfully\n");
	}

Cleanup:


	if (schService)
	{
		CloseServiceHandle(schService);
	}

	if (SchSCManager)
	{
		CloseServiceHandle(SchSCManager);
	}

	return errCode;
}
