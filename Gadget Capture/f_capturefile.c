#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/usb/composite.h>

#include "g_capture.h"


static unsigned qlen;
static unsigned buflen;
struct f_capturefile {
	struct usb_function	function;

	struct usb_ep		*in_ep;
	struct usb_ep		*out_ep;
};

static struct usb_interface_descriptor capturefile_intf = {
	.bLength =		sizeof capturefile_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_capturefile_descs[] = {
	(struct usb_descriptor_header *) &capturefile_intf,
	(struct usb_descriptor_header *) &fs_loop_sink_desc,
	(struct usb_descriptor_header *) &fs_loop_source_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_capturefile_descs[] = {
	(struct usb_descriptor_header *) &capturefile_intf,
	(struct usb_descriptor_header *) &hs_loop_source_desc,
	(struct usb_descriptor_header *) &hs_loop_sink_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

struct usb_ss_ep_comp_descriptor ss_loop_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

struct usb_ss_ep_comp_descriptor ss_loop_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_descriptor_header *ss_capturefile_descs[] = {
	(struct usb_descriptor_header *) &capturefile_intf,
	(struct usb_descriptor_header *) &ss_loop_source_desc,
	(struct usb_descriptor_header *) &ss_loop_source_comp_desc,
	(struct usb_descriptor_header *) &ss_loop_sink_desc,
	(struct usb_descriptor_header *) &ss_loop_sink_comp_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_capturefile[] = {
	[0].s = "capture file",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_loop = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_capturefile,
};

static struct usb_gadget_strings *capturefile_strings[] = {
	&stringtab_loop,
	NULL,
};


/**************************************inside func******************************************************/
static inline struct f_capturefile *func_to_cf(struct usb_function *f)
{
	return container_of(f, struct f_capturefile, function);
}

static void disable_capturefile(struct f_capturefile *capture)
{
	struct usb_composite_dev	*cdev;

	cdev = capture->function.config->cdev;
	disable_endpoints(cdev, capture->in_ep, capture->out_ep);
	VDBG(cdev, "%s disabled\n", capture->function.name);
}

static void capturefile_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_capturefile		*capture = ep->driver_data;
	struct usb_composite_dev 	*cdev = capture->function.config->cdev;
	int							status = req->status;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == capture->out_ep) {
			/* loop this OUT packet back IN to the host */
			req->zero = (req->actual < req->length);
			req->length = req->actual;
			status = usb_ep_queue(capture->in_ep, req, GFP_ATOMIC);
			if (status == 0)
				return;

			/* "should never get here" */
			ERROR(cdev, "can't capture %s to %s: %d\n",
				ep->name, capture->in_ep->name,
				status);
		}

		/* queue the buffer for some later OUT packet */
		req->length = buflen;
		status = usb_ep_queue(capture->out_ep, req, GFP_ATOMIC);
		if (status == 0)
			return;

		/* "should never get here" */
		/* FALLTHROUGH */

	default:
		ERROR(cdev, "%s capture complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
		/* FALLTHROUGH */

	/* NOTE:  since this driver doesn't maintain an explicit record
	 * of requests it submitted (just maintains qlen count), we
	 * rely on the hardware driver to clean up on disconnect or
	 * endpoint disable.
	 */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		free_ep_req(ep, req);
		return;
	}
}

static int
enable_capturefile(struct usb_composite_dev *cdev, struct f_capturefile *capture)
{
	int							result = 0;
	struct usb_ep				*ep;
	struct usb_request			*req;
	unsigned					i;

	/* one endpoint writes data back IN to the host */
	ep = capture->in_ep;
	result = config_ep_by_speed(cdev->gadget, &(capture->function), ep);
	if (result)
		return result;
	result = usb_ep_enable(ep);
	if (result < 0)
		return result;
	ep->driver_data = capture;

	/* one endpoint just reads OUT packets */
	ep = capture->out_ep;
	result = config_ep_by_speed(cdev->gadget, &(capture->function), ep);
	if (result)
		goto fail0;

	result = usb_ep_enable(ep);
	if (result < 0) {
fail0:
		ep = capture->in_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		return result;
	}
	ep->driver_data = capture;

	/* allocate a bunch of read buffers and queue them all at once.
	 * we buffer at most 'qlen' transfers; fewer if any need more
	 * than 'buflen' bytes each.
	 */
	for (i = 0; i < qlen && result == 0; i++) {
		req = alloc_ep_req(ep, 0);
		if (req) {
			req->complete = capturefile_complete;
			result = usb_ep_queue(ep, req, GFP_ATOMIC);
			if (result)
				ERROR(cdev, "%s queue req --> %d\n",
						ep->name, result);
		} else {
			usb_ep_disable(ep);
			ep->driver_data = NULL;
			result = -ENOMEM;
			goto fail0;
		}
	}

	DBG(cdev, "%s enabled\n", capture->function.name);
	return result;
}

/****************************** ****END inside func END*************************************************/

static int capturefile_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_capturefile	*loop = func_to_cf(f);
	int			id;
	int ret;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	capturefile_intf.bInterfaceNumber = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_capturefile[0].id = id;
	capturefile_intf.iInterface = id;

	/* allocate endpoints */

	loop->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_loop_source_desc);
	if (!loop->in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
			f->name, cdev->gadget->name);
		return -ENODEV;
	}
	loop->in_ep->driver_data = cdev;	/* claim */

	loop->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_loop_sink_desc);
	if (!loop->out_ep)
		goto autoconf_fail;
	loop->out_ep->driver_data = cdev;	/* claim */

	/* support high speed hardware */
	hs_loop_source_desc.bEndpointAddress =
		fs_loop_source_desc.bEndpointAddress;
	hs_loop_sink_desc.bEndpointAddress = fs_loop_sink_desc.bEndpointAddress;

	/* support super speed hardware */
	ss_loop_source_desc.bEndpointAddress =
		fs_loop_source_desc.bEndpointAddress;
	ss_loop_sink_desc.bEndpointAddress = fs_loop_sink_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_capturefile_descs, hs_capturefile_descs,
			ss_capturefile_descs);
	if (ret)
		return ret;

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
	    (gadget_is_superspeed(c->cdev->gadget) ? "super" :
	     (gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full")),
			f->name, loop->in_ep->name, loop->out_ep->name);
	return 0;
}

static int capturefile_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct f_capturefile		*capture = func_to_cf(f);
	struct usb_composite_dev 	*cdev = f->config->cdev;

	/* we know alt is zero */
	if (capture->in_ep->driver_data)
		disable_capturefile(capture);
	return enable_capturefile(cdev, capture);
}


static void capturefile_disable(struct usb_function *f)
{
	struct f_capturefile	*capture = func_to_cf(f);

	disable_capturefile(capture);
}

static void cf_free_func(struct usb_function *f)
{
	usb_free_all_descriptors(f);
	kfree(func_to_cf(f));
}

static struct usb_function *capturefile_alloc(struct usb_function_instance *fi)
{
	struct f_capturefile	*capture;
	struct f_cf_opts		*cf_opts;

	capture = kzalloc(sizeof *capture, GFP_KERNEL);
	if (!capture)
		return ERR_PTR(-ENOMEM);

	cf_opts = container_of(fi, struct f_cf_opts, func_inst);
	buflen = cf_opts->bulk_buflen;
	qlen = cf_opts->qlen;
	if (!qlen)
		qlen = 32;

	capture->function.name = "capturefile";
	capture->function.bind = capturefile_bind;
	capture->function.set_alt = capturefile_set_alt;
	capture->function.disable = capturefile_disable;
	capture->function.strings = capturefile_strings;

	capture->function.free_func = cf_free_func;

	return &capture->function;
}
static void cf_free_instance(struct usb_function_instance *fi)
{
	struct f_cf_opts *cf_opts;

	cf_opts = container_of(fi, struct f_cf_opts, func_inst);
	kfree(cf_opts);
}
static struct usb_function_instance *capturefile_alloc_instance(void)
{
	struct f_cf_opts *cf_opts;

	cf_opts = kzalloc(sizeof(*cf_opts), GFP_KERNEL);
	if (!cf_opts)
		return ERR_PTR(-ENOMEM);
	cf_opts->func_inst.free_func_inst = cf_free_instance;
	return  &cf_opts->func_inst;
}
DECLARE_USB_FUNCTION(CaptureFile, capturefile_alloc_instance, capturefile_alloc);


int __init cf_modinit(void)
{
	int ret;

	ret = usb_function_register(&CaptureFileusb_func);
	if (ret)
		return ret;
	return ret;
}
void __exit cf_modexit(void)
{
	usb_function_unregister(&CaptureFileusb_func);
}

MODULE_LICENSE("GPL");
