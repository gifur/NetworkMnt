#include "MntProcess.h"
#include "intsafe.h"

#define INITGUID
#include <guiddef.h>
#include "MntGuid.h"


//
// Software Tracing Definitions 5dbdc325-8529-4aa7-99cd-9ec812dfcf0c
//
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(MsnMntrMonitor,(5dbdc325, 8529, 4aa7, 99cd, 9ec812dfcf0c),  \
        WPP_DEFINE_BIT(TRACE_FLOW_ESTABLISHED)      \
        WPP_DEFINE_BIT(TRACE_STATE_CHANGE)      \
		WPP_DEFINE_BIT(TRACE_ERROR_OCCUR)      \
		WPP_DEFINE_BIT(TRACE_STREAM_MONITOR)    \
        WPP_DEFINE_BIT(TRACE_LAYER_NOTIFY) )

#include "MntProcess.tmh"

#define TAG_NAME_CONTEXT 'bmCM'
#define TAG_NAME_INFO 'bmCI'

UINT32 flowEstablishedCalloutId = 0;
UINT32 streamCalloutId = 0;
long monitoringEnabled = 0;
UINT32 numKernelInfor = 0;
LIST_ENTRY flowContextList;
KSPIN_LOCK flowContextListLock;
KSPIN_LOCK flowContextIncreLock;

LIST_ENTRY kernelInforList;
KSPIN_LOCK kernelInforLock;

MONITOR_INFORMATION* gMonitorInfor = NULL;

UINT64 gSentBytes = 0;
UINT64 gReceivedBytes = 0;

PKEVENT gEvents[DEFAULT_EVENT_NUM - 1];

//有用-获取进程数据信息
NTSTATUS RetrieveInfors(_Out_ MONITOR_INFORMATIONS* informations)
{
	KLOCK_QUEUE_HANDLE lockHandle;
	PMONITOR_KERNEL_INFORMATION willRetrivedInfor = NULL;
	LIST_ENTRY* entry;
	UINT32 undex = 0;

	ASSERT(numKernelInfor <= MAX_NUM_OF_INFORMATION);

	if (informations == NULL)
	{
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireInStackQueuedSpinLock(&kernelInforLock, &lockHandle);

	if (IsListEmpty(&kernelInforList))
	{
		//DoTraceMessage(TRACE_STATE_CHANGE, "there are no network activity");
		KeReleaseInStackQueuedSpinLock(&lockHandle);
		return STATUS_SUCCESS;
	}

	entry = &kernelInforList;
	while ((entry = entry->Blink) != &kernelInforList)
	{
		willRetrivedInfor = CONTAINING_RECORD(entry, MONITOR_KERNEL_INFORMATION, listEntry);
		RtlCopyMemory(&informations->monitorInformation[undex], &willRetrivedInfor->information, SIZEOF_MONITOR_INFORMATION);
		willRetrivedInfor->information.sentBytes = willRetrivedInfor->information.receivedBytes = 0;
		undex++;
	}

	ASSERT(undex == numKernelInfor);

	informations->numMonitorInformations = undex;

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	return STATUS_SUCCESS;
}

NTSTATUS RetrieveInfor(_Out_ MONITOR_INFORMATION* information)
{
	KLOCK_QUEUE_HANDLE lockHandle;

	if (gMonitorInfor == NULL)
	{
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireInStackQueuedSpinLock(&flowContextIncreLock, &lockHandle);

	information->receivedBytes = gMonitorInfor->receivedBytes - gReceivedBytes;
	information->sentBytes = gMonitorInfor->sentBytes - gSentBytes;

	gSentBytes = gMonitorInfor->sentBytes;
	gReceivedBytes = gMonitorInfor->receivedBytes;

	KeReleaseInStackQueuedSpinLock(&lockHandle);

	return STATUS_SUCCESS;
}

NTSTATUS DealWithMntSettings(_In_ MONITOR_SETTINGS* settings)
{
	DoTraceMessage(TRACE_STATE_CHANGE, "Deal with Monitor settings...");
	NTSTATUS status = STATUS_SUCCESS;
	if (!settings)
	{
		return STATUS_INVALID_PARAMETER;
	}

	for (INT idx = 0; idx < DEFAULT_EVENT_NUM - 1; idx++)
	{
		if (settings->hInforEvents[idx] == NULL || gEvents[idx])
		{
			DoTraceMessage(TRACE_ERROR_OCCUR, "Unexpect event init %d.", idx);
			return STATUS_INVALID_PARAMETER;
		}
		status = ObReferenceObjectByHandle(settings->hInforEvents[idx], EVENT_MODIFY_STATE, *ExEventObjectType,
			KernelMode, (PVOID*)&gEvents[idx], NULL);
		if (!NT_SUCCESS(status))
		{
			DoTraceMessage(TRACE_ERROR_OCCUR, "Unexpect event ObReferenceObjectByHandle %d.", idx);
			return STATUS_INVALID_PARAMETER;
		}
	}
	DoTraceMessage(TRACE_STATE_CHANGE, "Deal with Monitor settings finished.");
	return STATUS_SUCCESS;
}

NTSTATUS MonitorSetEnableMonitoring(_In_ MONITOR_SETTINGS* settings)
{
	KLOCK_QUEUE_HANDLE lockQueueHandle;

	DealWithMntSettings(settings);

	DoTraceMessage(TRACE_STATE_CHANGE, "Enabling monitoring(setEnableFlag and zeroMemory monitorInfor).");
	KeAcquireInStackQueuedSpinLock(&flowContextListLock, &lockQueueHandle);
	monitoringEnabled = 1;
	//-- 2016-2-6 Joseph
	RtlZeroMemory(gMonitorInfor, sizeof(MONITOR_INFORMATION));
	gSentBytes = gReceivedBytes = 0;
	KeReleaseInStackQueuedSpinLock(&lockQueueHandle);
	return STATUS_SUCCESS;
}

void MonitorSetDisableMonitoring(void)
{
	KLOCK_QUEUE_HANDLE lockQueueHandle;
	DoTraceMessage(TRACE_STATE_CHANGE, "Disabling monitoring (set unEnable flag)");
	KeAcquireInStackQueuedSpinLock(&flowContextListLock, &lockQueueHandle);
	monitoringEnabled = 0;
	KeReleaseInStackQueuedSpinLock(&lockQueueHandle);
	
	DoTraceMessage(TRACE_STATE_CHANGE, "Total Sent %I64d bytes, Received %I64d bytes, Together %I64d bytes",
		gMonitorInfor->sentBytes, gMonitorInfor->receivedBytes, gMonitorInfor->receivedBytes + gMonitorInfor->sentBytes);
}

//Initialize flow tracking
NTSTATUS MonitorProInitialize(_Inout_ DEVICE_OBJECT* deviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;

	//2016-12-30
	//deviceObject->Flags |= DO_BUFFERED_IO;

	InitializeListHead(&flowContextList);
	InitializeListHead(&kernelInforList);
	KeInitializeSpinLock(&flowContextListLock);
	KeInitializeSpinLock(&flowContextIncreLock);
	KeInitializeSpinLock(&kernelInforLock);
	numKernelInfor = 0;
	//-- 2016-2-6 Joseph ++2016-2-15 
	gMonitorInfor = NULL;
	gMonitorInfor = ExAllocatePoolWithTag(NonPagedPool, sizeof(MONITOR_INFORMATION), TAG_NAME_CONTEXT);
	if (!gMonitorInfor)
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Run 'ExAllocatePoolWithTag' for MONITOR_INFORMATION Function failed");
		return STATUS_NO_MEMORY;
	}
	RtlZeroMemory(gMonitorInfor, sizeof(MONITOR_INFORMATION));

	//2017-01-02
	for (INT idx = 0; idx < DEFAULT_EVENT_NUM - 1; idx++)
	{
		gEvents[idx] = NULL;
	}

	status = RegisterCallouts(deviceObject);
	return status;
}

void MonitorProUninitialize(void)
{
	LIST_ENTRY list;
	NTSTATUS status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE lockHandle;
	MonitorSetDisableMonitoring();
	InitializeListHead(&list);

	KeAcquireInStackQueuedSpinLock(&flowContextListLock, &lockHandle);
	while (!IsListEmpty(&flowContextList))
	{
		FLOW_DATA* curFlowData;
		LIST_ENTRY* entry;
		entry = RemoveHeadList(&flowContextList);
		curFlowData = CONTAINING_RECORD(entry, FLOW_DATA, listEntry);
		//Prevent FlowData from being deleted twice, so mark it!
		curFlowData->deleting = TRUE;
		InsertHeadList(&list, entry);
	}
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	//-- 2016-2-6 Joseph
	ExFreePoolWithTag(gMonitorInfor, TAG_NAME_CONTEXT);
	gMonitorInfor = NULL;
	
	while (!IsListEmpty(&list))
	{
		FLOW_DATA* curFlowData;
		LIST_ENTRY* entry;
		
		entry = RemoveHeadList(&list);
		curFlowData = CONTAINING_RECORD(entry, FLOW_DATA, listEntry);

		status = FwpsFlowRemoveContext(curFlowData->flowHandle, FWPS_LAYER_STREAM_V4, streamCalloutId);
		// If the FwpsFlowRemoveContext0 function returns STATUS_SUCCESS, 
		// FwpsFlowRemoveContext0 calls the flowDeleteFn callout function synchronously
		NT_ASSERT(NT_SUCCESS(status));

		//// Without this you'll get warning
		//_Analysis_assume_(NT_SUCCESS(status));
	}

	KeAcquireInStackQueuedSpinLock(&kernelInforLock, &lockHandle);
	while (!IsListEmpty(&kernelInforList))
	{
		PMONITOR_KERNEL_INFORMATION kernelInfo;
		LIST_ENTRY* entry;

		entry = RemoveHeadList(&kernelInforList);
		kernelInfo = CONTAINING_RECORD(entry, MONITOR_KERNEL_INFORMATION, listEntry);
		CleanupKernelInfor(kernelInfo);
		numKernelInfor--;
	}
	KeReleaseInStackQueuedSpinLock(&lockHandle);

	UnregisterCallouts();
}

NTSTATUS RegisterCallouts(_Inout_ void* deviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = RegisterCallout(deviceObject,
		FlowEstablishClassfiyFn,
		FlowEstablishNotifyFn,
		NULL,
		&NETWORK_MONITOR_FLOW_ESTABLISHED_CALLOUT_V4,
		0,
		&flowEstablishedCalloutId);


	if (NT_SUCCESS(status))
	{
		status = RegisterCallout(deviceObject,
			StreamClassfiyFn,
			StreamNotifyFn,
			StreamFlowDeleteFn,
			&NETWORK_MONITOR_STREAM_CALLOUT_V4,
			FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW,
			&streamCalloutId);
	}

	return status;
}


NTSTATUS UnregisterCallouts(void)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = FwpsCalloutUnregisterByKey(&NETWORK_MONITOR_FLOW_ESTABLISHED_CALLOUT_V4);
	if (NT_SUCCESS(status))
	{
		status = FwpsCalloutUnregisterByKey(&NETWORK_MONITOR_STREAM_CALLOUT_V4);
	}
	return status;
}

NTSTATUS RegisterCallout(
	_Inout_ void* deviceObject,
	_In_ FWPS_CALLOUT_CLASSIFY_FN ClassifyFunction,
	_In_ FWPS_CALLOUT_NOTIFY_FN NotifyFunction,
	_In_opt_ FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN FlowDeleteFunction,
	_In_ const GUID* calloutKey,
	_In_ UINT32 flags,
	_Out_ UINT32* calloutId
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	//https://msdn.microsoft.com/zh-cn/library/windows/apps/ff551224
	FWPS_CALLOUT sCallout;

	memset(&sCallout, 0, sizeof(FWPS_CALLOUT));
	//A callout driver-defined GUID that uniquely identifies the callout.
	sCallout.calloutKey = *calloutKey;
	sCallout.flags = flags;
	sCallout.classifyFn = ClassifyFunction;
	sCallout.notifyFn = NotifyFunction;
	sCallout.flowDeleteFn = FlowDeleteFunction;

	status = FwpsCalloutRegister(deviceObject, &sCallout, calloutId);
	return status;
}

//Invoke function whenever there is data to be processed by the callout.
/*
inFixeedValues: 当前过滤层数据域的值
inMetaValues: 当前过滤层元数据域的值
layerdata: 原生数据，是否为空取决于当前过滤层和该函数被调用时的状态，对于数据层，指向 FWPS_STREAM_CALLOUT_IO_PACKET
			其他层则指向 NET_BUFFER_LIST 类型
*/


#if (NTDDI_VERSION >= NTDDI_WIN7)

void FlowEstablishClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	)
#else

void FlowEstablishClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	)
#endif // (NTDDI_VERSION >= NTDDI_WIN7)
{
	NTSTATUS status = STATUS_SUCCESS;
	UINT64   flowHandle;
	UINT64   flowContextLocal;

	UNREFERENCED_PARAMETER(layerData);
#if(NTDDI_VERSION >= NTDDI_WIN7)
	UNREFERENCED_PARAMETER(classifyContext);
#endif /// (NTDDI_VERSION >= NTDDI_WIN7)
	UNREFERENCED_PARAMETER(flowContext);

	if (monitoringEnabled)
	{
		flowContextLocal = CreateFlowContext(inFixedValues, inMetaValues, &flowHandle);
		if (!flowContextLocal)
		{
			classifyOut->actionType = FWP_ACTION_CONTINUE;
			return;
		}
		//绑定数据流至上下文
		//数据流标识，层标识，callout标识，
		//layerId必须有注册flowDeleteFn函数，否则将返回 STATUS_INVALID_PARAMETER，流结束后可调用DeleteFn函数
		status = FwpsFlowAssociateContext(flowHandle, FWPS_LAYER_STREAM_V4, streamCalloutId, flowContextLocal);
		if (!NT_SUCCESS(status))
		{
			classifyOut->actionType = FWP_ACTION_CONTINUE;
			return;
		}
	}
	classifyOut->actionType = FWP_ACTION_PERMIT;

	//https://msdn.microsoft.com/en-us/library/windows/hardware/hh439337(v=vs.85).aspx
	if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
	{
		classifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
	}

	return;
}


#if (NTDDI_VERSION >= NTDDI_WIN7)

void StreamClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	)
#else
void StreamClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	)
#endif // (NTDDI_VERSION >= NTDDI_WIN7)
{
	FLOW_DATA* flowData;
	FWPS_STREAM_CALLOUT_IO_PACKET* streamPacket;
	KLOCK_QUEUE_HANDLE lockHandle;
	BOOLEAN inbound;
	//KLOCK_QUEUE_HANDLE lockHandle;
	UNREFERENCED_PARAMETER(inFixedValues);
	UNREFERENCED_PARAMETER(inMetaValues);
#if(NTDDI_VERSION >= NTDDI_WIN7)
	UNREFERENCED_PARAMETER(classifyContext);
#endif /// (NTDDI_VERSION >= NTDDI_WIN7)
	UNREFERENCED_PARAMETER(filter);

	_Analysis_assume_(layerData != NULL);

	if (monitoringEnabled)
	{
		streamPacket = (FWPS_STREAM_CALLOUT_IO_PACKET*)layerData;
		if (streamPacket->streamData != NULL && streamPacket->streamData->dataLength != 0)
		{
			//??????????????????????
			flowData = *(FLOW_DATA**)(UINT64*)&flowContext;
			inbound = (BOOLEAN)((streamPacket->streamData->flags & FWPS_STREAM_FLAG_RECEIVE) == FWPS_STREAM_FLAG_RECEIVE);
			//status = MonitorNfNotifyMessage(streamPacket->streamData, inbound, flowData->localPort,flowData->remotePort);

			KeAcquireInStackQueuedSpinLock(&flowContextIncreLock, &lockHandle);
			if (inbound)
			{
				gMonitorInfor->receivedBytes += streamPacket->streamData->dataLength;
			}
			else
			{
				gMonitorInfor->sentBytes += streamPacket->streamData->dataLength;
			}
			KeReleaseInStackQueuedSpinLock(&lockHandle);

			if (inbound)
			{
				InsertIntoInfoList(flowData, 0, streamPacket->streamData->dataLength, FALSE);
			}
			else
			{
				InsertIntoInfoList(flowData, streamPacket->streamData->dataLength, 0, FALSE);
			}
		}
	}
	classifyOut->actionType = FWP_ACTION_CONTINUE;
	return;
}

NTSTATUS FlowEstablishNotifyFn(_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey, _Inout_ FWPS_FILTER* filter
	)
{
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);
	switch (notifyType)
	{
	case FWPS_CALLOUT_NOTIFY_ADD_FILTER:
		DoTraceMessage(TRACE_LAYER_NOTIFY, "Filter Added to Flow Established layer.\r\n");
		break;

	case FWPS_CALLOUT_NOTIFY_DELETE_FILTER:
		DoTraceMessage(TRACE_LAYER_NOTIFY, "Filter Deleted from Flow Established layer.\r\n");
		break;
	}
	return STATUS_SUCCESS;
}

NTSTATUS StreamNotifyFn(_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey, _Inout_ FWPS_FILTER* filter
	)
{
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);
	switch (notifyType)
	{
	case FWPS_CALLOUT_NOTIFY_ADD_FILTER:
		DoTraceMessage(TRACE_LAYER_NOTIFY, "Filter Added to Stream layer.\r\n");
		break;

	case FWPS_CALLOUT_NOTIFY_DELETE_FILTER:
		DoTraceMessage(TRACE_LAYER_NOTIFY, "Filter Deleted from Stream layer.\r\n");
		break;
	}
	return STATUS_SUCCESS;
}

void StreamFlowDeleteFn(_In_ UINT16 layerId, _In_ UINT32 calloutId, _In_ UINT64 flowContext)
{
	KLOCK_QUEUE_HANDLE lockHandle;
	FLOW_DATA* flowData;
	HRESULT result;
	ULONG_PTR flowPtr;
	//PMONITOR_KERNEL_INFORMATION specifiyInfor = NULL;

	UNREFERENCED_PARAMETER(layerId);
	UNREFERENCED_PARAMETER(calloutId);

	//????????????????????????????
	result = ULongLongToULongPtr(flowContext, &flowPtr);
	ASSERT(result == S_OK);
	_Analysis_assume_(result == S_OK);

	//当初赋值给flowContext时仅仅是FLOW_DATA*类型
	flowData = ((FLOW_DATA*)flowPtr);

	KeAcquireInStackQueuedSpinLock(&flowContextListLock, &lockHandle);
	if (!flowData->deleting)
	{
		RemoveEntryList(&flowData->listEntry);
	}
	KeReleaseInStackQueuedSpinLock(&lockHandle);

	//DoTraceMessage(TRACE_STREAM_MONITOR, "Remove flowContext");

	//KeAcquireInStackQueuedSpinLock(&kernelInforLock, &lockHandle);
	//if (NT_SUCCESS(FindSpecifyKernelInfo(flowData, &specifiyInfor)))
	//{
	//	RemoveEntryList(&specifiyInfor->listEntry);
	//	CleanupKernelInfor(specifiyInfor);
	//	numKernelInfor--;
	//}
	//KeReleaseInStackQueuedSpinLock(&lockHandle);
	
	CleanupFlowContext(flowData);

}
//flowHandle: [out] flowId
UINT64 CreateFlowContext(_In_ const FWPS_INCOMING_VALUES* inFixedValues,_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,_Out_ UINT64* flowHandle)
{
	FLOW_DATA*		flowContext = NULL;
	NTSTATUS		status = STATUS_SUCCESS;
	FWP_BYTE_BLOB*	processPath;
	UINT32			index;
	WCHAR			processName[256] = { 0 };
	WCHAR*			pointProcessName = NULL;

	*flowHandle = 0;
	if (!FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues, FWPS_METADATA_FIELD_PROCESS_PATH))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Cannot found processPath in metalValues");
		return (UINT64 )NULL;
	}
	if (!FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues, FWPS_METADATA_FIELD_FLOW_HANDLE))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Cannot found flowHandle in metalValues");
		return (UINT64)NULL;
	}

	processPath = inMetaValues->processPath;
	status = AllocFlowContext(processPath->size, &flowContext);
	
	if (!NT_SUCCESS(status))
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Allocate flowContext failed with status 0x%08x", status);
		return (UINT64)NULL;
	}

	*flowHandle = inMetaValues->flowHandle;
	flowContext->deleting = FALSE;
	flowContext->flowHandle = inMetaValues->flowHandle;

	//index = FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS;
	//flowContext->localAddressV4 = inFixedValues->incomingValue[index].value.uint32;

	//index = FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_PORT;
	//flowContext->localPort = inFixedValues->incomingValue[index].value.uint16;

	//index = FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_ADDRESS;
	//flowContext->remoteAddressV4 = inFixedValues->incomingValue[index].value.uint32;

	//index = FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_PORT;
	//flowContext->remotePort = inFixedValues->incomingValue[index].value.uint16;

	index = FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_PROTOCOL;
	flowContext->ipProto = inFixedValues->incomingValue[index].value.uint16;

	// flowContext->processPath gets deleted in MonitorCoCleanupFlowContext 
	//++ 2016-2-6 Joseph
	RtlCopyMemory(processName, processPath->data, processPath->size);
	pointProcessName = wcsrchr(processName, '\\');
	if (pointProcessName == NULL)
	{
		flowContext->processPathSize = processPath->size;
		RtlZeroMemory(flowContext->processPath, sizeof(flowContext->processPath));
		RtlCopyMemory(flowContext->processPath, processPath->data, processPath->size);
	}
	else
	{
		pointProcessName += 1;
		flowContext->processPathSize = (UINT32)wcslen(pointProcessName) * 2;
		RtlZeroMemory(flowContext->processPath, sizeof(flowContext->processPath));
		RtlCopyMemory(flowContext->processPath, pointProcessName, flowContext->processPathSize);
	}


	//RtlCopyMemory(flowContext->processPath, processPath->data, processPath->size);
	//flowContext->processPathSize = processPath->size;
	//--
	//memcpy(flowContext->processPath, processPath->data, processPath->size);
	flowContext->processId = inMetaValues->processId;

	DoTraceMessage(TRACE_FLOW_ESTABLISHED, L"ProcessPath %ws ProcessName %ws, ProcessId %I64d", processName, flowContext->processPath, flowContext->processId);

	status = InsertIntoFlowContext(flowContext);

	if (!NT_SUCCESS(status))
	{
		CleanupFlowContext(flowContext);
		flowContext = NULL;
	}

	InsertIntoInfoList(flowContext, 0, 0, TRUE);
	return (UINT64)flowContext;
}

//2016-12-28 processPathSize noused, allocate sizeof(FLOW_DATA)
NTSTATUS AllocFlowContext(_In_ SIZE_T processPathSize, _Out_ FLOW_DATA** flowContextOut)
{
	NTSTATUS status = STATUS_SUCCESS;
	FLOW_DATA* flowContext = NULL;
	UNREFERENCED_PARAMETER(processPathSize);

	*flowContextOut = NULL;
	flowContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(FLOW_DATA), TAG_NAME_CONTEXT);
	if (!flowContext)
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Run 'ExAllocatePoolWithTag' for flowContext Function failed");
		return STATUS_NO_MEMORY;
	}
	RtlZeroMemory(flowContext, sizeof(FLOW_DATA));
	//2016-2-15 -- Joseph
	//flowContext->processPath = ExAllocatePoolWithTag(NonPagedPool, processPathSize, TAG_NAME_CONTEXT);
	//if (!flowContext->processPath)
	//{
	//	DoTraceMessage(TRACE_ERROR_OCCUR, "Run 'ExAllocatePoolWithTag' Function for processPath failed");
	//	if (flowContext) ExFreePoolWithTag(flowContext, TAG_NAME_CONTEXT);
	//	return STATUS_NO_MEMORY;
	//}

	*flowContextOut = flowContext;
	return status;
}

NTSTATUS InsertIntoInfoList(_In_ FLOW_DATA* flowData, SIZE_T outBound, SIZE_T inBound, BOOL bEstablished)
{
	KLOCK_QUEUE_HANDLE lockHandle;
	NTSTATUS status = STATUS_SUCCESS;
	PMONITOR_KERNEL_INFORMATION kernelInfor = NULL;

	//++ 2016-2-14 Joseph
	KeAcquireInStackQueuedSpinLock(&kernelInforLock, &lockHandle);
	if (monitoringEnabled)
	{
		//2017-01-02 Test
		DoTraceMessage(TRACE_FLOW_ESTABLISHED, "Event Test.");
		KeSetEvent(gEvents[0], 0, FALSE);

		status = FindSpecifyKernelInfo(flowData, &kernelInfor);
		//DoTraceMessage(TRACE_FLOW_ESTABLISHED, "FindSpecifyKernelInfo 0x%08x", status);
		if (!NT_SUCCESS(status) && bEstablished)
		{
			status = AllocateAndSetKernelInfo(flowData, &kernelInfor);
			//DoTraceMessage(TRACE_FLOW_ESTABLISHED, "AllocateAndSetKernelInfo %I64d return 0x%08x", flowData->processId, status);
			if (NT_SUCCESS(status))
			{
				InsertTailList(&kernelInforList, &kernelInfor->listEntry);
				numKernelInfor++;
			}
		}
		else if (NT_SUCCESS(status))
		{
			//DoTraceMessage(TRACE_FLOW_ESTABLISHED, "found processId %I64d", kernelInfor->information.processId);
			kernelInfor->information.totalRecvBytes += inBound;
			kernelInfor->information.totalSetnBytes += outBound;
			kernelInfor->information.sentBytes += outBound;
			kernelInfor->information.receivedBytes += inBound;
		}
		else
		{
			DoTraceMessage(TRACE_FLOW_ESTABLISHED, "InsertIntoInfoList Unexpected PId: %I64d, status: 0x%08x", flowData->processId, status);
		}
	}
	else
	{
		DoTraceMessage(TRACE_FLOW_ESTABLISHED, "Unable to create Info and Inert, driver shutting down");
		// Our driver is shutting down.
		status = STATUS_SHUTDOWN_IN_PROGRESS;
	}
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	return status;
}

NTSTATUS InsertIntoFlowContext(_Inout_ FLOW_DATA* flowContext)
{
	KLOCK_QUEUE_HANDLE lockHandle;
	NTSTATUS status = STATUS_SUCCESS;
	
	KeAcquireInStackQueuedSpinLock(&flowContextListLock, &lockHandle);
	if (monitoringEnabled)
	{
		//DoTraceMessage(TRACE_FLOW_ESTABLISHED, "Creating flow for traffic.\r\n");
		InsertTailList(&flowContextList, &flowContext->listEntry);
		status = STATUS_SUCCESS;
	}
	else
	{
		DoTraceMessage(TRACE_FLOW_ESTABLISHED, "Unable to create flow, driver shutting down");
		// Our driver is shutting down.
		status = STATUS_SHUTDOWN_IN_PROGRESS;
	}
	KeReleaseInStackQueuedSpinLock(&lockHandle);

	return status;
}

VOID CleanupFlowContext(_In_ __drv_freesMem(Mem) FLOW_DATA* flowContext)
{
	//-- 2016-2-15
	//if (flowContext->processPath)
	//{
	//	ExFreePoolWithTag(flowContext->processPath, TAG_NAME_CONTEXT);
	//}
	ExFreePoolWithTag(flowContext, TAG_NAME_CONTEXT);
}

NTSTATUS AllocateAndSetKernelInfo(_In_ PFLOW_DATA flowData, _Out_ MONITOR_KERNEL_INFORMATION** kernelInforOut)
{
	PMONITOR_KERNEL_INFORMATION kernelInfor = NULL;
	if (flowData == NULL)
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "flowData is empty in 'ExAllocatePoolWithTag' for kernelInfo Function ");
		return STATUS_INVALID_PARAMETER;
	}
	*kernelInforOut = NULL;
	kernelInfor = ExAllocatePoolWithTag(NonPagedPool, sizeof(MONITOR_KERNEL_INFORMATION), TAG_NAME_INFO);
	if (!kernelInfor)
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "Run 'ExAllocatePoolWithTag' for kernelInfo Function failed");
		return STATUS_NO_MEMORY;
	}
	//kernelInfor->information = ExAllocatePoolWithTag(NonPagedPool, sizeof(MONITOR_INFORMATION), TAG_NAME_INFO);
	//if (!(kernelInfor->information))
	//{
	//	DoTraceMessage(TRACE_ERROR_OCCUR, "Run 'ExAllocatePoolWithTag' for kernelInfo->infor Function failed");
	//	ExFreePoolWithTag(kernelInfor, TAG_NAME_INFO);
	//	return STATUS_NO_MEMORY;
	//}
	//kernelInfor->information->processPath = ExAllocatePoolWithTag(NonPagedPool, flowData->processPathSize, TAG_NAME_INFO);
	//if (!(kernelInfor->information->processPath))
	//{
	//	DoTraceMessage(TRACE_ERROR_OCCUR, "Run 'ExAllocatePoolWithTag' for infor->processpath Function failed");
	//	ExFreePoolWithTag(kernelInfor->information, TAG_NAME_INFO);
	//	ExFreePoolWithTag(kernelInfor, TAG_NAME_INFO);
	//	return STATUS_NO_MEMORY;
	//}

	//kernelInfor->information->localAddressV4 = flowData->localAddressV4;
	//kernelInfor->information->localPort = flowData->localPort;
	kernelInfor->information.ipProto = flowData->ipProto;
	//kernelInfor->information->remoteAddressV4 = flowData->remoteAddressV4;
	//kernelInfor->information->remotePort = flowData->remotePort;

	RtlZeroMemory(kernelInfor->information.processPath, sizeof(kernelInfor->information.processPath));
	RtlCopyMemory(kernelInfor->information.processPath, flowData->processPath, flowData->processPathSize);
	kernelInfor->information.processId = flowData->processId;
	kernelInfor->deleting = FALSE;
	kernelInfor->information.sentBytes = kernelInfor->information.receivedBytes = 0;
	kernelInfor->information.totalSetnBytes = kernelInfor->information.totalRecvBytes = 0;
	*kernelInforOut = kernelInfor;
	return STATUS_SUCCESS;
}

//2017-01-02 change _FindSpecifyKernelInfo to FindSpecifyKernelInfo
NTSTATUS FindSpecifyKernelInfo(_In_ PFLOW_DATA flowData, _Out_ MONITOR_KERNEL_INFORMATION** kernelInforOut)
{
	PLIST_ENTRY headListEntry = &kernelInforList;
	PLIST_ENTRY tempListEntry = &kernelInforList;
	PMONITOR_KERNEL_INFORMATION curKernelInfo;
	if (flowData == NULL)
	{
		DoTraceMessage(TRACE_ERROR_OCCUR, "flowData is empty in  FindSpecifyKernelInfo routine");
		return STATUS_INVALID_PARAMETER;
	}
	*kernelInforOut = NULL;
	while ((tempListEntry = tempListEntry->Blink) != headListEntry)
	{
		curKernelInfo = CONTAINING_RECORD(tempListEntry, MONITOR_KERNEL_INFORMATION, listEntry);
		if (curKernelInfo->information.processId == flowData->processId)
		{
			*kernelInforOut = curKernelInfo;
			return STATUS_SUCCESS;
		}
	}
	return STATUS_NO_MATCH;
}

void CleanupKernelInfor(_In_ __drv_freesMem(Mem) PMONITOR_KERNEL_INFORMATION infor)
{
	if (infor == NULL)
	{
		return;
	}

	//if (infor->information)
	//{
	//	if (infor->information->processPath)
	//	{
	//		ExFreePoolWithTag(infor->information->processPath, TAG_NAME_INFO);
	//		infor->information->processPath = NULL;
	//	}
	//	ExFreePoolWithTag(infor->information, TAG_NAME_INFO);
	//	infor->information = NULL;
	//}
	ExFreePoolWithTag(infor, TAG_NAME_INFO);
	infor = NULL;
}

VOID ProcessCreateNotifyRoutie(IN HANDLE ParentId, IN HANDLE ProcessId, IN BOOLEAN Create)
{
	UINT64 processId = (UINT64)ProcessId;
	UNREFERENCED_PARAMETER(ParentId);
	if (monitoringEnabled && !Create)
	{
		KLOCK_QUEUE_HANDLE lockHandle;
		PLIST_ENTRY tempListEntry = &kernelInforList;
		PMONITOR_KERNEL_INFORMATION curKernelInfo = NULL;

		KeAcquireInStackQueuedSpinLock(&kernelInforLock, &lockHandle);

		while ((tempListEntry = tempListEntry->Blink) != &kernelInforList)
		{
			curKernelInfo = CONTAINING_RECORD(tempListEntry, MONITOR_KERNEL_INFORMATION, listEntry);
			if (curKernelInfo->information.processId == processId)
			{
				Create = TRUE;
				break;
			}
		}
		if (Create)
		{
			DoTraceMessage(TRACE_FLOW_ESTABLISHED, "Event Test remove process.");
			KeSetEvent(gEvents[1], 0, FALSE);

			RemoveEntryList(&curKernelInfo->listEntry);
			CleanupKernelInfor(curKernelInfo);
			numKernelInfor--;
			DoTraceMessage(TRACE_ERROR_OCCUR, "Delete ProcessId %I64d", processId);
		}
		KeReleaseInStackQueuedSpinLock(&lockHandle);
	}
}