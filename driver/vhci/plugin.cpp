#include "plugin.h"
#include "trace.h"
#include "plugin.tmh"

#include "strutil.h"
#include "vhub.h"
#include "pnp.h"
#include "usb_util.h"
#include "usbdsc.h"
#include "vhci.h"

namespace
{

PAGEABLE void vhci_init_vpdo(vpdo_dev_t * vpdo)
{
	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, "vhci_init_vpdo: %p", vpdo);

	vpdo->plugged = TRUE;

	vpdo->current_intf_num = 0;
	vpdo->current_intf_alt = 0;

	INITIALIZE_PNP_STATE(vpdo);

	// vpdo usually starts its life at D3
	vpdo->DevicePowerState = PowerDeviceD3;
	vpdo->SystemPowerState = PowerSystemWorking;

	InitializeListHead(&vpdo->head_urbr);
	InitializeListHead(&vpdo->head_urbr_pending);
	InitializeListHead(&vpdo->head_urbr_sent);
	KeInitializeSpinLock(&vpdo->lock_urbr);

	to_devobj(vpdo)->Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	InitializeListHead(&vpdo->Link);

	vhub_attach_vpdo(vhub_from_vpdo(vpdo), vpdo);

	// This should be the last step in initialization.
	to_devobj(vpdo)->Flags &= ~DO_DEVICE_INITIALIZING;
}

PAGEABLE auto setup_vpdo_with_descriptor(vpdo_dev_t *vpdo, const USB_DEVICE_DESCRIPTOR &d)
{
	PAGED_CODE();

	if (is_valid_dsc(&d)) {
		NT_ASSERT(!is_valid_dsc(&vpdo->descriptor)); // first time initialization
		RtlCopyMemory(&vpdo->descriptor, &d, sizeof(d));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid device descriptor");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo->speed = get_usb_speed(d.bcdUSB);

	vpdo->bDeviceClass = d.bDeviceClass;
	vpdo->bDeviceSubClass = d.bDeviceSubClass;
	vpdo->bDeviceProtocol = d.bDeviceProtocol;

	return STATUS_SUCCESS;
}

/*
* Many devices have 0 usb class number in a device descriptor.
* 0 value means that class number is determined at interface level.
* USB class, subclass and protocol numbers should be setup before importing.
* Because windows vhci driver builds a device compatible id with those numbers.
*/
PAGEABLE auto setup_vpdo_with_dsc_conf(vpdo_dev_t *vpdo, const USB_CONFIGURATION_DESCRIPTOR &d)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo->actconfig); // first time initialization
	vpdo->actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePoolWithTag(PagedPool, d.wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo->actconfig) {
		RtlCopyMemory(vpdo->actconfig, &d, d.wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate configuration descriptor, wTotalLength %d", d.wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (vpdo->bDeviceClass || vpdo->bDeviceSubClass || vpdo->bDeviceProtocol) {
		return STATUS_SUCCESS;
	}

	if (auto i = dsc_find_next_intf(vpdo->actconfig, nullptr)) {
		vpdo->bDeviceClass = i->bInterfaceClass;
		vpdo->bDeviceSubClass = i->bInterfaceSubClass;
		vpdo->bDeviceProtocol = i->bInterfaceProtocol;
		Trace(TRACE_LEVEL_VERBOSE, "Set Class/SubClass/Protocol from bInterfaceNumber %d, bAlternateSetting %d",
						i->bInterfaceNumber, i->bAlternateSetting);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Interface descriptor not found");
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

} // namespace


PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, vhci_pluginfo_t *pluginfo, ULONG inlen, FILE_OBJECT *fo)
{
	PAGED_CODE();

	if (inlen < sizeof(*pluginfo)) {
		Trace(TRACE_LEVEL_ERROR, "too small input length: %lld < %lld", inlen, sizeof(*pluginfo));
		return STATUS_INVALID_PARAMETER;
	}

	auto wTotalLength = pluginfo->dscr_conf.wTotalLength;

	if (inlen != sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf)) {
		Trace(TRACE_LEVEL_ERROR, "invalid pluginfo format: %lld != %lld", inlen, sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf));
		return STATUS_INVALID_PARAMETER;
	}

	pluginfo->port = vhub_get_empty_port(vhub_from_vhci(vhci));
	if (pluginfo->port < 0) {
		return STATUS_END_OF_FILE;
	}

	Trace(TRACE_LEVEL_INFORMATION, "Port #%d", pluginfo->port);

	auto devobj = vdev_create(to_devobj(vhci)->DriverObject, VDEV_VPDO);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	auto vpdo = devobj_to_vpdo_or_null(devobj);
	vpdo->parent = vhub_from_vhci(vhci);

	if (auto err = setup_vpdo_with_descriptor(vpdo, pluginfo->dscr_dev)) {
		IoDeleteDevice(devobj);
		return err;
	}
	
	if (auto err = setup_vpdo_with_dsc_conf(vpdo, pluginfo->dscr_conf)) {
		IoDeleteDevice(devobj);
		return err;
	}

	vpdo->SerialNumberUser = *pluginfo->wserial ? libdrv_strdupW(pluginfo->wserial) : nullptr;

	auto devpdo_old = (vpdo_dev_t*)InterlockedCompareExchangePointer(&fo->FsContext, vpdo, 0);
	if (devpdo_old) {
		Trace(TRACE_LEVEL_INFORMATION, "you can't plugin again");
		IoDeleteDevice(devobj);
		return STATUS_INVALID_PARAMETER;
	}

	vpdo->port = pluginfo->port;
	vpdo->fo = fo;
	vpdo->devid = pluginfo->devid;

	vhci_init_vpdo(vpdo);

	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhci->pdo, BusRelations);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhci_unplug_port(vhci_dev_t * vhci, CHAR port)
{
	PAGED_CODE();

	vhub_dev_t *	vhub = vhub_from_vhci(vhci);
	vpdo_dev_t *	vpdo;

	if (vhub == nullptr) {
		Trace(TRACE_LEVEL_INFORMATION, "vhub has gone");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port < 0) {
		Trace(TRACE_LEVEL_INFORMATION, "plugging out all the devices!");
		vhub_mark_unplugged_all_vpdos(vhub);
		return STATUS_SUCCESS;
	}

	Trace(TRACE_LEVEL_INFORMATION, "plugging out device: port %u", port);

	vpdo = vhub_find_vpdo(vhub, port);
	if (vpdo == nullptr) {
		Trace(TRACE_LEVEL_INFORMATION, "no matching vpdo: port %u", port);
		return STATUS_NO_SUCH_DEVICE;
	}

	vhub_mark_unplugged_vpdo(vhub, vpdo);
	vdev_del_ref(vpdo);

	return STATUS_SUCCESS;
}