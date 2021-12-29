#include "vhci.h"
#include "trace.h"
#include "vhci.tmh"

#include "vhci_plugin.h"
#include "usbreq.h"
#include "vhci_pnp.h"
#include "vhci_irp.h"

#include <ntstrsafe.h>
#include <usbdi.h>

GLOBALS Globals;
NPAGED_LOOKASIDE_LIST g_lookaside;

namespace
{

PAGEABLE void cleanup_vpdo(vhci_dev_t * vhci, PIRP irp)
{
	PAGED_CODE();

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	auto vpdo = static_cast<vpdo_dev_t*>(irpstack->FileObject->FsContext);

	if (vpdo) {
		vpdo->fo = nullptr;
		irpstack->FileObject->FsContext = nullptr;
		if (vpdo->plugged) {
			vhci_unplug_port(vhci, (CHAR)vpdo->port);
		}
	}
}

PAGEABLE NTSTATUS vhci_create(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PAGED_CODE();

	auto vdev = devobj_to_vdev(devobj);

	if (vdev->DevicePnPState == Deleted) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: no such device", vdev->type);
		return irp_done(Irp, STATUS_NO_SUCH_DEVICE);
	}

	TraceCall("%!vdev_type_t!: irql !%!irql!", vdev->type, KeGetCurrentIrql());

	Irp->IoStatus.Information = 0;
	return irp_done_success(Irp);
}

PAGEABLE NTSTATUS vhci_cleanup(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PAGED_CODE();

	vdev_t *vdev = devobj_to_vdev(devobj);

	if (vdev->DevicePnPState == Deleted) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: no such device", vdev->type);
		return irp_done(irp, STATUS_NO_SUCH_DEVICE);
	}

	TraceCall("%!vdev_type_t!: irql !%!irql!", vdev->type, KeGetCurrentIrql());

	if (vdev->type == VDEV_VHCI) {
		cleanup_vpdo((vhci_dev_t*)vdev, irp);
	}

	irp->IoStatus.Information = 0;
	return irp_done_success(irp);
}

PAGEABLE NTSTATUS vhci_close(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PAGED_CODE();

	vdev_t *vdev = devobj_to_vdev(devobj);

	if (vdev->DevicePnPState == Deleted) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: no such device", vdev->type);
		return irp_done(Irp, STATUS_NO_SUCH_DEVICE);
	}

	TraceCall("%!vdev_type_t!: irql !%!irql!", vdev->type, KeGetCurrentIrql());

	Irp->IoStatus.Information = 0;
	return irp_done_success(Irp);
}

PAGEABLE void vhci_driverUnload(__in DRIVER_OBJECT *drvobj)
{
	PAGED_CODE();
	TraceCall("Enter");

	ExDeleteNPagedLookasideList(&g_lookaside);
	NT_ASSERT(!drvobj->DeviceObject);

	if (Globals.RegistryPath.Buffer) {
		ExFreePool(Globals.RegistryPath.Buffer);
		Globals.RegistryPath.Buffer = nullptr;
	}

	WPP_CLEANUP(drvobj);
}

/*
* Set only if such value does not exist.
*/
PAGEABLE NTSTATUS set_verbose_on(HANDLE h)
{
	PAGED_CODE();

	UNICODE_STRING name;
	RtlInitUnicodeString(&name, L"VerboseOn");

	ULONG len = 0;
	KEY_VALUE_PARTIAL_INFORMATION info;

	NTSTATUS st = ZwQueryValueKey(h, &name, KeyValuePartialInformation, &info, sizeof(info), &len);

	if (st == STATUS_OBJECT_NAME_NOT_FOUND) {
		DWORD val = 1;
		st = ZwSetValueKey(h, &name, 0, REG_DWORD, &val, sizeof(val));
	} else {
		NT_ASSERT(!st);
	}

	return st;
}

/*
* Configure Inflight Trace Recorder (IFR) parameter "VerboseOn".
* The default setting of zero causes the IFR to log errors, warnings, and informational events.
* Set to one to add verbose output to the log.
*
* reg add "HKLM\SYSTEM\ControlSet001\Services\usbip_vhci\Parameters" /v VerboseOn /t REG_DWORD /d 1 /f
*/
PAGEABLE NTSTATUS set_ifr_verbose(const UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	UNICODE_STRING params;
	RtlInitUnicodeString(&params, L"\\Parameters");

	UNICODE_STRING path;
	path.Length = 0;
	path.MaximumLength = RegistryPath->Length + params.Length;
	path.Buffer = (PWCH)ExAllocatePoolWithTag(PagedPool, path.MaximumLength + sizeof(*path.Buffer), USBIP_VHCI_POOL_TAG);

	if (!path.Buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NTSTATUS st = RtlUnicodeStringCopy(&path, RegistryPath);
	NT_ASSERT(!st);

	st = RtlUnicodeStringCat(&path, &params);
	NT_ASSERT(!st);

	OBJECT_ATTRIBUTES attrs;
	InitializeObjectAttributes(&attrs, &path, OBJ_KERNEL_HANDLE, nullptr, nullptr);

	HANDLE h = nullptr;
	st = ZwCreateKey(&h, KEY_WRITE, &attrs, 0, nullptr, 0, nullptr);
	if (!st) {
		st = set_verbose_on(h);
		ZwClose(h);
	}

	ExFreePoolWithTag(path.Buffer, USBIP_VHCI_POOL_TAG);
	return st;
}

} // namespace


extern "C" {

PAGEABLE DRIVER_ADD_DEVICE vhci_add_device;
	
PAGEABLE __drv_dispatchType(IRP_MJ_READ) DRIVER_DISPATCH vhci_read;
PAGEABLE __drv_dispatchType(IRP_MJ_WRITE) DRIVER_DISPATCH vhci_write;
PAGEABLE __drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH vhci_ioctl;
PAGEABLE __drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL) DRIVER_DISPATCH vhci_internal_ioctl;
PAGEABLE __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH vhci_pnp;
PAGEABLE __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL) DRIVER_DISPATCH vhci_system_control;
	 __drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH vhci_power;

PAGEABLE NTSTATUS DriverEntry(__in DRIVER_OBJECT *drvobj, __in UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	NTSTATUS st = set_ifr_verbose(RegistryPath);
	WPP_INIT_TRACING(drvobj, RegistryPath);

	if (st) {
		Trace(TRACE_LEVEL_CRITICAL, "Can't set IFR parameter: %!STATUS!", st);
		WPP_CLEANUP(drvobj);
		return st;
	}

	TraceCall("RegistryPath '%!USTR!'", RegistryPath);

	ExInitializeNPagedLookasideList(&g_lookaside, nullptr, nullptr, 0, sizeof(struct urb_req), 'USBV', 0);

	// Save the RegistryPath for WMI
	Globals.RegistryPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
	Globals.RegistryPath.Length = RegistryPath->Length;
	Globals.RegistryPath.Buffer = (PWCH)ExAllocatePoolWithTag(PagedPool, Globals.RegistryPath.MaximumLength, USBIP_VHCI_POOL_TAG);

	if (Globals.RegistryPath.Buffer) {
		RtlCopyUnicodeString(&Globals.RegistryPath, RegistryPath);
	} else {
		Trace(TRACE_LEVEL_CRITICAL, "ExAllocatePoolWithTag failed");
		vhci_driverUnload(drvobj);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	drvobj->MajorFunction[IRP_MJ_CREATE] = vhci_create;
	drvobj->MajorFunction[IRP_MJ_CLEANUP] = vhci_cleanup;
	drvobj->MajorFunction[IRP_MJ_CLOSE] = vhci_close;
	drvobj->MajorFunction[IRP_MJ_READ] = vhci_read;
	drvobj->MajorFunction[IRP_MJ_WRITE] = vhci_write;
	drvobj->MajorFunction[IRP_MJ_PNP] = vhci_pnp;
	drvobj->MajorFunction[IRP_MJ_POWER] = vhci_power;
	drvobj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = vhci_ioctl;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = vhci_internal_ioctl;
	drvobj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = vhci_system_control;

	drvobj->DriverUnload = vhci_driverUnload;
	drvobj->DriverExtension->AddDevice = vhci_add_device;

	return STATUS_SUCCESS;
}

} // extern "C"
