#pragma once;

#include <ndis.h>
#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>

#include <fwpmk.h>

#pragma warning(push)
#pragma warning(disable:4201)       // unnamed struct/union

#include <fwpsk.h>

#pragma warning(pop)

#include "Common.h"

typedef struct _FLOW_DATA
{
	LIST_ENTRY  listEntry;
	UINT64      flowHandle;
	UINT64      flowContext;
	UINT64      calloutId;
	USHORT      ipProto;
	WCHAR		processPath[MAX_PROCESS_PATH_SIZE];
	UINT32		processPathSize;
	UINT64		processId;
	BOOLEAN     deleting;

} FLOW_DATA, *PFLOW_DATA;

typedef struct _MONITOR_KERNEL_INFORMATION
{
	LIST_ENTRY				listEntry;
	MONITOR_INFORMATION		information;
	BOOLEAN					deleting;

}MONITOR_KERNEL_INFORMATION, *PMONITOR_KERNEL_INFORMATION;

#define EVENT_TAG (ULONG)'TEVE'

typedef struct _DEVICE_CONTEXT
{
	WDFQUEUE DefaultQueue;
	LIST_ENTRY EventQueueHead; // where all the user notification requests are queued
	KSPIN_LOCK QueueLock;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;
//定义全局数据结构并创建驱动程序可调用例程，所以驱动程序必须在声明全局数据（通常为头文件）的驱动程序区域内
//创建并初始化 WDF_OBJECT_CONTEXT_TYPE_INFO 结构
//定义驱动程序以后将用来访问对象的上下文空间的访问器方法，该访问器方法的返回值是指向对象的上下文空间的指针
WDF_DECLARE_CONTEXT_TYPE(DEVICE_CONTEXT);

typedef struct _NOTIFY_RECORD{
	
	LIST_ENTRY ListEntry;
	PKEVENT Event;
	PDEVICE_CONTEXT deviceContext;

} NOTIFY_RECORD, *PNOTIFY_RECORD;

NTSTATUS RetrieveInfor(_Out_ MONITOR_INFORMATION* information);

NTSTATUS RetrieveInfors(_Out_ MONITOR_INFORMATIONS* informations);

//将FlowContext节点插入到链表
NTSTATUS InsertIntoFlowContext(_Inout_ FLOW_DATA* flowContext);

//给FlowContext分配空间
NTSTATUS AllocFlowContext(_In_ SIZE_T processPathSize, _Out_ FLOW_DATA** flowContextOut);

NTSTATUS InsertIntoInfoList(_In_ FLOW_DATA* flowData, SIZE_T outBound, SIZE_T inBound, BOOL bEstablished);

//生成FlowContext内容
UINT64 CreateFlowContext(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Out_ UINT64* flowHandle);

#if (NTDDI_VERSION >= NTDDI_WIN7)

void FlowEstablishClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	);

void StreamClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	);
#else

void FlowEstablishClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	);

void StreamClassfiyFn(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
	);

#endif // (NTDDI_VERSION >= NTDDI_WIN7)


NTSTATUS FlowEstablishNotifyFn(_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey, _Inout_ FWPS_FILTER* filter
	);

NTSTATUS StreamNotifyFn(_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey, _Inout_ FWPS_FILTER* filter
	);

//销毁分配给FlowContext的空间
VOID CleanupFlowContext(_In_ __drv_freesMem(Mem) FLOW_DATA* flowContext);

void StreamFlowDeleteFn(_In_ UINT16 layerId, _In_ UINT32 calloutId, _In_ UINT64 flowContext);

NTSTATUS RegisterCallout(
	_Inout_ void* deviceObject,
	_In_ FWPS_CALLOUT_CLASSIFY_FN ClassifyFunction,
	_In_ FWPS_CALLOUT_NOTIFY_FN NotifyFunction,
	_In_opt_ FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN FlowDeleteFunction,
	_In_ const GUID* calloutKey,
	_In_ UINT32 flags,
	_Out_ UINT32* calloutId
	);

NTSTATUS MonitorSetEnableMonitoring(_In_ MONITOR_SETTINGS* monitorSettings);
void MonitorSetDisableMonitoring(void);

NTSTATUS RegisterCallouts(_Inout_ void* deviceObject);
NTSTATUS UnregisterCallouts(void);

NTSTATUS MonitorProInitialize(_Inout_ DEVICE_OBJECT* deviceObject);
void MonitorProUninitialize(void);

NTSTATUS AllocateAndSetKernelInfo(_In_ PFLOW_DATA flowData, _Out_ MONITOR_KERNEL_INFORMATION** kernelInforOut);

NTSTATUS FindSpecifyKernelInfo(_In_ PFLOW_DATA flowData, _Out_ MONITOR_KERNEL_INFORMATION** kernelInforOut);

//销毁分配给KernelMonitorInfor的空间
void CleanupKernelInfor(_In_ __drv_freesMem(Mem) PMONITOR_KERNEL_INFORMATION infor);


VOID ProcessCreateNotifyRoutie(IN HANDLE ParentId, IN HANDLE ProcessId, IN BOOLEAN Create);