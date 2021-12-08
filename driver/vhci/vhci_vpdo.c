#include "vhci_vpdo.h"
#include "trace.h"
#include "vhci_vpdo.tmh"

#include "vhci.h"
#include "usbreq.h"
#include "devconf.h"

PAGEABLE NTSTATUS vpdo_select_config(vpdo_dev_t *vpdo, struct _URB_SELECT_CONFIGURATION *cfg)
{
	if (vpdo->dsc_conf) {
		ExFreePoolWithTag(vpdo->dsc_conf, USBIP_VHCI_POOL_TAG);
		vpdo->dsc_conf = NULL;
	}

	USB_CONFIGURATION_DESCRIPTOR *new_conf = cfg->ConfigurationDescriptor;
	if (!new_conf) {
		TraceInfo(TRACE_VPDO, "going to unconfigured state");
		return STATUS_SUCCESS;
	}

	vpdo->dsc_conf = ExAllocatePoolWithTag(NonPagedPool, new_conf->wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo->dsc_conf) {
		RtlCopyMemory(vpdo->dsc_conf, new_conf, new_conf->wTotalLength);
	} else {
		TraceError(TRACE_WRITE, "failed to allocate configuration descriptor: out of memory");
		return STATUS_UNSUCCESSFUL;
	}

	const void *cfg_end = (char*)cfg + cfg->Hdr.Length;

	NTSTATUS status = setup_config(vpdo->dsc_conf, &cfg->Interface, cfg_end, vpdo->speed);
	if (NT_SUCCESS(status)) {
		cfg->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)0x12345678; // meaningless value, handle value is not used
	}

	return status;
}

PAGEABLE NTSTATUS vpdo_select_interface(vpdo_dev_t *vpdo, USBD_INTERFACE_INFORMATION *iface)
{
	if (!vpdo->dsc_conf) {
		TraceWarning(TRACE_WRITE, "Empty configuration descriptor");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	NTSTATUS status = setup_intf(iface, vpdo->dsc_conf, vpdo->speed);

	if (NT_SUCCESS(status)) {
		vpdo->current_intf_num = iface->InterfaceNumber;
		vpdo->current_intf_alt = iface->AlternateSetting;
	}

	return status;
}

static PAGEABLE bool copy_ep(int i, USB_ENDPOINT_DESCRIPTOR *d, void *data)
{
	USB_PIPE_INFO *pi = (USB_PIPE_INFO*)data + i;

	RtlCopyMemory(&pi->EndpointDescriptor, d, sizeof(*d));
	pi->ScheduleOffset = 0; // TODO

	return false;
}

PAGEABLE NTSTATUS
vpdo_get_nodeconn_info(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION conninfo, PULONG poutlen)
{
	ULONG outlen = 0;
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	conninfo->DeviceAddress = (USHORT)conninfo->ConnectionIndex;
	conninfo->NumberOfOpenPipes = 0;
	conninfo->DeviceIsHub = FALSE;

	if (vpdo == NULL) {
		conninfo->ConnectionStatus = NoDeviceConnected;
		conninfo->LowSpeed = FALSE;
		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		status = STATUS_SUCCESS;
	} else {
		if (!vpdo->dsc_dev) {
			return STATUS_INVALID_PARAMETER;
		}

		conninfo->ConnectionStatus = DeviceConnected;

		RtlCopyMemory(&conninfo->DeviceDescriptor, vpdo->dsc_dev, sizeof(*vpdo->dsc_dev));

		if (vpdo->dsc_conf) {
			conninfo->CurrentConfigurationValue = vpdo->dsc_conf->bConfigurationValue;
		}

		conninfo->LowSpeed = vpdo->speed == USB_SPEED_LOW || vpdo->speed == USB_SPEED_FULL;

		USB_INTERFACE_DESCRIPTOR *dsc_intf = dsc_find_intf(vpdo->dsc_conf, vpdo->current_intf_num, vpdo->current_intf_alt);
		if (dsc_intf) {
			conninfo->NumberOfOpenPipes = dsc_intf->bNumEndpoints;
		}

		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * conninfo->NumberOfOpenPipes;
		if (*poutlen < outlen) {
			status = STATUS_BUFFER_TOO_SMALL;
		} else {
			if (conninfo->NumberOfOpenPipes > 0) {
				dsc_for_each_endpoint(vpdo->dsc_conf, dsc_intf, copy_ep, conninfo->PipeList);
			}
			status = STATUS_SUCCESS;
		}
	}

	*poutlen = outlen;
	return status;
}

PAGEABLE NTSTATUS
vpdo_get_nodeconn_info_ex(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX conninfo, PULONG poutlen)
{
	ULONG outlen = 0;
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	conninfo->DeviceAddress = (USHORT)conninfo->ConnectionIndex;
	conninfo->NumberOfOpenPipes = 0;
	conninfo->DeviceIsHub = FALSE;

	if (!vpdo) {
		conninfo->ConnectionStatus = NoDeviceConnected;
		conninfo->Speed = UsbFullSpeed;
		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		status = STATUS_SUCCESS;
	} else {
		if (!vpdo->dsc_dev) {
			return STATUS_INVALID_PARAMETER;
		}

		conninfo->ConnectionStatus = DeviceConnected;
		RtlCopyMemory(&conninfo->DeviceDescriptor, vpdo->dsc_dev, sizeof(USB_DEVICE_DESCRIPTOR));

		if (vpdo->dsc_conf) {
			conninfo->CurrentConfigurationValue = vpdo->dsc_conf->bConfigurationValue;
		}

		conninfo->Speed = vpdo->speed;

		USB_INTERFACE_DESCRIPTOR *dsc_intf = dsc_find_intf(vpdo->dsc_conf, vpdo->current_intf_num, vpdo->current_intf_alt);
		if (dsc_intf) {
			conninfo->NumberOfOpenPipes = dsc_intf->bNumEndpoints;
		}

		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * conninfo->NumberOfOpenPipes;
		if (*poutlen < outlen) {
			status = STATUS_BUFFER_TOO_SMALL;
		} else {
			if (conninfo->NumberOfOpenPipes > 0) {
				dsc_for_each_endpoint(vpdo->dsc_conf, dsc_intf, copy_ep, conninfo->PipeList);
			}
			status = STATUS_SUCCESS;
		}
	}

	*poutlen = outlen;
	return status;
}

PAGEABLE NTSTATUS
vpdo_get_nodeconn_info_ex_v2(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX_V2 conninfo, PULONG poutlen)
{
	UNREFERENCED_PARAMETER(vpdo);

	conninfo->SupportedUsbProtocols.ul = 0;
	conninfo->SupportedUsbProtocols.Usb110 = TRUE;
	conninfo->SupportedUsbProtocols.Usb200 = TRUE;
	conninfo->Flags.ul = 0;
	conninfo->Flags.DeviceIsOperatingAtSuperSpeedOrHigher = FALSE;
	conninfo->Flags.DeviceIsSuperSpeedCapableOrHigher = FALSE;
	conninfo->Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher = FALSE;
	conninfo->Flags.DeviceIsSuperSpeedPlusCapableOrHigher = FALSE;

	*poutlen = sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2);

	return STATUS_SUCCESS;
}