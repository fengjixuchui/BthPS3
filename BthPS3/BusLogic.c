/**********************************************************************************
 *                                                                                *
 * BthPS3 - Windows kernel-mode Bluetooth profile and bus driver                  *
 *                                                                                *
 * BSD 3-Clause License                                                           *
 *                                                                                *
 * Copyright (c) 2018-2022, Nefarius Software Solutions e.U.                      *
 * All rights reserved.                                                           *
 *                                                                                *
 * Redistribution and use in source and binary forms, with or without             *
 * modification, are permitted provided that the following conditions are met:    *
 *                                                                                *
 * 1. Redistributions of source code must retain the above copyright notice, this *
 *    list of conditions and the following disclaimer.                            *
 *                                                                                *
 * 2. Redistributions in binary form must reproduce the above copyright notice,   *
 *    this list of conditions and the following disclaimer in the documentation   *
 *    and/or other materials provided with the distribution.                      *
 *                                                                                *
 * 3. Neither the name of the copyright holder nor the names of its               *
 *    contributors may be used to endorse or promote products derived from        *
 *    this software without specific prior written permission.                    *
 *                                                                                *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"    *
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE      *
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE *
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE   *
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL     *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR     *
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER     *
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,  *
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE  *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.           *
 *                                                                                *
 **********************************************************************************/


#include "Driver.h"
#include <limits.h>
#include "buslogic.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BthPS3_EvtWdfChildListCreateDevice)
#endif


 //
 // IOCTLs handled by each PDO
 // 
IoctlHandler_IoctlRecord G_PDO_IoctlSpecification[] =
{
	{IOCTL_BTHPS3_HID_CONTROL_READ, 0, 1, BthPS3_PDO_HandleHidControlRead},
	{IOCTL_BTHPS3_HID_CONTROL_WRITE, 1, 0, BthPS3_PDO_HandleHidControlWrite},
	{IOCTL_BTHPS3_HID_INTERRUPT_READ, 0, 1, BthPS3_PDO_HandleHidInterruptRead},
	{IOCTL_BTHPS3_HID_INTERRUPT_WRITE, 1, 0, BthPS3_PDO_HandleHidInterruptWrite},
	{IOCTL_HID_GET_DEVICE_DESCRIPTOR, 0, 0, BthPS3_PDO_HandleOther},
};


#pragma region REMOVE
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
	NTSTATUS                                status = STATUS_UNSUCCESSFUL;
	PPDO_IDENTIFICATION_DESCRIPTION         pDesc;
	UNICODE_STRING                          guidString;
	WDFDEVICE                               hChild = NULL;
	WDF_OBJECT_ATTRIBUTES                   attributes;
	PBTHPS3_PDO_DEVICE_CONTEXT              pdoCtx = NULL;
	WDF_DEVICE_PNP_CAPABILITIES             pnpCaps;
	WDF_DEVICE_POWER_CAPABILITIES			powerCaps;
	WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS   idleSettings;
	WDF_PNPPOWER_EVENT_CALLBACKS            pnpPowerCallbacks;
	WDFKEY                                  hKey = NULL;
	ULONG                                   rawPdo = 0;
	ULONG                                   hidePdo = 0;
	ULONG                                   adminOnlyPdo = 0;
	ULONG                                   exclusivePdo = 1;
	ULONG                                   idleTimeout = 10000; // 10 secs idle timeout

	DECLARE_UNICODE_STRING_SIZE(deviceId, MAX_DEVICE_ID_LEN);
	DECLARE_UNICODE_STRING_SIZE(hardwareId, MAX_DEVICE_ID_LEN);
	DECLARE_UNICODE_STRING_SIZE(instanceId, sizeof(INT32));

	DECLARE_CONST_UNICODE_STRING(rawPdoValue, BTHPS3_REG_VALUE_RAW_PDO);
	DECLARE_CONST_UNICODE_STRING(hidePdoValue, BTHPS3_REG_VALUE_HIDE_PDO);
	DECLARE_CONST_UNICODE_STRING(adminOnlyPdoValue, BTHPS3_REG_VALUE_ADMIN_ONLY_PDO);
	DECLARE_CONST_UNICODE_STRING(exclusivePdoValue, BTHPS3_REG_VALUE_EXCLUSIVE_PDO);
	DECLARE_CONST_UNICODE_STRING(idleTimeoutValue, BTHPS3_REG_VALUE_CHILD_IDLE_TIMEOUT);

	UNREFERENCED_PARAMETER(ChildList);

	PAGED_CODE();


	FuncEntry(TRACE_BUSLOGIC);

	pDesc = CONTAINING_RECORD(
		IdentificationDescription,
		PDO_IDENTIFICATION_DESCRIPTION,
		Header);

	//
	// Open
	//   HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\BthPS3\Parameters
	// key
	// 
	status = WdfDriverOpenParametersRegistryKey(
		WdfGetDriver(),
		STANDARD_RIGHTS_ALL,
		WDF_NO_OBJECT_ATTRIBUTES,
		&hKey
	);

	//
	// On success, read configuration values
	// 
	if (NT_SUCCESS(status))
	{
		//
		// Don't care, if it fails, keep default value
		// 
		(void)WdfRegistryQueryULong(
			hKey,
			&rawPdoValue,
			&rawPdo
		);

		//
		// Don't care, if it fails, keep default value
		// 
		(void)WdfRegistryQueryULong(
			hKey,
			&hidePdoValue,
			&hidePdo
		);

		//
		// Don't care, if it fails, keep default value
		// 
		(void)WdfRegistryQueryULong(
			hKey,
			&adminOnlyPdoValue,
			&adminOnlyPdo
		);

		//
		// Don't care, if it fails, keep default value
		// 
		(void)WdfRegistryQueryULong(
			hKey,
			&exclusivePdoValue,
			&exclusivePdo
		);

		//
		// Don't care, if it fails, keep default value
		// 
		(void)WdfRegistryQueryULong(
			hKey,
			&idleTimeoutValue,
			&idleTimeout
		);

		WdfRegistryClose(hKey);
	}

	//
	// PDO features
	// 
	WdfDeviceInitSetDeviceType(ChildInit, FILE_DEVICE_BUS_EXTENDER);

	//
	// Only one instance (either function driver or user-land application)
	// may talk to this PDO at the same time to avoid splitting traffic.
	// 
	WdfDeviceInitSetExclusive(ChildInit, (BOOLEAN)exclusivePdo);

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
		TraceError(
			TRACE_BUSLOGIC,
			"RtlStringFromGUID failed with status %!STATUS!",
			status
		);
		return status;
	}

	do {

#pragma region Raw PDO properties

		if (rawPdo)
		{
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
				break;
			}

			if (!NT_SUCCESS(status))
			{
				TraceError(
					TRACE_BUSLOGIC,
					"WdfPdoInitAssignRawDevice failed with status %!STATUS!",
					status
				);
				break;
			}

			//
			// Let the world talk to us
			// 
			status = WdfDeviceInitAssignSDDLString(ChildInit,
				(adminOnlyPdo) ? &SDDL_DEVOBJ_SYS_ALL_ADM_ALL : // only elevated allowed
				&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX  // everyone is allowed
			);

			if (!NT_SUCCESS(status))
			{
				TraceError(
					TRACE_BUSLOGIC,
					"WdfDeviceInitAssignSDDLString failed with status %!STATUS!",
					status
				);
				break;
			}
		}

#pragma endregion

#pragma region Build DeviceID

		//
		// Adjust properties depending on device type
		// 
		switch (pDesc->ClientConnection->DeviceType)
		{
		case DS_DEVICE_TYPE_SIXAXIS:
			status = RtlUnicodeStringPrintf(
				&deviceId,
				L"%ws\\%wZ&DEV&VID_%04X&PID_%04X&%012llX",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_SIXAXIS_VID,
				BTHPS3_SIXAXIS_PID,
				pDesc->ClientConnection->RemoteAddress
			);
			break;
		case DS_DEVICE_TYPE_NAVIGATION:
			status = RtlUnicodeStringPrintf(
				&deviceId,
				L"%ws\\%wZ&DEV&VID_%04X&PID_%04X&%012llX",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_NAVIGATION_VID,
				BTHPS3_NAVIGATION_PID,
				pDesc->ClientConnection->RemoteAddress
			);
			break;
		case DS_DEVICE_TYPE_MOTION:
			status = RtlUnicodeStringPrintf(
				&deviceId,
				L"%ws\\%wZ&DEV&VID_%04X&PID_%04X&%012llX",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_MOTION_VID,
				BTHPS3_MOTION_PID,
				pDesc->ClientConnection->RemoteAddress
			);
			break;
		case DS_DEVICE_TYPE_WIRELESS:
			status = RtlUnicodeStringPrintf(
				&deviceId,
				L"%ws\\%wZ&DEV&VID_%04X&PID_%04X&%012llX",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_WIRELESS_VID,
				BTHPS3_WIRELESS_PID,
				pDesc->ClientConnection->RemoteAddress
			);
			break;
		default:
			// Doesn't happen
			return status;
		}

		if (!NT_SUCCESS(status)) {
			TraceError(
				TRACE_BUSLOGIC,
				"RtlUnicodeStringPrintf failed for deviceId with status %!STATUS!",
				status
			);
			break;
		}

		status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
		if (!NT_SUCCESS(status)) {
			TraceError(
				TRACE_BUSLOGIC,
				"WdfPdoInitAssignDeviceID failed with status %!STATUS!",
				status);
			break;
		}

#pragma endregion

#pragma region Build HardwareID

		//
		// Adjust properties depending on device type
		// 
		switch (pDesc->ClientConnection->DeviceType)
		{
		case DS_DEVICE_TYPE_SIXAXIS:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_SIXAXIS_VID,
				BTHPS3_SIXAXIS_PID
			);
			break;
		case DS_DEVICE_TYPE_NAVIGATION:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_NAVIGATION_VID,
				BTHPS3_NAVIGATION_PID
			);
			break;
		case DS_DEVICE_TYPE_MOTION:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_MOTION_VID,
				BTHPS3_MOTION_PID
			);
			break;
		case DS_DEVICE_TYPE_WIRELESS:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_WIRELESS_VID,
				BTHPS3_WIRELESS_PID
			);
			break;
		default:
			// Doesn't happen
			return status;
		}

		if (!NT_SUCCESS(status)) {
			TraceError(
				TRACE_BUSLOGIC,
				"RtlUnicodeStringPrintf failed for hardwareId with status %!STATUS!",
				status
			);
			break;
		}

		status = WdfPdoInitAddHardwareID(ChildInit, &hardwareId);
		if (!NT_SUCCESS(status)) {
			TraceError(
				TRACE_BUSLOGIC,
				"WdfPdoInitAddHardwareID failed with status %!STATUS!",
				status);
			break;
		}

#pragma endregion

#pragma region Build InstanceID

		status = RtlIntegerToUnicodeString(
			0,
			16,
			&instanceId
		);
		if (!NT_SUCCESS(status))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"RtlIntegerToUnicodeString failed for instanceId with status %!STATUS!",
				status
			);
			break;
		}

		status = WdfPdoInitAssignInstanceID(ChildInit, &instanceId);
		if (!NT_SUCCESS(status)) {
			TraceError(
				TRACE_BUSLOGIC,
				"WdfPdoInitAssignInstanceID failed with status %!STATUS!",
				status);
			break;
		}

#pragma endregion

#pragma region PNP/Power Callbacks

		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
		pnpPowerCallbacks.EvtDeviceD0Exit = BthPS3_PDO_EvtWdfDeviceD0Exit;
		pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = BthPS3_PDO_EvtWdfDeviceSelfManagedIoInit;

		WdfDeviceInitSetPnpPowerEventCallbacks(ChildInit, &pnpPowerCallbacks);

#pragma endregion 

#pragma region Child device creation

		if (!rawPdo)
		{
			WdfDeviceInitSetPowerPolicyOwnership(ChildInit, TRUE);
		}

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
			TraceError(
				TRACE_BUSLOGIC,
				"WdfDeviceCreate failed with status %!STATUS!",
				status);
			break;
		}

#pragma endregion

#pragma region Expose device interface

		if (rawPdo)
		{
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
				break;
			}

			if (!NT_SUCCESS(status))
			{
				TraceError(
					TRACE_BUSLOGIC,
					"WdfDeviceCreateDeviceInterface failed with status %!STATUS!",
					status
				);
				break;
			}
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
		// it getting freed too soon. See BthPS3_PDO_EvtDeviceContextCleanup
		// 
		WdfObjectReference(WdfObjectContextGetObject(pDesc->ClientConnection));

#pragma endregion

#pragma region PNP/Power Caps

		//
		// PNP Capabilities
		// 

		WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
		pnpCaps.Removable = WdfTrue;
		pnpCaps.SurpriseRemovalOK = WdfTrue;
		pnpCaps.NoDisplayInUI = (hidePdo) ? WdfTrue : WdfFalse;

		WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

		//
		// Power capabilities
		// 

		WDF_DEVICE_POWER_CAPABILITIES_INIT(&powerCaps);
		powerCaps.DeviceD1 = WdfFalse;
		powerCaps.DeviceD2 = WdfTrue;
		powerCaps.WakeFromD2 = WdfTrue;
		powerCaps.DeviceState[PowerSystemWorking] = PowerDeviceD0;
		powerCaps.DeviceState[PowerSystemSleeping1] = PowerDeviceD2;
		powerCaps.DeviceState[PowerSystemSleeping2] = PowerDeviceD2;
		powerCaps.DeviceState[PowerSystemSleeping3] = PowerDeviceD2;
		powerCaps.DeviceState[PowerSystemHibernate] = PowerDeviceD2;
		powerCaps.DeviceState[PowerSystemShutdown] = PowerDeviceD3;

		WdfDeviceSetPowerCapabilities(hChild, &powerCaps);

		//
		// Idle settings
		// 

		WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleCannotWakeFromS0);
		idleSettings.IdleTimeout = idleTimeout;
		status = WdfDeviceAssignS0IdleSettings(hChild, &idleSettings);

		//
		// Catch special case more precisely 
		// 
		if (status == STATUS_INVALID_DEVICE_REQUEST)
		{
			TraceError(
				TRACE_BUSLOGIC,
				"No function driver attached and not in RAW mode, can't continue"
			);
			break;
		}

		if (!NT_SUCCESS(status))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfDeviceAssignS0IdleSettings failed with status %!STATUS!",
				status
			);
			break;
		}

#pragma endregion

	} while (FALSE);

	RtlFreeUnicodeString(&guidString);

	FuncExit(TRACE_BUSLOGIC, "status=%!STATUS!", status);

	return status;
}
#pragma endregion

//
// Triggered on entering I/O idle state
// 
NTSTATUS BthPS3_PDO_EvtWdfDeviceD0Exit(
	WDFDEVICE Device,
	WDF_POWER_DEVICE_STATE TargetState
)
{
	NTSTATUS                    status;
	WDFDEVICE                   parentDevice;
	WDFIOTARGET                 parentTarget;
	PBTHPS3_PDO_DEVICE_CONTEXT  pPdoDevCtx;
	WDFREQUEST					dcRequest;
	WDF_OBJECT_ATTRIBUTES		attributes;
	WDFMEMORY					payload;


	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(TargetState);

	FuncEntry(TRACE_BUSLOGIC);

	pPdoDevCtx = GetPdoDeviceContext(Device);
	parentDevice = WdfPdoGetParent(Device);
	parentTarget = WdfDeviceGetIoTarget(parentDevice);

	TraceInformation(
		TRACE_BUSLOGIC,
		"Requesting device disconnect"
	);

	/*
	 * Low power conditions are not supported by the remote device,
	 * if low power or idle was requested, instruct radio to disconnect.
	 */

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = Device;

	status = WdfRequestCreate(
		&attributes,
		parentTarget,
		&dcRequest
	);
	if (!NT_SUCCESS(status))
	{
		TraceError(
			TRACE_BUSLOGIC,
			"WdfRequestCreate failed with status %!STATUS!",
			status
		);
	}

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = dcRequest;

	status = WdfMemoryCreatePreallocated(
		&attributes,
		&pPdoDevCtx->ClientConnection->RemoteAddress,
		sizeof(BTH_ADDR),
		&payload
	);
	if (!NT_SUCCESS(status))
	{
		TraceError(
			TRACE_BUSLOGIC,
			"WdfMemoryCreatePreallocated failed with status %!STATUS!",
			status
		);
	}

	status = WdfIoTargetFormatRequestForIoctl(
		parentTarget,
		dcRequest,
		IOCTL_BTH_DISCONNECT_DEVICE,
		payload,
		NULL,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status))
	{
		TraceError(
			TRACE_BUSLOGIC,
			"WdfIoTargetFormatRequestForIoctl failed with status %!STATUS!",
			status
		);
	}

	WdfRequestSetCompletionRoutine(
		dcRequest,
		BthPS3_PDO_DisconnectRequestCompleted,
		NULL
	);

	//
	// Request parent to abandon us :'(
	// 
	if (WdfRequestSend(
		dcRequest,
		parentTarget,
		WDF_NO_SEND_OPTIONS
	) == FALSE)
	{
		status = WdfRequestGetStatus(dcRequest);

		if (status != STATUS_DEVICE_NOT_CONNECTED && !NT_SUCCESS(status))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfRequestSend failed with status %!STATUS!",
				status
			);
		}
		else
		{
			//
			// Override
			// 
			status = STATUS_SUCCESS;
		}
	}

	FuncExit(TRACE_BUSLOGIC, "status=%!STATUS!", status);

	return status;
}

//
// Gets called on PDO removal
// 
VOID
BthPS3_PDO_EvtDeviceContextCleanup(
	IN WDFOBJECT Device
)
{
	PBTHPS3_PDO_DEVICE_CONTEXT pDevCtx;

	FuncEntry(TRACE_BUSLOGIC);

	pDevCtx = GetPdoDeviceContext(Device);

	//
	// At this point it's safe (for us, the PDO) to dispose the connection object
	// 
	WdfObjectDereference(WdfObjectContextGetObject(pDevCtx->ClientConnection));

	FuncExitNoReturn(TRACE_BUSLOGIC);
}

//
// Disconnect request got completed
// 
void BthPS3_PDO_DisconnectRequestCompleted(
	_In_ WDFREQUEST Request,
	_In_ WDFIOTARGET Target,
	_In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
	_In_ WDFCONTEXT Context
)
{
	UNREFERENCED_PARAMETER(Target);
	UNREFERENCED_PARAMETER(Context);

	FuncEntryArguments(
		TRACE_BUSLOGIC,
		"status=%!STATUS!",
		Params->IoStatus.Status
	);

	WdfObjectDelete(Request);

	FuncExitNoReturn(TRACE_BUSLOGIC);
}

//
// Set device properties
// 
NTSTATUS BthPS3_PDO_EvtWdfDeviceSelfManagedIoInit(
	WDFDEVICE Device
)
{
	PBTHPS3_PDO_DEVICE_CONTEXT pCtx = GetPdoDeviceContext(Device);
	WCHAR deviceAddress[13];
	PWSTR manufacturer = L"Nefarius Software Solutions e.U.";
	LARGE_INTEGER lastConnectionTime;

	FuncEntry(TRACE_BUSLOGIC);

	KeQuerySystemTimePrecise(&lastConnectionTime);

	//
	// Common
	// 

	if (NT_SUCCESS(RtlStringCbPrintfW(
		deviceAddress,
		ARRAYSIZE(deviceAddress) * sizeof(WCHAR),
		L"%012llX",
		pCtx->ClientConnection->RemoteAddress
	)))
	{
		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DeviceAddress,
			DEVPROP_TYPE_STRING,
			ARRAYSIZE(deviceAddress) * sizeof(WCHAR),
			deviceAddress
		);
	}

	(void)BthPS3_AssignDeviceProperty(
		Device,
		&DEVPKEY_BluetoothRadio_Address,
		DEVPROP_TYPE_UINT64,
		sizeof(UINT64),
		&pCtx->ClientConnection->DevCtxHdr->LocalBthAddr
	);

	(void)BthPS3_AssignDeviceProperty(
		Device,
		&DEVPKEY_Device_FriendlyName,
		DEVPROP_TYPE_STRING,
		pCtx->ClientConnection->RemoteName.Length + sizeof(L'\0'),
		pCtx->ClientConnection->RemoteName.Buffer
	);

	(void)BthPS3_AssignDeviceProperty(
		Device,
		&DEVPKEY_Bluetooth_DeviceManufacturer,
		DEVPROP_TYPE_STRING,
		sizeof(manufacturer),
		manufacturer
	);

	(void)BthPS3_AssignDeviceProperty(
		Device,
		&DEVPKEY_Bluetooth_LastConnectedTime,
		DEVPROP_TYPE_FILETIME,
		sizeof(LARGE_INTEGER),
		&lastConnectionTime
	);

	switch (pCtx->ClientConnection->DeviceType)
	{
	case DS_DEVICE_TYPE_SIXAXIS:

		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DeviceVID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PUSHORT)&BTHPS3_SIXAXIS_VID
		);
		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DevicePID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PVOID)&BTHPS3_SIXAXIS_PID
		);

		break;
	case DS_DEVICE_TYPE_NAVIGATION:

		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DeviceVID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PUSHORT)&BTHPS3_NAVIGATION_VID
		);
		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DevicePID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PVOID)&BTHPS3_NAVIGATION_PID
		);

		break;
	case DS_DEVICE_TYPE_MOTION:

		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DeviceVID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PUSHORT)&BTHPS3_MOTION_VID
		);
		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DevicePID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PVOID)&BTHPS3_MOTION_PID
		);

		break;
	case DS_DEVICE_TYPE_WIRELESS:

		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DeviceVID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PUSHORT)&BTHPS3_WIRELESS_VID
		);
		(void)BthPS3_AssignDeviceProperty(
			Device,
			&DEVPKEY_Bluetooth_DevicePID,
			DEVPROP_TYPE_UINT16,
			sizeof(USHORT),
			(PVOID)&BTHPS3_WIRELESS_PID
		);

		break;
	default:
		// Doesn't happen
		break;
	}

	FuncExitNoReturn(TRACE_BUSLOGIC);

	return STATUS_SUCCESS;
}

NTSTATUS BthPS3_AssignDeviceProperty(
	WDFDEVICE Device,
	const DEVPROPKEY* PropertyKey,
	DEVPROPTYPE Type,
	ULONG Size,
	PVOID Data
)
{
#if KMDF_VERSION_MAJOR == 1 && KMDF_VERSION_MINOR >= 13

	WDF_DEVICE_PROPERTY_DATA propertyData;

	WDF_DEVICE_PROPERTY_DATA_INIT(&propertyData, PropertyKey);
	propertyData.Flags |= PLUGPLAY_PROPERTY_PERSISTENT;
	propertyData.Lcid = LOCALE_NEUTRAL;

	return WdfDeviceAssignProperty(
		Device,
		&propertyData,
		Type,
		Size,
		Data
	);

#else

	//
	// KMDF version too low to support this
	// 

	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(PropertyKey);
	UNREFERENCED_PARAMETER(Type);
	UNREFERENCED_PARAMETER(Size);
	UNREFERENCED_PARAMETER(Data);

	TraceVerbose(
		TRACE_BUSLOGIC,
		"Assigning device properties requires KMDF 1.13 (Windows 8.x) or later"
	);

	return STATUS_NOT_SUPPORTED;

#endif
}


//
// The new stuff
// 


//
// Called when initializing DMF modules for the PDO
// 
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
BthPS3_PDO_EvtDmfModulesAdd(
	_In_ WDFDEVICE Device,
	_In_ PDMFMODULE_INIT DmfModuleInit
)
{
	FuncEntry(TRACE_BUSLOGIC);

	DMF_MODULE_ATTRIBUTES moduleAttributes;
	DMF_CONFIG_IoctlHandler moduleConfigIoctlHandler;

	const PBTHPS3_PDO_CONTEXT pPdoCtx = GetPdoContext(Device);

	//
	// IOCTL Handler Module
	// 

	DMF_CONFIG_IoctlHandler_AND_ATTRIBUTES_INIT(
		&moduleConfigIoctlHandler,
		&moduleAttributes
	);

	moduleConfigIoctlHandler.DeviceInterfaceGuid = GUID_DEVINTERFACE_BTHPS3;
	moduleConfigIoctlHandler.AccessModeFilter = IoctlHandler_AccessModeDefault;
	moduleConfigIoctlHandler.EvtIoctlHandlerAccessModeFilter = NULL;
	moduleConfigIoctlHandler.IoctlRecordCount = ARRAYSIZE(G_PDO_IoctlSpecification);
	moduleConfigIoctlHandler.IoctlRecords = G_PDO_IoctlSpecification;
	moduleConfigIoctlHandler.ForwardUnhandledRequests = FALSE;
	moduleConfigIoctlHandler.ManualMode = TRUE;

	DMF_DmfModuleAdd(
		DmfModuleInit,
		&moduleAttributes,
		WDF_NO_OBJECT_ATTRIBUTES,
		&pPdoCtx->DmfModuleIoctlHandler
	);

	FuncExitNoReturn(TRACE_BUSLOGIC);
}

//
// Creates a new PDO and connection context for a given remote address
// 
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
BthPS3_PDO_Create(
	_In_ PBTHPS3_SERVER_CONTEXT Context,
	_In_ BTH_ADDR RemoteAddress,
	_In_ DS_DEVICE_TYPE DeviceType,
	_In_ PSTR RemoteName,
	_In_ PFN_WDF_OBJECT_CONTEXT_CLEANUP CleanupCallback,
	_Out_ PBTHPS3_PDO_CONTEXT* PdoContext
)
{
	FuncEntry(TRACE_BUSLOGIC);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES attributes;
	PDO_RECORD record;
	WDFDEVICE device;
	UNICODE_STRING guidString = { 0 };
	WCHAR devAddr[13];
	PWSTR manufacturer = L"Nefarius Software Solutions e.U.";
	LARGE_INTEGER lastConnectionTime;
	WDFKEY hKey = NULL;
	ULONG rawPdo = 0;

	DECLARE_UNICODE_STRING_SIZE(hardwareId, MAX_DEVICE_ID_LEN);
	DECLARE_UNICODE_STRING_SIZE(remotenameWide, BTH_MAX_NAME_SIZE);
	DECLARE_CONST_UNICODE_STRING(rawPdoValue, BTHPS3_REG_VALUE_RAW_PDO);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, BTHPS3_PDO_CONTEXT);

	attributes.EvtCleanupCallback = CleanupCallback;

	KeQuerySystemTimePrecise(&lastConnectionTime);

	RtlZeroMemory(&record, sizeof(PDO_RECORD));

	record.CustomClientContext = &attributes;
	record.EnableDmf = TRUE;
	record.EvtDmfDeviceModulesAdd = BthPS3_PDO_EvtDmfModulesAdd;


	//
	// Open
	//   HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\BthPS3\Parameters
	// key
	// 
	status = WdfDriverOpenParametersRegistryKey(
		WdfGetDriver(),
		STANDARD_RIGHTS_ALL,
		WDF_NO_OBJECT_ATTRIBUTES,
		&hKey
	);

	//
	// On success, read configuration values
	// 
	if (NT_SUCCESS(status))
	{
		//
		// Don't care, if it fails, keep default value
		// 
		(void)WdfRegistryQueryULong(
			hKey,
			&rawPdoValue,
			&rawPdo
		);
	}

	do
	{
		WdfSpinLockAcquire(Context->Header.SlotsSpinLock);

		//
		// Get next free serial number
		// 
		for (record.SerialNumber = 1; record.SerialNumber <= BTHPS3_MAX_NUM_DEVICES; record.SerialNumber++)
		{
			if (!TestBit(Context->Header.Slots, record.SerialNumber))
			{
				TraceVerbose(
					TRACE_BUSLOGIC,
					"Assigned serial: %d",
					record.SerialNumber
				);

				SetBit(Context->Header.Slots, record.SerialNumber);
				break;
			}
		}

		if (record.SerialNumber > BTHPS3_MAX_NUM_DEVICES)
		{
			status = STATUS_NO_MORE_ENTRIES;
		}

		WdfSpinLockRelease(Context->Header.SlotsSpinLock);

		if (!NT_SUCCESS(status))
		{
			break;
		}

		//
		// Convert remote name from narrow to wide
		// 
		if (!NT_SUCCESS(status = RtlUnicodeStringPrintf(
			&remotenameWide,
			L"%hs",
			RemoteName
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"RtlUnicodeStringPrintf failed with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Prepare properties
		// 

		if (!NT_SUCCESS(RtlStringCbPrintfW(
			devAddr,
			ARRAYSIZE(devAddr) * sizeof(WCHAR),
			L"%012llX",
			RemoteAddress
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"RtlStringCbPrintfW failed with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Set these device properties for the new PDO
		// 
		Pdo_DevicePropertyEntry entries[] =
		{
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_Bluetooth_DeviceAddress, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_STRING,
				devAddr,
				ARRAYSIZE(devAddr) * sizeof(WCHAR),
				FALSE,
				NULL
			},
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_BluetoothRadio_Address, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_UINT64,
				&Context->Header.LocalBthAddr,
				sizeof(UINT64),
				FALSE,
				NULL
			},
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_Device_FriendlyName, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_STRING,
				remotenameWide.Buffer,
				remotenameWide.Length + sizeof(L'\0'),
				FALSE,
				NULL
			},
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_Bluetooth_DeviceManufacturer, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_STRING,
				manufacturer,
				(ULONG)(wcslen(manufacturer) * sizeof(WCHAR)) + sizeof(L'\0'),
				FALSE,
				NULL
			},
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_Bluetooth_LastConnectedTime, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_FILETIME,
				&lastConnectionTime,
				sizeof(LARGE_INTEGER),
				FALSE,
				NULL
			},
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_Bluetooth_DeviceVID, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_UINT16,
				NULL,
				sizeof(USHORT),
				FALSE,
				NULL
			},
			{
				{sizeof(WDF_DEVICE_PROPERTY_DATA), &DEVPKEY_Bluetooth_DevicePID, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT},
				DEVPROP_TYPE_UINT16,
				NULL,
				sizeof(USHORT),
				FALSE,
				NULL
			},
		};

		//
		// Set VID/PID according to device type
		// 
		switch (DeviceType)
		{
		case DS_DEVICE_TYPE_SIXAXIS:
			entries[5].ValueData = &BTHPS3_SIXAXIS_VID;
			entries[6].ValueData = &BTHPS3_SIXAXIS_PID;
			break;
		case DS_DEVICE_TYPE_NAVIGATION:
			entries[5].ValueData = &BTHPS3_NAVIGATION_VID;
			entries[6].ValueData = &BTHPS3_NAVIGATION_PID;
			break;
		case DS_DEVICE_TYPE_MOTION:
			entries[5].ValueData = &BTHPS3_MOTION_VID;
			entries[6].ValueData = &BTHPS3_MOTION_PID;
			break;
		case DS_DEVICE_TYPE_WIRELESS:
			entries[5].ValueData = &BTHPS3_WIRELESS_VID;
			entries[6].ValueData = &BTHPS3_WIRELESS_PID;
			break;
		default:
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		Pdo_DeviceProperty_Table properties;

		properties.ItemCount = ARRAYSIZE(entries);
		properties.TableEntries = entries;

		record.DeviceProperties = &properties;

		//
		// Prepare Hardware ID GUID segment and description based on device type
		// 
		switch (DeviceType)
		{
		case DS_DEVICE_TYPE_SIXAXIS:
			record.Description = L"PLAYSTATION(R)3 Controller";
			status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_SIXAXIS,
				&guidString
			);
			break;
		case DS_DEVICE_TYPE_NAVIGATION:
			record.Description = L"Navigation Controller";
			status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_NAVIGATION,
				&guidString
			);
			break;
		case DS_DEVICE_TYPE_MOTION:
			record.Description = L"Motion Controller";
			status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_MOTION,
				&guidString
			);
			break;
		case DS_DEVICE_TYPE_WIRELESS:
			record.Description = L"Wireless Controller";
			status = RtlStringFromGUID(&GUID_BUSENUM_BTHPS3_WIRELESS,
				&guidString
			);
			break;
		default:
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		if (!NT_SUCCESS(status))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"RtlStringFromGUID failed with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Build Hardware ID depending on device type
		// 
		switch (DeviceType)
		{
		case DS_DEVICE_TYPE_SIXAXIS:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_SIXAXIS_VID,
				BTHPS3_SIXAXIS_PID
			);
			break;
		case DS_DEVICE_TYPE_NAVIGATION:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_NAVIGATION_VID,
				BTHPS3_NAVIGATION_PID
			);
			break;
		case DS_DEVICE_TYPE_MOTION:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_MOTION_VID,
				BTHPS3_MOTION_PID
			);
			break;
		case DS_DEVICE_TYPE_WIRELESS:
			status = RtlUnicodeStringPrintf(
				&hardwareId,
				L"%ws\\%wZ&Dev&VID_%04X&PID_%04X",
				BthPS3BusEnumeratorName,
				guidString,
				BTHPS3_WIRELESS_VID,
				BTHPS3_WIRELESS_PID
			);
			break;
		default:
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		if (!NT_SUCCESS(status)) {
			TraceError(
				TRACE_BUSLOGIC,
				"RtlUnicodeStringPrintf failed for hardwareId with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Set Hardware ID previously built
		// 
		record.HardwareIds[0] = hardwareId.Buffer;
		record.HardwareIdsCount = 1;

		//
		// Expose as RAW device if told
		// 
		if (rawPdo)
		{
			record.RawDevice = TRUE;

			switch (DeviceType)
			{
			case DS_DEVICE_TYPE_SIXAXIS:
				record.RawDeviceClassGuid = &GUID_DEVCLASS_BTHPS3_SIXAXIS;
				break;
			case DS_DEVICE_TYPE_NAVIGATION:
				record.RawDeviceClassGuid = &GUID_DEVCLASS_BTHPS3_NAVIGATION;
				break;
			case DS_DEVICE_TYPE_MOTION:
				record.RawDeviceClassGuid = &GUID_DEVCLASS_BTHPS3_MOTION;
				break;
			case DS_DEVICE_TYPE_WIRELESS:
				record.RawDeviceClassGuid = &GUID_DEVCLASS_BTHPS3_WIRELESS;
				break;
			default:
				status = STATUS_INVALID_PARAMETER;
				break;
			}
		}

		//
		// Create PDO, DMF modules and allocate PDO context
		// 
		if (!NT_SUCCESS(status = DMF_Pdo_DevicePlugEx(
			Context->Header.DmfModulePdo,
			&record,
			&device
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"DMF_Pdo_DevicePlugEx failed with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Insert PDO in connection collection
		// 
		if (!NT_SUCCESS(status = WdfCollectionAdd(
			Context->Header.Clients,
			device
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfCollectionAdd for PDO device object failed with status %!STATUS!",
				status
			);
			break;
		}

		const PBTHPS3_PDO_CONTEXT pPdoCtx = *PdoContext = GetPdoContext(device);

		pPdoCtx->RemoteAddress = RemoteAddress;
		pPdoCtx->DevCtxHdr = &Context->Header;
		pPdoCtx->DeviceType = DeviceType;
		pPdoCtx->SerialNumber = record.SerialNumber;

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		attributes.ParentObject = device;

		PUCHAR pBuffer = NULL;
		const size_t hwIdByteCount = (wcslen(hardwareId.Buffer) * sizeof(WCHAR)) + sizeof(L'\0');

		//
		// Save Hardware ID for later unplug
		// 
		if (!NT_SUCCESS(status = WdfMemoryCreate(
			&attributes,
			NonPagedPoolNx,
			POOLTAG_BTHPS3,
			hwIdByteCount,
			&pPdoCtx->HardwareId,
			(PVOID*)&pBuffer
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfMemoryCreate failed with status %!STATUS!",
				status
			);
			break;
		}

		RtlCopyMemory(pBuffer, hardwareId.Buffer, hwIdByteCount);

		//
		// Initialize HidControlChannel properties
		// 

		if (!NT_SUCCESS(status = WdfRequestCreate(
			&attributes,
			pPdoCtx->DevCtxHdr->IoTarget,
			&pPdoCtx->HidControlChannel.ConnectDisconnectRequest
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfRequestCreate for HidControlChannel failed with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Initialize signaled, will be cleared once a connection is established
		// 
		KeInitializeEvent(&pPdoCtx->HidControlChannel.DisconnectEvent,
			NotificationEvent,
			TRUE
		);

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		attributes.ParentObject = device;

		if (!NT_SUCCESS(status = WdfSpinLockCreate(
			&attributes,
			&pPdoCtx->HidControlChannel.ConnectionStateLock
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfSpinLockCreate for HidControlChannel failed with status %!STATUS!",
				status
			);
			break;
		}

		pPdoCtx->HidControlChannel.ConnectionState = ConnectionStateInitialized;

		//
		// Initialize HidInterruptChannel properties
		// 

		if (!NT_SUCCESS(status = WdfRequestCreate(
			&attributes,
			pPdoCtx->DevCtxHdr->IoTarget,
			&pPdoCtx->HidInterruptChannel.ConnectDisconnectRequest
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfRequestCreate for HidInterruptChannel failed with status %!STATUS!",
				status
			);
			break;
		}

		//
		// Initialize signaled, will be cleared once a connection is established
		// 
		KeInitializeEvent(&pPdoCtx->HidInterruptChannel.DisconnectEvent,
			NotificationEvent,
			TRUE
		);

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		attributes.ParentObject = device;

		if (!NT_SUCCESS(status = WdfSpinLockCreate(
			&attributes,
			&pPdoCtx->HidInterruptChannel.ConnectionStateLock
		)))
		{
			TraceError(
				TRACE_BUSLOGIC,
				"WdfSpinLockCreate for HidInterruptChannel failed with status %!STATUS!",
				status
			);
			break;
		}

		pPdoCtx->HidInterruptChannel.ConnectionState = ConnectionStateInitialized;

		//
		// Store reported remote name to assign to property later
		// 
		pPdoCtx->RemoteName.MaximumLength = BTH_MAX_NAME_SIZE * sizeof(WCHAR);
		pPdoCtx->RemoteName.Buffer = ExAllocatePoolWithTag(
			NonPagedPoolNx,
			BTH_MAX_NAME_SIZE * sizeof(WCHAR),
			BTHPS_POOL_TAG
		);

		//
		// We're ready, expose interface
		// 
		DMF_IoctlHandler_IoctlStateSet(pPdoCtx->DmfModuleIoctlHandler, TRUE);

	} while (FALSE);

	//
	// TODO: add error handling
	// 

	if (hKey)
	{
		WdfRegistryClose(hKey);
	}

	FuncExit(TRACE_BUSLOGIC, "status=%!STATUS!", status);

	return status;
}

//
// Retrieves an existing connection from connection list identified by BTH_ADDR
// 
NTSTATUS
BthPS3_PDO_RetrieveByBthAddr(
	_In_ PBTHPS3_SERVER_CONTEXT Context,
	_In_ BTH_ADDR RemoteAddress,
	_Out_ PBTHPS3_PDO_CONTEXT* PdoContext
)
{
	NTSTATUS status = STATUS_NOT_FOUND;

	FuncEntryArguments(
		TRACE_BUSLOGIC,
		"RemoteAddress=%012llX",
		RemoteAddress
	);

	WdfSpinLockAcquire(Context->Header.ClientsLock);

	const ULONG itemCount = WdfCollectionGetCount(Context->Header.Clients);

	for (ULONG index = 0; index < itemCount; index++)
	{
		const WDFDEVICE currentPdo = WdfCollectionGetItem(Context->Header.Clients, index);
		const PBTHPS3_PDO_CONTEXT pPdoCtx = GetPdoContext(currentPdo);

		if (pPdoCtx->RemoteAddress == RemoteAddress)
		{
			TraceVerbose(
				TRACE_BUSLOGIC,
				"Found desired connection item in connection list"
			);

			status = STATUS_SUCCESS;
			*PdoContext = pPdoCtx;
			break;
		}
	}

	WdfSpinLockRelease(Context->Header.ClientsLock);

	FuncExit(TRACE_BUSLOGIC, "status=%!STATUS!", status);

	return status;
}

VOID
BthPS3_PDO_Destroy(
	_In_ PBTHPS3_DEVICE_CONTEXT_HEADER Context,
	_In_ PBTHPS3_PDO_CONTEXT ClientConnection
)
{
	FuncEntryArguments(
		TRACE_BUSLOGIC,
		"ClientConnection=0x%p",
		ClientConnection
	);

	WdfSpinLockAcquire(Context->ClientsLock);

	const WDFDEVICE device = WdfObjectContextGetObject(ClientConnection);
	const ULONG itemCount = WdfCollectionGetCount(Context->Clients);

	for (ULONG index = 0; index < itemCount; index++)
	{
		const WDFDEVICE currentPdo = WdfCollectionGetItem(Context->Clients, index);

		if (currentPdo == device)
		{
			TraceVerbose(
				TRACE_BUSLOGIC,
				"Found desired connection item in connection list"
			);

			const PBTHPS3_PDO_CONTEXT pPdoCtx = GetPdoContext(currentPdo);

			NTSTATUS status = DMF_Pdo_DeviceUnPlugEx(
				Context->DmfModulePdo,
				(PWSTR)WdfMemoryGetBuffer(pPdoCtx->HardwareId, NULL),
				pPdoCtx->SerialNumber
			);

			if (!NT_SUCCESS(status))
			{
				TraceError(
					TRACE_BUSLOGIC,
					"DMF_Pdo_DeviceUnPlugEx failed with status %!STATUS!",
					status
				);
			}

			WdfCollectionRemoveItem(Context->Clients, index);

			//
			// TODO: implement me!
			// 

			break;
		}
	}

	WdfSpinLockRelease(Context->ClientsLock);

	FuncExitNoReturn(TRACE_BUSLOGIC);
}
