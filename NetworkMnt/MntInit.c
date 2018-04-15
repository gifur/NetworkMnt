
#include "MntProcess.h"
//
// Software Tracing Definitions 
//8a2e793c-a8d0-4360-8234-f28811b9c66e
//
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(MetworkMntInit,(8a2e793c, a8d0, 4360, 8234, f28811b9c66e),  \
        WPP_DEFINE_BIT(TRACE_INIT)               \
        WPP_DEFINE_BIT(TRACE_SHUTDOWN)\
		WPP_DEFINE_BIT(TRACE_DEVICE_CONTROL) \
		WPP_DEFINE_BIT(TRACE_FILE_CONTROL) \
		WPP_DEFINE_BIT(TRACE_ERROR_OCCUR)) 


#include "MntInit.tmh"


DEVICE_OBJECT* gMonitorWdmDevice;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD EvtWDFDriverUnload;



NTSTATUS InitialAndCreateDevice(_In_ PWDFDEVICE_INIT pWDFDeviceInit);
//Initializes the request queue for driver
NTSTATUS MonitorControlQueueInit(_In_ WDFDEVICE* pDevice);

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL MonitorEvtDeviceControl;
EVT_WDF_IO_QUEUE_IO_READ MonitorEvtRead;

EVT_WDF_DEVICE_FILE_CREATE MonitorEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE MonitorEvtFileClose;

EVT_WDF_IO_IN_CALLER_CONTEXT MonitorEvtIoInCallerContext;



NTSTATUS DriverEntry(_In_ DRIVER_OBJECT* driverObject,_In_ UNICODE_STRING* registryPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	PWDFDEVICE_INIT pWDFDeviceInit = NULL; // into which the driver stores information about a device
	WDF_DRIVER_CONFIG WDFDriverConfig;
	//A handle to a framework driver object.
	WDFDRIVER driver;

	ExInitializeDriverRuntime(DrvRtPoolNxOptIn);  // Request NX Non-Paged Pool when available

	//
	// This macro is required to initialize software tracing on XP and beyond
	// For XP and beyond use the DriverObject as the first argument.
	//
	WPP_INIT_TRACING(driverObject, registryPath);
	DoTraceMessage(TRACE_INIT, "Initializing Network Monitor Driver");
	//不支持即插即拔，所以无需添加EvtDeviceAdd回调函数
	WDF_DRIVER_CONFIG_INIT(&WDFDriverConfig, WDF_NO_EVENT_CALLBACK);
	WDFDriverConfig.DriverInitFlags |= WdfDriverInitNonPnpDriver;
	WDFDriverConfig.EvtDriverUnload = EvtWDFDriverUnload;

	//创建WDF式驱动对象，传递的参数是WDM式驱动对象
	status = WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES, &WDFDriverConfig, &driver);

	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Create Driver failed with status 0x%08X", status);
		WPP_CLEANUP(driverObject);
		return status;
	}

	// 为创建控制设备对象，分配空间和指定相关权限，返回控制设备对象的初始化结构
	//- pWDFDeviceInit = WdfControlDeviceInitAllocate(driver, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
	//+ 2016-2-5 Joseph
	pWDFDeviceInit = WdfControlDeviceInitAllocate(driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);

	if (pWDFDeviceInit == NULL)
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Allocate Device_init failed with status 0x%08X", status);
		WPP_CLEANUP(driverObject);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	status = InitialAndCreateDevice(pWDFDeviceInit);

	//register process shutdown event callback function 
	status = PsSetCreateProcessNotifyRoutine(ProcessCreateNotifyRoutie, FALSE);

	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "PsSetCreateProcessNotifyRoutine Routine failed with status 0x%08X", status);
	}
	else
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "PsSetCreateProcessNotifyRoutine Routine Successfully");
	}
	return status;
}


NTSTATUS InitialAndCreateDevice(_In_ PWDFDEVICE_INIT pWDFDeviceInit)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFDEVICE device;
	//+ 2016-2-5
	WDF_OBJECT_ATTRIBUTES deviceAttributes;
	WDF_FILEOBJECT_CONFIG fileObjectConfig;

	DECLARE_CONST_UNICODE_STRING(ntDeviceName, MONITOR_DEVICE_NAME);
	DECLARE_CONST_UNICODE_STRING(symbolicName, MONITOR_SYMBOLIC_NAME);

	//指定设备在完成IO请求后，系统使用该值来确定线程的优先级，这里指定为IO_NETWORK_INCREMENT优先级
	WdfDeviceInitSetDeviceType(pWDFDeviceInit, FILE_DEVICE_NETWORK);
	
	// Set the device characteristics 
	//The framework always sets the FILE_DEVICE_SECURE_OPEN characteristic, so your driver does not have to set this characteristic
	WdfDeviceInitSetCharacteristics(pWDFDeviceInit, FILE_DEVICE_SECURE_OPEN, FALSE);

	status = WdfDeviceInitAssignName(pWDFDeviceInit, &ntDeviceName);
	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Set Device Name failed with status 0x%08X", status);
		if (pWDFDeviceInit) WdfDeviceInitFree(pWDFDeviceInit);
		return status;
	}

	
	//WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
	deviceAttributes.ParentObject = NULL;
	deviceAttributes.SynchronizationScope = WdfSynchronizationScopeNone;
	deviceAttributes.ExecutionLevel = WdfExecutionLevelPassive;
	WDF_FILEOBJECT_CONFIG_INIT(&fileObjectConfig, MonitorEvtDeviceFileCreate, MonitorEvtFileClose, NULL);
	WdfDeviceInitSetFileObjectConfig(pWDFDeviceInit, &fileObjectConfig, WDF_NO_OBJECT_ATTRIBUTES);

	//Register a driver's EvtIoInCallerContext event callback function.
	//WdfDeviceInitSetIoInCallerContextCallback(pWDFDeviceInit, MonitorEvtIoInCallerContext);

	// Create a framework device object 
	status = WdfDeviceCreate(&pWDFDeviceInit, &deviceAttributes, &device);


	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Create Device failed with status 0x%08X", status);
		WdfDeviceInitFree(pWDFDeviceInit);
		return status;
	}

	//创建应用程序可用于访问控制设备的符号链接名称
	//待看：https://msdn.microsoft.com/zh-cn/library/ff554302(v=vs.85).aspx
	status = WdfDeviceCreateSymbolicLink(device, &symbolicName);
	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Set Device symbolicName failed with status 0x%08X", status);
		return status;
	}

	status = MonitorControlQueueInit(&device);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	// returns WDM device object that is associated with a specified framework device object
	gMonitorWdmDevice = WdfDeviceWdmGetDeviceObject(device);

	status = MonitorProInitialize(gMonitorWdmDevice);
	if (!NT_SUCCESS(status))
	{
		return status;
	}


	// Initialization of the framework device object is complete
	WdfControlFinishInitializing(device);

	return status;
}

//Initializes the request queue for driver
NTSTATUS MonitorControlQueueInit(_In_ WDFDEVICE* pDevice)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT pDevContext;
	//框架队列对象的配置信息
	WDF_IO_QUEUE_CONFIG ioQueueConfig;

	pDevContext = WdfObjectGetTypedContext(*pDevice, DEVICE_CONTEXT);

	RtlZeroMemory(pDevContext, sizeof(DEVICE_CONTEXT));
	InitializeListHead(&pDevContext->EventQueueHead);
	KeInitializeSpinLock(&pDevContext->QueueLock);

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);

	//https://msdn.microsoft.com/zh-cn/library/ff544583(v=vs.85).aspx
	//添加读取请求的相关操作函数（注册相关回调函数）
	ioQueueConfig.EvtIoDeviceControl = MonitorEvtDeviceControl;
	ioQueueConfig.EvtIoRead = MonitorEvtRead;

	//创建 I/O 队列（框架队列对象）
	status = WdfIoQueueCreate(*pDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pDevContext->DefaultQueue);
	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Set Create IO Queue failed with status 0x%08X", status);
	}
	return status;
}


VOID MonitorEvtDeviceControl(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength, _In_ ULONG IoControlCode)
{
	NTSTATUS status = STATUS_SUCCESS;
	size_t  bytesReturned = 0;
	WDFDEVICE  hDevice;
	PDEVICE_CONTEXT pDevContext = NULL;

	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(OutputBufferLength);

	hDevice = WdfIoQueueGetDevice(Queue);
	pDevContext = WdfObjectGetTypedContext(hDevice, DEVICE_CONTEXT);

	switch (IoControlCode)
	{
		case MONITOR_IOCTL_ENABLE_MONITOR:
		{
			WDFMEMORY WdfMemory;
			void* pBuffer;
			DoTraceMessage(TRACE_DEVICE_CONTROL, "Dispatchs Device Control MONITOR_IOCTL_ENABLE_MONITOR: 0x%08x", IoControlCode);
			if (InputBufferLength < sizeof(MONITOR_SETTINGS))
			{
				DoTraceMessage(TRACE_ERROR_OCCUR, "Input Buffer Size shorter than our expected(MONITOR_SETTINGS).");
				status = STATUS_INVALID_PARAMETER;
			}
			else
			{
				status = WdfRequestRetrieveInputMemory(Request, &WdfMemory);
				if (NT_SUCCESS(status))
				{
					pBuffer = WdfMemoryGetBuffer(WdfMemory, NULL);
					status = MonitorSetEnableMonitoring((MONITOR_SETTINGS*)pBuffer);
				}
				else
				{
					DoTraceMessage(TRACE_ERROR_OCCUR, "WdfRequestRetrieveInputMemory failed with status: 0x%08x", IoControlCode);
				}
			}
			WdfRequestComplete(Request, status);
			break;
		}
		case MONITOR_IOCTL_DISABLE_MONITOR:
		{
			DoTraceMessage(TRACE_DEVICE_CONTROL, "Dispatchs Device Control MONITOR_IOCTL_DISABLE_MONITOR: 0x%08x", IoControlCode);
			MonitorSetDisableMonitoring();
			WdfRequestComplete(Request, STATUS_SUCCESS);
			break;
		}
		case MONITOR_IOCTL_GETINFO_MONITOR:
		{
			status = STATUS_SUCCESS;
			//WDFMEMORY WdfMemory;
			PMONITOR_INFORMATION pBuffer = NULL;
			size_t requireSize;

			if (OutputBufferLength < sizeof(MONITOR_INFORMATION))
			{
				DoTraceMessage(TRACE_ERROR_OCCUR, "Output Buffer Size shorter than our expected(MONITOR_INFORMATION) .");
				WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			}
			else
			{
				status = WdfRequestRetrieveOutputBuffer(Request, sizeof(MONITOR_INFORMATION), &pBuffer, &requireSize);
				if (NT_SUCCESS(status))
				{
					RtlZeroMemory(pBuffer, sizeof(MONITOR_INFORMATION));
					status = RetrieveInfor(pBuffer);

					bytesReturned = requireSize;
					WdfRequestCompleteWithInformation(Request, status, bytesReturned);
				}
				else
				{
					DoTraceMessage(TRACE_ERROR_OCCUR, "WdfRequestRetrieveOutputBuffer faied with status 0x%08x.", status);
					


					WdfRequestComplete(Request, status);
				}
			}

			break;
		}
		case MONITOR_IOCTL_GETINFOS_MONITOR:
		{
			status = STATUS_SUCCESS;
			PMONITOR_INFORMATIONS pBuffer = NULL;
			size_t requireSize;
			if (OutputBufferLength < sizeof(MONITOR_INFORMATIONS))
			{
				DoTraceMessage(TRACE_ERROR_OCCUR, "Output Buffer Size shorter than our expected(MONITOR_INFORMATIONS) .");
				WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			}
			else
			{
				status = WdfRequestRetrieveOutputBuffer(Request, sizeof(MONITOR_INFORMATIONS), &pBuffer, &requireSize);
				if (NT_SUCCESS(status))
				{
					RtlZeroMemory(pBuffer, sizeof(MONITOR_INFORMATIONS));
					status = RetrieveInfors(pBuffer);

					bytesReturned = requireSize;
					WdfRequestCompleteWithInformation(Request, status, bytesReturned);
				}
				else
				{
					DoTraceMessage(TRACE_ERROR_OCCUR, "WdfRequestRetrieveOutputBuffer faied with status 0x%08x.", status);
					WdfRequestComplete(Request, status);
				}
			}

			break;
		}
		//当初为实现驱动与应用消息交互所留下的代码
		case MONITOR_IOCTL_REGISTER_EVENT:
		{
			PREGISTER_EVENT pBuffer = NULL;
			size_t requireSize;
			PNOTIFY_RECORD notifyRecord;
			KLOCK_QUEUE_HANDLE lockHandle;

			DoTraceMessage(TRACE_DEVICE_CONTROL, "Dispatchs Device Control MONITOR_IOCTL_REGISTER_EVENT: 0x%08x", IoControlCode);
			if (InputBufferLength < sizeof(REGISTER_EVENT))
			{
				DoTraceMessage(TRACE_ERROR_OCCUR, "Input Buffer Size shorter than our expected(REGISTER_EVENT) .");
				WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			}
			else
			{
				status = WdfRequestRetrieveInputBuffer(Request, sizeof(REGISTER_EVENT), &pBuffer, &requireSize);
				//WdfRequestWdmGetIrp(Request);
				if (NT_SUCCESS(status))
				{
					notifyRecord = ExAllocatePoolWithQuotaTag(NonPagedPool, sizeof(NOTIFY_RECORD), EVENT_TAG);
					if (NULL == notifyRecord)
					{
						DoTraceMessage(TRACE_ERROR_OCCUR, "Allocate NOTIFY_RECORD buffer failed");
						WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
						break;
					}
					status = ObReferenceObjectByHandle(pBuffer->hEvent,
						SYNCHRONIZE | EVENT_MODIFY_STATE, *ExEventObjectType, KernelMode, &notifyRecord->Event, NULL);
					if (!NT_SUCCESS(status))
					{
						DoTraceMessage(TRACE_ERROR_OCCUR, "ObReferenceObjectByHandle failed with status 0x%08x", status);
						ExFreePoolWithTag(notifyRecord, EVENT_TAG);
					}
					else
					{
						DoTraceMessage(TRACE_DEVICE_CONTROL, "Insert notifyRecord into deviceContext HeadList");
						KeAcquireInStackQueuedSpinLock(&pDevContext->QueueLock, &lockHandle);
						//InsertTailList(&pDevContext->EventQueueHead, &notifyRecord->ListEntry);
						KeReleaseInStackQueuedSpinLock(&lockHandle);
						// Not implement entire routines so delete buffer. joseph 2016-2-17
						ExFreePoolWithTag(notifyRecord, EVENT_TAG);
					}
				}
				else
				{
					DoTraceMessage(TRACE_ERROR_OCCUR, "WdfRequestRetrieveInputBuffer faied with status 0x%08x.", status);
				}
				WdfRequestComplete(Request, status);
			}
			break;
		}

		default:
		{
			status = STATUS_NOT_IMPLEMENTED;
			WdfRequestComplete(Request, status);
		}
	}
	return;
}

VOID MonitorEvtRead(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN size_t Length)
{
	NTSTATUS status = STATUS_SUCCESS;
	PMONITOR_INFORMATION infor = NULL;
	size_t  bytesReturned = 0;

	WDF_REQUEST_PARAMETERS params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Length);

	if (Length < sizeof(MONITOR_INFORMATION))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Input Buffer Size shorter than our expected(MONITOR_INFORMATION) .");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
	}
	else
	{
		status = WdfRequestRetrieveOutputBuffer(Request, sizeof(MONITOR_INFORMATION), &infor, &bytesReturned);
		if (NT_SUCCESS(status))
		{
			RtlZeroMemory(infor, sizeof(MONITOR_INFORMATION));
			status = RetrieveInfor(infor);
			DoTraceMessage(TRACE_DEVICE_CONTROL, "download %I64d bytes, upload %I64d bytes",infor->receivedBytes, infor->sentBytes);
			WdfRequestCompleteWithInformation(Request, status, bytesReturned);
		}
		else
		{
			DoTraceMessage(TRACE_ERROR_OCCUR, "WdfRequestRetrieveOutputBuffer faied with status 0x%08x.", status);
			WdfRequestComplete(Request, status);
		}
	}

	return;
}

void EvtWDFDriverUnload(_In_ WDFDRIVER Driver)
{
	DRIVER_OBJECT* driverObject;
	NTSTATUS status = STATUS_SUCCESS;
	MonitorProUninitialize();
	//unload notify function about process shut down event.
	status = PsSetCreateProcessNotifyRoutine(ProcessCreateNotifyRoutie, TRUE);
	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Close ProcessNotify failed with status 0x%08X", status);
	}
	else
	{
		DoTraceMessage(TRACE_SHUTDOWN, "Close ProcessNotify Successfully");
	}

	DoTraceMessage(TRACE_SHUTDOWN, "Network Monitor Driver Shutting Down");

	driverObject = WdfDriverWdmGetDriverObject(Driver);
	WPP_CLEANUP(driverObject);
}

// MonitorEvtDeviceFileCreate MonitorEvtFileClose MonitorEvtIoInCallerContext 
// the above three funcs refered have no real function yet but have been registered to driver 2016-2-17 Joseph

VOID MonitorEvtDeviceFileCreate(__in  WDFDEVICE Device, __in  WDFREQUEST Request, __in  WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(FileObject);

	DoTraceMessage(TRACE_FILE_CONTROL, "Evt Device File Create");
	WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID MonitorEvtFileClose(__in  WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(FileObject);

	DoTraceMessage(TRACE_FILE_CONTROL, "Evt Device File Close");
}

VOID MonitorEvtIoInCallerContext(__in  WDFDEVICE Device, __in  WDFREQUEST Request)
{
	DWORD dwCode;
	//NTSTATUS status = STATUS_SUCCESS;
	//WDF_OBJECT_ATTRIBUTES attributes;
	WDF_REQUEST_PARAMETERS params;
	//2016-2-14
	//size_t inputBufferLength, outputBufferLength;
	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);
	if (params.Type == WdfRequestTypeDeviceControl)
	{
		dwCode = params.Parameters.DeviceIoControl.IoControlCode;
		if ((dwCode & 0x3) == METHOD_NEITHER)
		{

		}
	}

	DoTraceMessage(TRACE_FILE_CONTROL, "MonitorEvtIoInCallerContext");

	WdfDeviceEnqueueRequest(Device, Request);
}
