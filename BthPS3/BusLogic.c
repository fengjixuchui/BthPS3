/*
* BthPS3 - Windows kernel-mode Bluetooth profile and bus driver
* Copyright (C) 2018-2019  Nefarius Software Solutions e.U. and Contributors
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "Driver.h"
#include "buslogic.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BthPS3_EvtWdfChildListCreateDevice)
#endif


//
// Spawn new child device (PDO)
// 
_Use_decl_annotations_
NTSTATUS
BthPS3_EvtWdfChildListCreateDevice(
    WDFCHILDLIST ChildList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    PWDFDEVICE_INIT ChildInit
)
{
    NTSTATUS                            status = STATUS_UNSUCCESSFUL;
    PPDO_IDENTIFICATION_DESCRIPTION     pDesc;
    UNICODE_STRING                      guidString;
    WDFDEVICE                           hChild = NULL;
    WDF_IO_QUEUE_CONFIG                 defaultQueueCfg;
    WDFQUEUE                            defaultQueue;
    WDF_OBJECT_ATTRIBUTES               attributes;
    PBTHPS3_PDO_DEVICE_CONTEXT          pdoCtx = NULL;

    DECLARE_UNICODE_STRING_SIZE(deviceId, MAX_DEVICE_ID_LEN);
    DECLARE_UNICODE_STRING_SIZE(hardwareId, MAX_DEVICE_ID_LEN);
    DECLARE_UNICODE_STRING_SIZE(instanceId, BTH_ADDR_HEX_LEN);

    UNREFERENCED_PARAMETER(ChildList);

    PAGED_CODE();


    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Entry");

    pDesc = CONTAINING_RECORD(
        IdentificationDescription,
        PDO_IDENTIFICATION_DESCRIPTION,
        Header);

    //
    // PDO features
    // 
    WdfDeviceInitSetDeviceType(ChildInit, FILE_DEVICE_BUS_EXTENDER);

    //
    // Parent FDO will handle IRP_MJ_INTERNAL_DEVICE_CONTROL
    // 
    WdfPdoInitAllowForwardingRequestToParent(ChildInit);

    //
    // Adjust properties depending on device type
    // 
    switch (pDesc->ClientConnection->DeviceType)
    {
    case DS_DEVICE_TYPE_SIXAXIS:
        status = RtlStringFromGUID(&BTHPS3_BUSENUM_SIXAXIS,
            &guidString
        );
        break;
    case DS_DEVICE_TYPE_NAVIGATION:
        status = RtlStringFromGUID(&BTHPS3_BUSENUM_NAVIGATION,
            &guidString
        );
        break;
    case DS_DEVICE_TYPE_MOTION:
        status = RtlStringFromGUID(&BTHPS3_BUSENUM_MOTION,
            &guidString
        );
        break;
    case DS_DEVICE_TYPE_WIRELESS:
        status = RtlStringFromGUID(&BTHPS3_BUSENUM_WIRELESS,
            &guidString
        );
        break;
    default:
        // Doesn't happen
        return status;
    }

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "RtlStringFromGUID failed with status %!STATUS!",
            status
        );
        return status;
    }

#pragma region Build DeviceID

    status = RtlUnicodeStringPrintf(
        &deviceId,
        L"%ws\\%wZ",
        BthPS3BusEnumeratorName,
        guidString
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "RtlUnicodeStringPrintf failed for deviceId with status %!STATUS!",
            status
        );
        goto freeAndExit;
    }

    status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "WdfPdoInitAssignDeviceID failed with status %!STATUS!",
            status);
        goto freeAndExit;
    }

#pragma endregion

#pragma region Build HardwareID

    status = RtlUnicodeStringPrintf(
        &hardwareId,
        L"%ws\\%wZ",
        BthPS3BusEnumeratorName,
        guidString
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "RtlUnicodeStringPrintf failed for hardwareId with status %!STATUS!",
            status
        );
        goto freeAndExit;
    }

    status = WdfPdoInitAddHardwareID(ChildInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "WdfPdoInitAddHardwareID failed with status %!STATUS!",
            status);
        goto freeAndExit;
    }

#pragma endregion

#pragma region Build InstanceID

    status = RtlUnicodeStringPrintf(
        &instanceId,
        L"%I64X",
        pDesc->ClientConnection->RemoteAddress
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "RtlUnicodeStringPrintf failed for instanceId with status %!STATUS!",
            status
        );
        goto freeAndExit;
    }

    status = WdfPdoInitAssignInstanceID(ChildInit, &instanceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "WdfPdoInitAssignInstanceID failed with status %!STATUS!",
            status);
        goto freeAndExit;
    }

#pragma endregion

#pragma region Add request context shared with PDOs

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        BTHPS3_FDO_PDO_REQUEST_CONTEXT
    );
    WdfDeviceInitSetRequestAttributes(
        ChildInit,
        &attributes
    );

#pragma endregion

#pragma region Child device creation

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        BTHPS3_PDO_DEVICE_CONTEXT
    );

    status = WdfDeviceCreate(
        &ChildInit,
        &attributes,
        &hChild
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "WdfDeviceCreate failed with status %!STATUS!",
            status);
        goto freeAndExit;
    }

#pragma endregion

#pragma region Fill device context

    //
    // This info is used to attach it to incoming requests 
    // later on so the bus driver knows for which remote
    // device the request was made for.
    // 

    pdoCtx = GetPdoDeviceContext(hChild);

    pdoCtx->ClientConnection = pDesc->ClientConnection;

#pragma endregion

#pragma region Default I/O Queue creation

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&defaultQueueCfg, WdfIoQueueDispatchParallel);
    defaultQueueCfg.EvtIoInternalDeviceControl = BthPS3_PDO_EvtWdfIoQueueIoInternalDeviceControl;

    status = WdfIoQueueCreate(
        hChild,
        &defaultQueueCfg,
        WDF_NO_OBJECT_ATTRIBUTES,
        &defaultQueue
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "WdfIoQueueCreate (Default) failed with status %!STATUS!",
            status);
        goto freeAndExit;
    }

#pragma endregion

    freeAndExit:

               RtlFreeUnicodeString(&guidString);

               TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Exit");

               return status;
}

BOOLEAN BthPS3_PDO_EvtChildListIdentificationDescriptionCompare(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER FirstIdentificationDescription,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER SecondIdentificationDescription)
{
    PPDO_IDENTIFICATION_DESCRIPTION lhs, rhs;

    UNREFERENCED_PARAMETER(DeviceList);

    lhs = CONTAINING_RECORD(FirstIdentificationDescription,
        PDO_IDENTIFICATION_DESCRIPTION,
        Header);
    rhs = CONTAINING_RECORD(SecondIdentificationDescription,
        PDO_IDENTIFICATION_DESCRIPTION,
        Header);

    return (lhs->ClientConnection->RemoteAddress ==
        rhs->ClientConnection->RemoteAddress) ? TRUE : FALSE;
}

//
// Handle IRP_MJ_INTERNAL_DEVICE_CONTROL sent to PDO
// 
void BthPS3_PDO_EvtWdfIoQueueIoInternalDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode
)
{
    WDFDEVICE device, parentDevice;
    WDF_REQUEST_FORWARD_OPTIONS forwardOptions;
    NTSTATUS status;
    PBTHPS3_FDO_PDO_REQUEST_CONTEXT reqCtx = NULL;
    PBTHPS3_PDO_DEVICE_CONTEXT pdoCtx = NULL;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(IoControlCode);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Entry");

    device = WdfIoQueueGetDevice(Queue);
    parentDevice = WdfPdoGetParent(device);
    reqCtx = GetFdoPdoRequestContext(Request);
    pdoCtx = GetPdoDeviceContext(device);

    //
    // Establish relationship of PDO to BTH_ADDR so the parent bus
    // can pick the related device from connection list.
    // 
    reqCtx->ClientConnection = pdoCtx->ClientConnection;

    WDF_REQUEST_FORWARD_OPTIONS_INIT(&forwardOptions);
    forwardOptions.Flags = WDF_REQUEST_FORWARD_OPTION_SEND_AND_FORGET;

    //
    // FDO has all the state info so don't bother handling this ourself
    // 
    status = WdfRequestForwardToParentDeviceIoQueue(
        Request,
        WdfDeviceGetDefaultQueue(parentDevice),
        &forwardOptions
    );
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Exit");
}