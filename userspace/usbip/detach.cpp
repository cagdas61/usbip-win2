/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"

#include <libusbip\vhci.h>
#include <spdlog\spdlog.h>

bool usbip::cmd_detach(void *p)
{
	auto &r = *reinterpret_cast<detach_args*>(p);

	auto dev = vhci::open();
	if (!dev) {
		return false;
	}

	auto ret = vhci::detach(dev.get(), r.port);
	dev.reset();

	if (ret == ERR_NONE) {
		if (r.port <= 0) {
			printf("all ports are detached\n");
		} else {
			printf("port %d is succesfully detached\n", r.port);
		}
		return true;
	}

	switch (ret) {
	case ERR_INVARG:
		spdlog::error("invalid port {}", r.port);
		break;
	case ERR_NOTEXIST:
		spdlog::error("non-existent port {}", r.port);
		break;
	default:
		spdlog::error("failed to detach");
	}

	return false;
}
