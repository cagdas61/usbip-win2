#pragma once

#include "usbip_vhci_api.h"

HANDLE usbip_vhci_driver_open(void);
void usbip_vhci_driver_close(HANDLE hdev);
int usbip_vhci_get_imported_devs(HANDLE hdev, pioctl_usbip_vhci_imported_dev_t *pidevs);
int usbip_vhci_attach_device(HANDLE hdev, struct vhci_pluginfo_t *pluginfo);
int usbip_vhci_detach_device(HANDLE hdev, int port);
