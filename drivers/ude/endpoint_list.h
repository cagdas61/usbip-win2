/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "context.h"

namespace usbip
{

struct compare_algo
{
        virtual bool operator() (const endpoint_ctx &endp) const = 0;
};

struct compare_handle : compare_algo
{
        explicit compare_handle(USBD_PIPE_HANDLE h) : handle(h) { NT_ASSERT(handle); }
        constexpr bool operator() (const endpoint_ctx &endp) const override { return endp.PipeHandle == handle; }

        USBD_PIPE_HANDLE handle;
};

struct compare_descr : compare_algo
{
        compare_descr(const USBD_PIPE_INFORMATION &p) : pipe(p) {}
        bool operator()(const endpoint_ctx &endp) const override;

        USBD_PIPE_INFORMATION pipe;
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void insert_endpoint_list(_In_ endpoint_ctx &endp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void remove_endpoint_list(_In_ endpoint_ctx &endp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
endpoint_ctx *find_endpoint(_In_ device_ctx &dev, _In_ const compare_algo &compare);

} // namespace usbip