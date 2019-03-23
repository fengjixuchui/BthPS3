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
    WDF_DEVICE_PNP_CAPABILITIES         pnpCaps;

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
        status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_SIXAXIS,
            &guidString
        );
        break;
    case DS_DEVICE_TYPE_NAVIGATION:
        status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_NAVIGATION,
            &guidString
        );
        break;
    case DS_DEVICE_TYPE_MOTION:
        status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_MOTION,
            &guidString
        );
        break;
    case DS_DEVICE_TYPE_WIRELESS:
        status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_WIRELESS,
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

#pragma region Raw PDO properties

    //
    // Assign RAW PDO Device Class GUID depending on device type
    // 
    // In raw device mode, user-land applications can talk to our
    // PDO as well with no function driver attached.
    // 
    switch (pDesc->ClientConnection->DeviceType)
    {
    case DS_DEVICE_TYPE_SIXAXIS:
        status = WdfPdoInitAssignRawDevice(ChildInit,
            &GUID_DEVCLASS_BTHPS3_SIXAXIS
        );
        break;
    case DS_DEVICE_TYPE_NAVIGATION:
        status = WdfPdoInitAssignRawDevice(ChildInit,
            &GUID_DEVCLASS_BTHPS3_NAVIGATION
        );
        break;
    case DS_DEVICE_TYPE_MOTION:
        status = WdfPdoInitAssignRawDevice(ChildInit,
            &GUID_DEVCLASS_BTHPS3_MOTION
        );
        break;
    case DS_DEVICE_TYPE_WIRELESS:
        status = WdfPdoInitAssignRawDevice(ChildInit,
            &GUID_DEVCLASS_BTHPS3_WIRELESS
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
            "WdfPdoInitAssignRawDevice failed with status %!STATUS!",
            status
        );
        return status;
    }

    //
    // Let the world talk to us
    // 
    status = WdfDeviceInitAssignSDDLString(ChildInit,
        &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_BUSLOGIC,
            "WdfDeviceInitAssignSDDLString failed with status %!STATUS!",
            status
        );

        return status;
    }

#pragma endregion

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
        L"%012llX",
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

#pragma region Add FDO-shared request context

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
    attributes.EvtCleanupCallback = BthPS3_PDO_EvtDeviceContextCleanup;

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

#pragma region Expose device interface

    switch (pDesc->ClientConnection->DeviceType)
    {
    case DS_DEVICE_TYPE_SIXAXIS:
        status = WdfDeviceCreateDeviceInterface(hChild,
            &GUID_DEVINTERFACE_BTHPS3_SIXAXIS,
            NULL
        );
        break;
    case DS_DEVICE_TYPE_NAVIGATION:
        status = WdfDeviceCreateDeviceInterface(hChild,
            &GUID_DEVINTERFACE_BTHPS3_NAVIGATION,
            NULL
        );
        break;
    case DS_DEVICE_TYPE_MOTION:
        status = WdfDeviceCreateDeviceInterface(hChild,
            &GUID_DEVINTERFACE_BTHPS3_MOTION,
            NULL
        );
        break;
    case DS_DEVICE_TYPE_WIRELESS:
        status = WdfDeviceCreateDeviceInterface(hChild,
            &GUID_DEVINTERFACE_BTHPS3_WIRELESS,
            NULL
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
            "WdfDeviceCreateDeviceInterface failed with status %!STATUS!",
            status
        );
        return status;
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

    //
    // PDO relies on the connection object context so 
    // we increase the reference count to protect from
    // it getting freed to soon. See BthPS3_PDO_EvtDeviceContextCleanup
    // 
    WdfObjectReference(WdfObjectContextGetObject(pDesc->ClientConnection));

#pragma endregion

#pragma region PNP/Power Caps

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
    pnpCaps.Removable = WdfTrue;
    pnpCaps.SurpriseRemovalOK = WdfTrue;

    WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

#pragma endregion

#pragma region Default I/O Queue creation

    //
    // All of the heavy lifting is done by a function driver
    // which communicates via IRP_MJ_INTERNAL_DEVICE_CONTROL
    // 
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&defaultQueueCfg, WdfIoQueueDispatchParallel);

    defaultQueueCfg.EvtIoStop = BthPS3_EvtIoStop;
    defaultQueueCfg.EvtIoDeviceControl = BthPS3_PDO_EvtWdfIoQueueIoDeviceControl;

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

//
// Used to compare two bus children
// 
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
    //
    // BTH_ADDR of remote device is used to distinguish between children
    // 
    return (lhs->ClientConnection->RemoteAddress ==
        rhs->ClientConnection->RemoteAddress) ? TRUE : FALSE;
}

//
// Gets called on PDO removal
// 
VOID
BthPS3_PDO_EvtDeviceContextCleanup(
    IN WDFOBJECT Device
)
{
    PBTHPS3_PDO_DEVICE_CONTEXT devCtx = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Entry");

    devCtx = GetPdoDeviceContext(Device);

    //
    // At this point it's safe (for us, the PDO) to dispose the connection object
    // 
    WdfObjectDereference(WdfObjectContextGetObject(devCtx->ClientConnection));

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Exit");
}

//
// Handle IRP_MJ_DEVICE_CONTROL sent to PDO
// 
void BthPS3_PDO_EvtWdfIoQueueIoDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode
)
{
    NTSTATUS                    status = STATUS_UNSUCCESSFUL;
    WDFDEVICE                   child = NULL;
    WDFDEVICE                   parent = NULL;
    PBTHPS3_PDO_DEVICE_CONTEXT  childCtx = NULL;
    PBTHPS3_CLIENT_CONNECTION   clientConnection = NULL;
    PVOID                       buffer = NULL;
    size_t                      bufferLength = 0;
    WDF_REQUEST_FORWARD_OPTIONS forwardOptions;


    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Entry");

    child = WdfIoQueueGetDevice(Queue);
    parent = WdfPdoGetParent(child);
    childCtx = GetPdoDeviceContext(child);
    clientConnection = childCtx->ClientConnection;

    switch (IoControlCode)
    {
#pragma region IOCTL_BTHPS3_HID_CONTROL_READ

    case IOCTL_BTHPS3_HID_CONTROL_READ:

        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_BUSLOGIC,
            ">> IOCTL_BTHPS3_HID_CONTROL_READ"
        );

        status = WdfRequestRetrieveOutputBuffer(
            Request,
            OutputBufferLength,
            &buffer,
            &bufferLength
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "WdfRequestRetrieveOutputBuffer failed with status %!STATUS!",
                status
            );
            break;
        }

        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_BUSLOGIC,
            "bufferLength: %d",
            (ULONG)bufferLength
        );

        status = L2CAP_PS3_ReadControlTransferAsync(
            clientConnection,
            Request,
            buffer,
            bufferLength,
            L2CAP_PS3_AsyncReadControlTransferCompleted
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "L2CAP_PS3_ReadControlTransferAsync failed with status %!STATUS!",
                status
            );
        }
        else
        {
            status = STATUS_PENDING;
        }

        break;

#pragma endregion

#pragma region IOCTL_BTHPS3_HID_CONTROL_WRITE

    case IOCTL_BTHPS3_HID_CONTROL_WRITE:

        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_BUSLOGIC,
            ">> IOCTL_BTHPS3_HID_CONTROL_WRITE"
        );

        status = WdfRequestRetrieveInputBuffer(
            Request,
            InputBufferLength,
            &buffer,
            &bufferLength
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "WdfRequestRetrieveInputBuffer failed with status %!STATUS!",
                status
            );
            break;
        }

        status = L2CAP_PS3_SendControlTransferAsync(
            clientConnection,
            Request,
            buffer,
            bufferLength,
            L2CAP_PS3_AsyncSendControlTransferCompleted
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "L2CAP_PS3_SendControlTransferAsync failed with status %!STATUS!",
                status
            );
        }
        else
        {
            status = STATUS_PENDING;
        }

        break;

#pragma endregion

#pragma region IOCTL_BTHPS3_HID_INTERRUPT_READ

    case IOCTL_BTHPS3_HID_INTERRUPT_READ:

        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_BUSLOGIC,
            ">> IOCTL_BTHPS3_HID_INTERRUPT_READ"
        );

        status = WdfRequestRetrieveOutputBuffer(
            Request,
            OutputBufferLength,
            &buffer,
            &bufferLength
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "WdfRequestRetrieveOutputBuffer failed with status %!STATUS!",
                status
            );
            break;
        }

        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_BUSLOGIC,
            "bufferLength: %d",
            (ULONG)bufferLength
        );

        status = L2CAP_PS3_ReadInterruptTransferAsync(
            clientConnection,
            Request,
            buffer,
            bufferLength,
            L2CAP_PS3_AsyncReadInterruptTransferCompleted
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "L2CAP_PS3_ReadInterruptTransferAsync failed with status %!STATUS!",
                status
            );
        }
        else
        {
            status = STATUS_PENDING;
        }

        break;

#pragma endregion

#pragma region IOCTL_BTHPS3_HID_INTERRUPT_WRITE

    case IOCTL_BTHPS3_HID_INTERRUPT_WRITE:

        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_BUSLOGIC,
            ">> IOCTL_BTHPS3_HID_INTERRUPT_WRITE"
        );

        status = WdfRequestRetrieveInputBuffer(
            Request,
            InputBufferLength,
            &buffer,
            &bufferLength
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "WdfRequestRetrieveInputBuffer failed with status %!STATUS!",
                status
            );
            break;
        }

        status = L2CAP_PS3_SendInterruptTransferAsync(
            clientConnection,
            Request,
            buffer,
            bufferLength,
            L2CAP_PS3_AsyncSendInterruptTransferCompleted
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "L2CAP_PS3_SendInterruptTransferAsync failed with status %!STATUS!",
                status
            );
        }
        else
        {
            status = STATUS_PENDING;
        }

        break;

#pragma endregion

    default:
        TraceEvents(TRACE_LEVEL_WARNING,
            TRACE_BUSLOGIC,
            "Unknown IoControlCode received: 0x%X",
            IoControlCode
        );

        WDF_REQUEST_FORWARD_OPTIONS_INIT(&forwardOptions);
        forwardOptions.Flags = WDF_REQUEST_FORWARD_OPTION_SEND_AND_FORGET;

        status = WdfRequestForwardToParentDeviceIoQueue(
            Request,
            WdfDeviceGetDefaultQueue(parent),
            &forwardOptions
        );

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_BUSLOGIC,
                "WdfRequestForwardToParentDeviceIoQueue failed with status %!STATUS!",
                status
            );
            WdfRequestComplete(Request, status);
        }

        return;
    }

    if (status != STATUS_PENDING) {
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_BUSLOGIC, "%!FUNC! Exit (status: %!STATUS!)", status);
}
