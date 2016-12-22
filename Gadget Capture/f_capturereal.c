#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include "g_capture.h"
#include "../gadget_chips.h"

struct f_capturereal {
	struct usb_function	function;

	struct usb_ep		*in_ep;
	struct usb_ep		*out_ep;
	struct usb_ep		*iso_in_ep;
	struct usb_ep		*iso_out_ep;
	int			cur_alt;
};

static unsigned pattern;
static unsigned isoc_interval;
static unsigned isoc_maxpacket;
static unsigned isoc_mult;
static unsigned isoc_maxburst;
static unsigned buflen;

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor capture_real_intf_alt0 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

static struct usb_interface_descriptor capture_real_intf_alt1 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	1,
	.bNumEndpoints =	4,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_iso_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1023),
	.bInterval =		4,
};

static struct usb_endpoint_descriptor fs_iso_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1023),
	.bInterval =		4,
};

static struct usb_descriptor_header *fs_capture_real_descs[] = {
	(struct usb_descriptor_header *) &capture_real_intf_alt0,
	(struct usb_descriptor_header *) &fs_sink_desc,
	(struct usb_descriptor_header *) &fs_source_desc,
	(struct usb_descriptor_header *) &capture_real_intf_alt1,
#define FS_ALT_IFC_1_OFFSET	3
	(struct usb_descriptor_header *) &fs_sink_desc,
	(struct usb_descriptor_header *) &fs_source_desc,
	(struct usb_descriptor_header *) &fs_iso_sink_desc,
	(struct usb_descriptor_header *) &fs_iso_source_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_iso_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_endpoint_descriptor hs_iso_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_descriptor_header *hs_capture_real_descs[] = {
	(struct usb_descriptor_header *) &capture_real_intf_alt0,
	(struct usb_descriptor_header *) &hs_source_desc,
	(struct usb_descriptor_header *) &hs_sink_desc,
	(struct usb_descriptor_header *) &capture_real_intf_alt1,
#define HS_ALT_IFC_1_OFFSET	3
	(struct usb_descriptor_header *) &hs_source_desc,
	(struct usb_descriptor_header *) &hs_sink_desc,
	(struct usb_descriptor_header *) &hs_iso_source_desc,
	(struct usb_descriptor_header *) &hs_iso_sink_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

struct usb_ss_ep_comp_descriptor ss_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

struct usb_ss_ep_comp_descriptor ss_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_iso_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

struct usb_ss_ep_comp_descriptor ss_iso_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor ss_iso_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

struct usb_ss_ep_comp_descriptor ss_iso_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_descriptor_header *ss_capture_real_descs[] = {
	(struct usb_descriptor_header *) &capture_real_intf_alt0,
	(struct usb_descriptor_header *) &ss_source_desc,
	(struct usb_descriptor_header *) &ss_source_comp_desc,
	(struct usb_descriptor_header *) &ss_sink_desc,
	(struct usb_descriptor_header *) &ss_sink_comp_desc,
	(struct usb_descriptor_header *) &capture_real_intf_alt1,
#define SS_ALT_IFC_1_OFFSET	5
	(struct usb_descriptor_header *) &ss_source_desc,
	(struct usb_descriptor_header *) &ss_source_comp_desc,
	(struct usb_descriptor_header *) &ss_sink_desc,
	(struct usb_descriptor_header *) &ss_sink_comp_desc,
	(struct usb_descriptor_header *) &ss_iso_source_desc,
	(struct usb_descriptor_header *) &ss_iso_source_comp_desc,
	(struct usb_descriptor_header *) &ss_iso_sink_desc,
	(struct usb_descriptor_header *) &ss_iso_sink_comp_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_capturereal[] = {
	[0].s = "source and sink data",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_capturereal = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_capturereal,
};

static struct usb_gadget_strings *capturereal_strings[] = {
	&stringtab_capturereal,
	NULL,
};


/**********************************inside func*************************************************/
static void reinit_write_data(struct usb_ep *ep, struct usb_request *req)
{
	unsigned	i;
	u8			*buf = req->buf;

	switch (pattern) {
	case 0:
		memset(req->buf, 0xAA, req->length);
		break;
	case 1:
		for  (i = 0; i < req->length; i++)
			*buf++ = (u8) (i % 63);
		break;
	case 2:
		break;
	}
}

/* optionally require specific source/sink data patterns  */
static int check_read_data(struct f_capturereal *cr, struct usb_request *req)
{
	unsigned		i;
	u8			*buf = req->buf;
	struct usb_composite_dev *cdev = cr->function.config->cdev;

	if (pattern == 2)
		return 0;

	for (i = 0; i < req->actual; i++, buf++) {
		switch (pattern) {

		/* all-zeroes has no synchronization issues */
		case 0:
			if (*buf == 0)
				continue;
			break;

		/* "mod63" stays in sync with short-terminated transfers,
		 * OR otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  Resync is done
		 * with set_interface or set_config.  (We *WANT* it to
		 * get quickly out of sync if controllers or their drivers
		 * stutter for any reason, including buffer duplication...)
		 */
		case 1:
			if (*buf == (u8)(i % 63))
				continue;
			break;
		}
		ERROR(cdev, "bad OUT byte, buf[%d] = %d\n", i, *buf);
		usb_ep_set_halt(cr->out_ep);
		return -EINVAL;
	}
	return 0;
}

void free_ep_req(struct usb_ep *ep, struct usb_request *req)
{
	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

static void capture_real_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev	*cdev;
	struct f_capturereal		*cr = ep->driver_data;
	int	status = req->status;

	/* driver_data will be null if ep has been disabled */
	if (!cr)
		return;

	cdev = cr->function.config->cdev;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == cr->out_ep) {
			check_read_data(cr, req);
			if (pattern != 2)
				memset(req->buf, 0x00, req->length);
		}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG(cdev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		if (ep == cr->out_ep)
			check_read_data(cr, req);
		free_ep_req(ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
#if 1
		DBG(cdev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
#endif
	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		ERROR(cdev, "kill %s:  resubmit %d bytes --> %d\n",
				ep->name, req->length, status);
		usb_ep_set_halt(ep);
		/* FIXME recover later ... somehow */
	}
}

static void disable_ep(struct usb_composite_dev *cdev, struct usb_ep *ep)
{
	int	value;

	if (ep->driver_data) {
		value = usb_ep_disable(ep);
		if (value < 0)
			DBG(cdev, "disable %s --> %d\n",
					ep->name, value);
		ep->driver_data = NULL;
	}
}

void disable_endpoints(struct usb_composite_dev *cdev,
		struct usb_ep *in, struct usb_ep *out,
		struct usb_ep *iso_in, struct usb_ep *iso_out)
{
	disable_ep(cdev, in);
	disable_ep(cdev, out);
	if (iso_in)
		disable_ep(cdev, iso_in);
	if (iso_out)
		disable_ep(cdev, iso_out);
}

struct usb_request *alloc_ep_req(struct usb_ep *ep, int len)
{
	struct usb_request      *req;

	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (req) {
		if (len)
			req->length = len;
		else
			req->length = buflen;
		req->buf = kmalloc(req->length, GFP_ATOMIC);
		if (!req->buf) {
			usb_ep_free_request(ep, req);
			req = NULL;
		}
	}
	return req;
}


static int capture_real_start_ep(struct f_capturereal *cr, bool is_in, bool is_iso, int speed)
{
	struct usb_ep		*ep;
	struct usb_request	*req;
	int	i, size, status;

	for (i = 0; i < 8; i++) {
		if (is_iso) {
			switch (speed) {
			case USB_SPEED_SUPER:
				size = isoc_maxpacket * (isoc_mult + 1) *
						(isoc_maxburst + 1);
				break;
			case USB_SPEED_HIGH:
				size = isoc_maxpacket * (isoc_mult + 1);
				break;
			default:
				size = isoc_maxpacket > 1023 ?
						1023 : isoc_maxpacket;
				break;
			}
			ep = is_in ? cr->iso_in_ep : cr->iso_out_ep;
			req = alloc_ep_req(ep, size);
		} else {
			ep = is_in ? cr->in_ep : cr->out_ep;
			req = alloc_ep_req(ep, 0);
		}

		if (!req)
			return -ENOMEM;

		req->complete = capture_real_complete;
		if (is_in)
			reinit_write_data(ep, req);
		else if (pattern != 2)
			memset(req->buf, 0x55, req->length);

		status = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (status) {
			struct usb_composite_dev	*cdev;

			cdev = cr->function.config->cdev;
			ERROR(cdev, "start %s%s %s --> %d\n",
			      is_iso ? "ISO-" : "", is_in ? "IN" : "OUT",
			      ep->name, status);
			free_ep_req(ep, req);
		}

		if (!is_iso)
			break;
	}

	return status;
}



/*capturereal_bind*/
static inline struct f_capturereal *func_to_cr(struct usb_function *f)
{
	return container_of(f, struct f_capturereal, function);
}

/*capturereal_set_alt*/
static void disable_capture_real(struct f_capturereal *cr)
{
	struct usb_composite_dev	*cdev;

	cdev = cr->function.config->cdev;
	disable_endpoints(cdev, cr->in_ep, cr->out_ep, cr->iso_in_ep,
			cr->iso_out_ep);
	VDBG(cdev, "%s disabled\n", cr->function.name);
}

static int enable_capture_real(struct usb_composite_dev *cdev, struct f_capturereal *cr, int alt)
{
	int					result = 0;
	int					speed = cdev->gadget->speed;
	struct usb_ep				*ep;

	/* one bulk endpoint writes (sources) zeroes IN (to the host) */
	ep = cr->in_ep;
	result = config_ep_by_speed(cdev->gadget, &(cr->function), ep);
	if (result)
		return result;
	result = usb_ep_enable(ep);
	if (result < 0)
		return result;
	ep->driver_data = cr;

	result = capture_real_start_ep(cr, true, false, speed);
	if (result < 0) {
fail:
		ep = cr->in_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		return result;
	}

	/* one bulk endpoint reads (sinks) anything OUT (from the host) */
	ep = cr->out_ep;
	result = config_ep_by_speed(cdev->gadget, &(cr->function), ep);
	if (result)
		goto fail;
	result = usb_ep_enable(ep);
	if (result < 0)
		goto fail;
	ep->driver_data = cr;

	result = capture_real_start_ep(cr, false, false, speed);
	if (result < 0) {
fail2:
		ep = cr->out_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		goto fail;
	}

	if (alt == 0)
		goto out;

	/* one iso endpoint writes (sources) zeroes IN (to the host) */
	ep = cr->iso_in_ep;
	if (ep) {
		result = config_ep_by_speed(cdev->gadget, &(cr->function), ep);
		if (result)
			goto fail2;
		result = usb_ep_enable(ep);
		if (result < 0)
			goto fail2;
		ep->driver_data = cr;

		result = capture_real_start_ep(cr, true, true, speed);
		if (result < 0) {
fail3:
			ep = cr->iso_in_ep;
			if (ep) {
				usb_ep_disable(ep);
				ep->driver_data = NULL;
			}
			goto fail2;
		}
	}

	/* one iso endpoint reads (sinks) anything OUT (from the host) */
	ep = cr->iso_out_ep;
	if (ep) {
		result = config_ep_by_speed(cdev->gadget, &(cr->function), ep);
		if (result)
			goto fail3;
		result = usb_ep_enable(ep);
		if (result < 0)
			goto fail3;
		ep->driver_data = cr;

		result = capture_real_start_ep(cr, false, true, speed);
		if (result < 0) {
			usb_ep_disable(ep);
			ep->driver_data = NULL;
			goto fail3;
		}
	}
out:
	cr->cur_alt = alt;

	DBG(cdev, "%s enabled, alt intf %d\n", cr->function.name, alt);
	return result;
}

/***********************************END inside func END*****************************************/



static int capturereal_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_capturereal	*cr = func_to_cr(f);
	int	id;
	int ret;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	capture_real_intf_alt0.bInterfaceNumber = id;
	capture_real_intf_alt1.bInterfaceNumber = id;

	/* allocate bulk endpoints */
	cr->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_source_desc);
	if (!cr->in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
			f->name, cdev->gadget->name);
		printk("err no dev\n");
		return -ENODEV;
	}
	cr->in_ep->driver_data = cdev;	/* claim */

	cr->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_sink_desc);
	if (!cr->out_ep)
		goto autoconf_fail;
	cr->out_ep->driver_data = cdev;	/* claim */

	/* sanity check the isoc module parameters */
	if (isoc_interval < 1)
		isoc_interval = 1;
	if (isoc_interval > 16)
		isoc_interval = 16;
	if (isoc_mult > 2)
		isoc_mult = 2;
	if (isoc_maxburst > 15)
		isoc_maxburst = 15;

	/* fill in the FS isoc descriptors from the module parameters */
	fs_iso_source_desc.wMaxPacketSize = isoc_maxpacket > 1023 ?
						1023 : isoc_maxpacket;
	fs_iso_source_desc.bInterval = isoc_interval;
	fs_iso_sink_desc.wMaxPacketSize = isoc_maxpacket > 1023 ?
						1023 : isoc_maxpacket;
	fs_iso_sink_desc.bInterval = isoc_interval;

	/* allocate iso endpoints */
	cr->iso_in_ep = usb_ep_autoconfig(cdev->gadget, &fs_iso_source_desc);
	if (!cr->iso_in_ep)
		goto no_iso;
	cr->iso_in_ep->driver_data = cdev;	/* claim */

	cr->iso_out_ep = usb_ep_autoconfig(cdev->gadget, &fs_iso_sink_desc);
	if (cr->iso_out_ep) {
		cr->iso_out_ep->driver_data = cdev;	/* claim */
	} else {
		cr->iso_in_ep->driver_data = NULL;
		cr->iso_in_ep = NULL;
no_iso:
		/*
		 * We still want to work even if the UDC doesn't have isoc
		 * endpoints, so null out the alt interface that contains
		 * them and continue.
		 */
		fs_capture_real_descs[FS_ALT_IFC_1_OFFSET] = NULL;
		hs_capture_real_descs[HS_ALT_IFC_1_OFFSET] = NULL;
		ss_capture_real_descs[SS_ALT_IFC_1_OFFSET] = NULL;
	}

	if (isoc_maxpacket > 1024)
		isoc_maxpacket = 1024;

	/* support high speed hardware */
	hs_source_desc.bEndpointAddress = fs_source_desc.bEndpointAddress;
	hs_sink_desc.bEndpointAddress = fs_sink_desc.bEndpointAddress;

	/*
	 * Fill in the HS isoc descriptors from the module parameters.
	 * We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	hs_iso_source_desc.wMaxPacketSize = isoc_maxpacket;
	hs_iso_source_desc.wMaxPacketSize |= isoc_mult << 11;
	hs_iso_source_desc.bInterval = isoc_interval;
	hs_iso_source_desc.bEndpointAddress =
		fs_iso_source_desc.bEndpointAddress;

	hs_iso_sink_desc.wMaxPacketSize = isoc_maxpacket;
	hs_iso_sink_desc.wMaxPacketSize |= isoc_mult << 11;
	hs_iso_sink_desc.bInterval = isoc_interval;
	hs_iso_sink_desc.bEndpointAddress = fs_iso_sink_desc.bEndpointAddress;

	/* support super speed hardware */
	ss_source_desc.bEndpointAddress =
		fs_source_desc.bEndpointAddress;
	ss_sink_desc.bEndpointAddress =
		fs_sink_desc.bEndpointAddress;

	/*
	 * Fill in the SS isoc descriptors from the module parameters.
	 * We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	ss_iso_source_desc.wMaxPacketSize = isoc_maxpacket;
	ss_iso_source_desc.bInterval = isoc_interval;
	ss_iso_source_comp_desc.bmAttributes = isoc_mult;
	ss_iso_source_comp_desc.bMaxBurst = isoc_maxburst;
	ss_iso_source_comp_desc.wBytesPerInterval =
		isoc_maxpacket * (isoc_mult + 1) * (isoc_maxburst + 1);
	ss_iso_source_desc.bEndpointAddress =
		fs_iso_source_desc.bEndpointAddress;

	ss_iso_sink_desc.wMaxPacketSize = isoc_maxpacket;
	ss_iso_sink_desc.bInterval = isoc_interval;
	ss_iso_sink_comp_desc.bmAttributes = isoc_mult;
	ss_iso_sink_comp_desc.bMaxBurst = isoc_maxburst;
	ss_iso_sink_comp_desc.wBytesPerInterval =
		isoc_maxpacket * (isoc_mult + 1) * (isoc_maxburst + 1);
	ss_iso_sink_desc.bEndpointAddress = fs_iso_sink_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_capture_real_descs,
			hs_capture_real_descs, ss_capture_real_descs);
	if (ret)
		return ret;

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s, ISO-IN/%s, ISO-OUT/%s\n",
	    (gadget_is_superspeed(c->cdev->gadget) ? "super" :
	     (gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full")),
			f->name, cr->in_ep->name, cr->out_ep->name,
			cr->iso_in_ep ? cr->iso_in_ep->name : "<none>",
			cr->iso_out_ep ? cr->iso_out_ep->name : "<none>");
	return 0;
}

static int capturereal_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct f_capturereal		*cr = func_to_cr(f);
	struct usb_composite_dev	*cdev = f->config->cdev;

	if (cr->in_ep->driver_data)
		disable_capture_real(cr);
	return enable_capture_real(cdev, cr, alt);
}

static int capturereal_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_capturereal		*cr = func_to_cr(f);

	return cr->cur_alt;
}

static void capturereal_disable(struct usb_function *f)
{
	struct f_capturereal	*cr = func_to_cr(f);

	disable_capture_real(cr);
}

static int capturereal_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_configuration	*c = f->config;
	struct usb_request			*req = c->cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	req->length = USB_COMP_EP0_BUFSIZ;

	/* composite driver infrastructure handles everything except
	 * the two control test requests.
	 */
	switch (ctrl->bRequest) {

	/*
	 * These are the same vendor-specific requests supported by
	 * Intel's USB 2.0 compliance test devices.  We exceed that
	 * device spec by allowing multiple-packet requests.
	 *
	 * NOTE:  the Control-OUT data stays in req->buf ... better
	 * would be copying it into a scratch buffer, so that other
	 * requests may safely intervene.
	 */
	case 0x5b:	/* control WRITE test -- fill the buffer */
		if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR)) {
			goto unknown;
		}
		if (w_value || w_index) {
			break;
		}
		/* just read that many bytes into the buffer */
		if (w_length > req->length) {
			break;
		}
		value = w_length;
		break;
	case 0x5c:	/* control READ test -- return the buffer */
		if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR)) {
			goto unknown;
		}
		if (w_value || w_index) {
			break;
		}
		/* expect those bytes are still in the buffer; send back */
		if (w_length > req->length) {
			break;
		}
		value = w_length;
		break;

	default:
unknown:
		VDBG(c->cdev,
			"unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		VDBG(c->cdev, "capture/real req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(c->cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			ERROR(c->cdev, "capture/real response, err %d\n",
					value);
		}
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static void capturereal_free_func(struct usb_function *f)
{
	usb_free_all_descriptors(f);
	kfree(func_to_cr(f));
}

static struct usb_function *capture_real_alloc_func(
		struct usb_function_instance *fi)
{
	struct f_capturereal     *cr;
	struct f_cr_opts	*cr_opts;

	cr = kzalloc(sizeof(*cr), GFP_KERNEL);
	if (!cr)
		return NULL;

	cr_opts =  container_of(fi, struct f_cr_opts, func_inst);
	pattern = cr_opts->pattern;
	isoc_interval = cr_opts->isoc_interval;
	isoc_maxpacket = cr_opts->isoc_maxpacket;
	isoc_mult = cr_opts->isoc_mult;
	isoc_maxburst = cr_opts->isoc_maxburst;
	buflen = cr_opts->bulk_buflen;

	cr->function.name = "capture/real";
	cr->function.bind = capturereal_bind;
	cr->function.set_alt = capturereal_set_alt;
	cr->function.get_alt = capturereal_get_alt;
	cr->function.disable = capturereal_disable;
	cr->function.setup = capturereal_setup;
	cr->function.strings = capturereal_strings;

	cr->function.free_func = capturereal_free_func;

	return &cr->function;
}

static void capture_real_free_instance(struct usb_function_instance *fi)
{
	struct f_cr_opts *cr_opts;

	cr_opts = container_of(fi, struct f_cr_opts, func_inst);
	kfree(cr_opts);
}

static struct usb_function_instance *capture_real_alloc_inst(void)
{
	struct f_cr_opts *cr_opts;

	cr_opts = kzalloc(sizeof(*cr_opts), GFP_KERNEL);
	if (!cr_opts)
		return ERR_PTR(-ENOMEM);
	cr_opts->func_inst.free_func_inst = capture_real_free_instance;
	return &cr_opts->func_inst;
}


DECLARE_USB_FUNCTION(CaptureReal, capture_real_alloc_inst,
		capture_real_alloc_func);


static int __init crcf_modinit(void)
{
	int ret;

	ret = usb_function_register(&CaptureRealusb_func);
	if (ret)
		return ret;
	ret = cf_modinit();
	if (ret)
		usb_function_unregister(&CaptureRealusb_func);
	return ret;
}
static void __exit crcf_modexit(void)
{
	usb_function_unregister(&CaptureRealusb_func);
	cf_modexit();
}

module_init(crcf_modinit);
module_exit(crcf_modexit);

MODULE_LICENSE("GPL");
