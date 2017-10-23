/*
 * Copyright (c) 2011 Jan Vesely
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libusbhost
 * @{
 */
/** @file
 *
 */

#include <usb/debug.h>
#include <usb/request.h>

#include <assert.h>
#include <async.h>
#include <errno.h>
#include <usb_iface.h>
#include <str_error.h>

#include "hcd.h"


/** Initialize hcd_t structure.
 * Initializes device and endpoint managers. Sets data and hook pointer to NULL.
 *
 * @param hcd hcd_t structure to initialize, non-null.
 * @param max_speed Maximum supported USB speed (full, high).
 * @param bandwidth Available bandwidth, passed to endpoint manager.
 * @param bw_count Bandwidth compute function, passed to endpoint manager.
 */
void hcd_init(hcd_t *hcd) {
	assert(hcd);

	hcd_set_implementation(hcd, NULL, NULL, NULL);
}

usb_address_t hcd_request_address(hcd_t *hcd, usb_speed_t speed)
{
	assert(hcd);
	usb_address_t address = 0;
	const int ret = bus_request_address(hcd->bus, &address, false, speed);
	if (ret != EOK)
		return ret;
	return address;
}

/** Prepare generic usb_transfer_batch and schedule it.
 * @param hcd Host controller driver.
 * @param target address and endpoint number.
 * @param setup_data Data to use in setup stage (Control communication type)
 * @param in Callback for device to host communication.
 * @param out Callback for host to device communication.
 * @param arg Callback parameter.
 * @param name Communication identifier (for nicer output).
 * @return Error code.
 */
int hcd_send_batch(hcd_t *hcd, device_t *device, usb_target_t target,
    usb_direction_t direction, char *data, size_t size, uint64_t setup_data,
    usb_transfer_batch_callback_t on_complete, void *arg, const char *name)
{
	assert(hcd);
	assert(device->address == target.address);

	endpoint_t *ep = bus_find_endpoint(hcd->bus, device, target, direction);
	if (ep == NULL) {
		usb_log_error("Endpoint(%d:%d) not registered for %s.\n",
		    device->address, target.endpoint, name);
		return ENOENT;
	}

	usb_log_debug2("%s %d:%d %zu(%zu).\n",
	    name, target.address, target.endpoint, size, ep->max_packet_size);

	const size_t bw = bus_count_bw(ep, size);
	/* Check if we have enough bandwidth reserved */
	if (ep->bandwidth < bw) {
		usb_log_error("Endpoint(%d:%d) %s needs %zu bw "
		    "but only %zu is reserved.\n",
		    ep->target.address, ep->target.endpoint, name, bw, ep->bandwidth);
		return ENOSPC;
	}
	if (!hcd->ops.schedule) {
		usb_log_error("HCD does not implement scheduler.\n");
		return ENOTSUP;
	}

	usb_transfer_batch_t *batch = usb_transfer_batch_create(ep);
	if (!batch) {
		usb_log_error("Failed to create transfer batch.\n");
		return ENOMEM;
	}

	batch->buffer = data;
	batch->buffer_size = size;
	batch->setup.packed = setup_data;
	batch->dir = direction;
	batch->on_complete = on_complete;
	batch->on_complete_data = arg;

	/* Check for commands that reset toggle bit */
	if (ep->transfer_type == USB_TRANSFER_CONTROL)
		batch->toggle_reset_mode
			= usb_request_get_toggle_reset_mode(&batch->setup.packet);

	const int ret = hcd->ops.schedule(hcd, batch);
	if (ret != EOK) {
		usb_log_warning("Batch %p failed to schedule: %s", batch, str_error(ret));
		usb_transfer_batch_destroy(batch);
	}

	/* Drop our own reference to ep. */
	endpoint_del_ref(ep);

	return ret;
}

typedef struct {
	fibril_mutex_t done_mtx;
	fibril_condvar_t done_cv;
	unsigned done;

	size_t transfered_size;
	int error;
} sync_data_t;

static int sync_transfer_complete(usb_transfer_batch_t *batch)
{
	sync_data_t *d = batch->on_complete_data;
	assert(d);
	d->transfered_size = batch->transfered_size;
	d->error = batch->error;
	fibril_mutex_lock(&d->done_mtx);
	d->done = 1;
	fibril_condvar_broadcast(&d->done_cv);
	fibril_mutex_unlock(&d->done_mtx);
	return EOK;
}

ssize_t hcd_send_batch_sync(hcd_t *hcd, device_t *device, usb_target_t target,
    usb_direction_t direction, char *data, size_t size, uint64_t setup_data,
    const char *name)
{
	assert(hcd);
	sync_data_t sd = { .done = 0 };
	fibril_mutex_initialize(&sd.done_mtx);
	fibril_condvar_initialize(&sd.done_cv);

	const int ret = hcd_send_batch(hcd, device, target, direction,
	    data, size, setup_data,
	    sync_transfer_complete, &sd, name);
	if (ret != EOK)
		return ret;

	fibril_mutex_lock(&sd.done_mtx);
	while (!sd.done)
		fibril_condvar_wait(&sd.done_cv, &sd.done_mtx);
	fibril_mutex_unlock(&sd.done_mtx);

	return (sd.error == EOK)
		? (ssize_t) sd.transfered_size
		: (ssize_t) sd.error;
}


/**
 * @}
 */
