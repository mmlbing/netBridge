#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include "g_capture.h"
#include "../gadget_chips.h"
#include "vendor_com.h"
#include "packet_list.h"
#include "sdebug.h"

#include <linux/miscdevice.h>
#include <linux/fs.h>		/*fasync_helper*/
#include <asm/signal.h>
#include <asm-generic/siginfo.h>
#include <linux/uaccess.h>	/*copy_to_user*/

#include <linux/delay.h>


/**/
int cap_misc_dev_count = 0;
static struct fasync_struct *cap_dev_fasync_struct;

struct pak_queue_state pak_queue_state_struct;
static PAK_LIST_HEAD(packets_list);
u16 capture_cmds;

u16 pak_bulk_in_count = 0;		/*本次待传输pak数*/

/**/
struct f_capturereal {
	struct usb_function	function;
	struct usb_ep		*bulk_in_ep;
	struct usb_ep		*bulk_out_ep;
	int					cur_alt;/*不知道是什么*/
};

struct f_capturereal *cr;

/**/
/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor capture_real_intf_alt0 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_capture_real_descs[] = {
	(struct usb_descriptor_header *) &capture_real_intf_alt0,
	(struct usb_descriptor_header *) &fs_bulk_out_desc,
	(struct usb_descriptor_header *) &fs_bulk_in_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(15),
};

static struct usb_endpoint_descriptor hs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(15),
};

static struct usb_descriptor_header *hs_capture_real_descs[] = {
	(struct usb_descriptor_header *) &capture_real_intf_alt0,
	(struct usb_descriptor_header *) &hs_bulk_in_desc,
	(struct usb_descriptor_header *) &hs_bulk_out_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

struct usb_ss_ep_comp_descriptor ss_bulk_in_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

struct usb_ss_ep_comp_descriptor ss_bulk_out_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_descriptor_header *ss_capture_real_descs[] = {
	(struct usb_descriptor_header *) &capture_real_intf_alt0,
	(struct usb_descriptor_header *) &ss_bulk_in_desc,
	(struct usb_descriptor_header *) &ss_bulk_in_comp_desc,
	(struct usb_descriptor_header *) &ss_bulk_out_desc,
	(struct usb_descriptor_header *) &ss_bulk_out_comp_desc,
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
struct usb_request *alloc_ep_req(struct usb_ep *ep, int len)
{
	struct usb_request      *req;

	//printk("alloc_ep_req---%d\n",len);
	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (req) {
		req->length = len;
		//req->buf = kmalloc(len, GFP_ATOMIC);
		req->buf = packets_list.next->data;
		if (!req->buf) {
			usb_ep_free_request(ep, req);
			req = NULL;
		}
	}
	return req;
}


static void capture_real_complete(struct usb_ep *ep, struct usb_request *req)
{
	int status = req->status;

	S_DEBUG("01-00-00:capture_real_complete IN\n");

	switch (status) {
		case 0:
			pak_list_del(&packets_list);

			if(pak_bulk_in_count) {
				req = alloc_ep_req(ep, pak_queue_state_struct.pak_length[--pak_bulk_in_count]);

				if (!req)
					return;

				//req->buf = &packets_list.next->data;		/*还需要对req->buf进行初始化，我们这里在open函数中把list_head->next->pak_num赋给它*/
				//printk("memcpy:%d\n",packets_list.next->pak_num);
				//memset(req->buf, 0x55, req->length);
				//memcpy(req->buf, &packets_list.next->pak_num, PAK_NODE_SIZE);
				
				req->complete = capture_real_complete;

				status = usb_ep_queue(ep, req, GFP_ATOMIC);
				if (status) {
					S_DEBUG("BULK IN halted\n");
					usb_ep_set_halt(ep);
		 		}
			}
			else {
				//usb_ep_free_request(ep, req);
			}
			break;
		/* this endpoint is normally active while we're configured */
		case -ECONNABORTED:		/* hardware forced ep reset */
		case -ECONNRESET:		/* request dequeued */
		case -ESHUTDOWN:		/* disconnect from host */
			usb_ep_free_request(ep, req);
			break;
		default:
			break;
	}
}

static void disable_ep(struct usb_composite_dev *cdev, struct usb_ep *ep)
{
	int	value;

	S_DEBUG("02-00-00:disable_ep IN\n");

	if (ep->driver_data) {
		value = usb_ep_disable(ep);
		if (value < 0)
			DBG(cdev, "disable %s --> %d\n",
					ep->name, value);
		ep->driver_data = NULL;
	}
}

/*static int capture_real_start_ep(struct f_capturereal *cr, bool is_in, int speed)
{
	struct usb_ep		*ep;
	struct usb_request	*req;
	int	status;

	S_DEBUG("03-00-00:capture_real_start_ep IN\n");
	
	ep = is_in ? cr->bulk_in_ep : cr->bulk_out_ep;
	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	
	if (!req)
		return -ENOMEM;
	
	req->length = PAK_NODE_SIZE;	//传输的数据大小为数据报结构体大小
									//还需要对req->buf进行初始化，我们这里在open函数中把list_head->next->pak_num赋给它
	req->complete = capture_real_complete;

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		struct usb_composite_dev	*cdev;

		cdev = cr->function.config->cdev;
		usb_ep_free_request(ep, req);
	}
	return status;
}*/

static inline struct f_capturereal *func_to_cr(struct usb_function *f)
{
	return container_of(f, struct f_capturereal, function);
}

static void disable_capture_real(void)
{
	struct usb_composite_dev	*cdev = cr->function.config->cdev;

	S_DEBUG("04-00-00:disable_capture_real IN\n");
	
	disable_ep(cdev, cr->bulk_in_ep);
	disable_ep(cdev, cr->bulk_out_ep);
	VDBG(cdev, "%s disabled\n", cr->function.name);
}

static int enable_capture_real(struct usb_composite_dev *cdev, int alt)
{
	int					result = 0;
	//int					speed = cdev->gadget->speed;
	struct usb_ep		*ep;

	S_DEBUG("05-00-00:enable_capture_real IN\n");

	/* one bulk endpoint writes IN (to the host) */
	ep = cr->bulk_in_ep;
	result = config_ep_by_speed(cdev->gadget, &(cr->function), ep);
	if (result)
		return result;
	
	result = usb_ep_enable(ep);
	if (result < 0)
		return result;

	ep->driver_data = cr;

	//result = capture_real_start_ep(cr, true, speed);
	if (result < 0) {
fail:
		ep = cr->bulk_in_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		return result;
	}

	/* one bulk endpoint reads OUT (from the host) */
	ep = cr->bulk_out_ep;
	result = config_ep_by_speed(cdev->gadget, &(cr->function), ep);
	if (result)
		goto fail;
	result = usb_ep_enable(ep);
	if (result < 0)
		goto fail;
	ep->driver_data = cr;

	//result = capture_real_start_ep(cr, false, speed);
	if (result < 0) {
		ep = cr->bulk_out_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		goto fail;
	}
	cr->cur_alt = alt;
	
	S_DEBUG("05-00-00:enable_capture_real OUT\n");
	return result;
}

static int start_transf(void)
{
	struct usb_ep		*ep;
	struct usb_request	*req;
	int status;

	ep = cr->bulk_in_ep;
	req = alloc_ep_req(ep, pak_queue_state_struct.pak_length[--pak_bulk_in_count]);

	if (!req)
		return -ENOMEM;
	
	//req->buf = &packets_list.next->data;		/*还需要对req->buf进行初始化，我们这里在open函数中把list_head->next->pak_num赋给它*/
	//req->buf = x;
	//printk("memcpy:%d\n",packets_list.next->pak_num);
	//memset(req->buf, 0x55, 20);
	//memcpy(req->buf, &packets_list.next->pak_num, PAK_NODE_SIZE);

	
	req->complete = capture_real_complete;

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		usb_ep_free_request(ep, req);
		return -1;
	}
	return 0;
}

void queue_state_prepare(void)
{
	int i;
	struct pak_node *node;
	node = packets_list.prev;
	pak_queue_state_struct.pak_count = pak_bulk_in_count;
	for(i=0;i<pak_bulk_in_count;i++) {
		pak_queue_state_struct.pak_length[i] = node->length;
		node = node->prev;
	}
}


/***********************************END inside func END*****************************************/

static int capturereal_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	int	id, ret;

	S_DEBUG("06-00-00:capturereal_bind IN\n");

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	capture_real_intf_alt0.bInterfaceNumber = id;

	/* allocate bulk endpoints */
	cr->bulk_in_ep = usb_ep_autoconfig(cdev->gadget, &fs_bulk_in_desc);
	if (!cr->bulk_in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
			f->name, cdev->gadget->name);
		//printk("err no dev\n");
		return -ENODEV;
	}
	cr->bulk_in_ep->driver_data = cdev;	/* claim */

	cr->bulk_out_ep = usb_ep_autoconfig(cdev->gadget, &fs_bulk_out_desc);
	if (!cr->bulk_out_ep)
		goto autoconf_fail;
	cr->bulk_out_ep->driver_data = cdev;	/* claim */

	/* support high speed hardware */
	hs_bulk_in_desc.bEndpointAddress = fs_bulk_in_desc.bEndpointAddress;
	hs_bulk_out_desc.bEndpointAddress = fs_bulk_out_desc.bEndpointAddress;

	/* support super speed hardware */
	ss_bulk_in_desc.bEndpointAddress = fs_bulk_in_desc.bEndpointAddress;
	ss_bulk_out_desc.bEndpointAddress = fs_bulk_out_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_capture_real_descs,
			hs_capture_real_descs, ss_capture_real_descs);
	return ret;
}

static int capturereal_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct usb_composite_dev	*cdev = f->config->cdev;

	S_DEBUG("07-00-00:capturereal_set_alt IN\n");
	
	if (cr->bulk_in_ep->driver_data)
		disable_capture_real();
	return enable_capture_real(cdev, alt);
}

static int capturereal_get_alt(struct usb_function *f, unsigned intf)
{
	S_DEBUG("08-00-00:capturereal_get_alt IN\n");

	return cr->cur_alt;
}

static void capturereal_disable(struct usb_function *f)
{
	S_DEBUG("09-00-00:capturereal_disable IN\n");

	disable_capture_real();
}

static int capturereal_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_configuration	*c = f->config;
	struct usb_request			*req = c->cdev->req;
	
	u16	w_value = le16_to_cpu(ctrl->wValue);
	//u16 w_index = le16_to_cpu(ctrl->wIndex);	/*忽略wIndex的值*/
	u16	w_length = le16_to_cpu(ctrl->wLength);
	int	value = -EOPNOTSUPP;

	S_DEBUG("10-00-00:capturereal_setup IN\n");

	req->length = USB_COMP_EP0_BUFSIZ;

	switch (ctrl->bRequest) {
		case SET_REQ_STATE:
			S_DEBUG("10-00-01:SET_REQ_STATE\n");
			if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)) {
				break;
			}
			if (w_length > USB_COMP_EP0_BUFSIZ) {
				break;
			}
			value = w_length;
			switch (w_value) {
				case SET_VAL_GET_QUEUE_STATE:
					S_DEBUG("10-00-02:SET_VAL_GET_QUEUE_STATE\n");
					value = w_length;
					//printk("\npackets_list.length: %d\n", packets_list.length);
					pak_bulk_in_count = packets_list.length;
					queue_state_prepare();
					memcpy(req->buf, &pak_queue_state_struct, GET_QUEUE_STATE_RES_LENGTH);
					break;
				default:
					break;
			}
			break;
		case SET_REQ_READ_START:	/*请求开始传输*/
			if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)) {
				break;
			}
			if (w_length > USB_COMP_EP0_BUFSIZ) {
				break;
			}
			S_DEBUG("10-00-03:SET_REQ_READ_START\n");
			value = w_length;
			start_transf();
			break;
		case SET_REQ_READ_FINISH:		/*上位机发送bulk读结束*/
			if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)) {
				break;
			}
			if (w_length > USB_COMP_EP0_BUFSIZ) {
				break;
			}
			value = w_length;
			//S_DEBUG("10-FIN-00\n");
			break;
		case SET_REQ_COMD:
			if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)) {
				break;
			}
			if (w_length > USB_COMP_EP0_BUFSIZ) {
				break;
			}
			value = w_length;
			capture_cmds &= CMD_CLEAR_MASK;
			switch (w_value) {
				case SET_VAL_RESET:
					capture_cmds |= SET_VAL_RESET;
					break;
				case SET_VAL_START:
					capture_cmds |= SET_VAL_START;
					break;
				case SET_VAL_STOP:
					capture_cmds |= SET_VAL_STOP;
					break;
				case SET_VAL_MODE_CR:
					capture_cmds &= ~CMD_CLEAR_MASK;
					capture_cmds |= SET_VAL_MODE_CR;
					break;
				case SET_VAL_MODE_CF:
					capture_cmds &= ~CMD_CLEAR_MASK;
					capture_cmds |= SET_VAL_MODE_CF;
					break;
				default:
					break;
			}
			cap_dev_send_sig();
			break;

		case SET_REQ_READ_ERR:
			if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)) {
				break;
			}
			if (w_length > USB_COMP_EP0_BUFSIZ) {
				break;
			}
			value = w_length;
			break;

		default:
			break;
	}

	if (value >= 0) { /*用于控制传输的数据阶段*/
		S_DEBUG("10-va-00:capturereal_setup: value  >= 0\n");
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(c->cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			ERROR(c->cdev, "capture/real response, err %d\n", value);
			//printk("10-capture/real response err:%d\n", value);
		}
	}

	return value;
}

static void capturereal_free_func(struct usb_function *f)
{
	S_DEBUG("11-00-00:capturereal_free_func IN\n");

	usb_free_all_descriptors(f);
	S_DEBUG("kfree 03\n");
	kfree(func_to_cr(f));
}

static struct usb_function *capture_real_alloc_func(struct usb_function_instance *fi)
{
	S_DEBUG("12-00-00:capture_real_alloc_func IN\n");

	cr = kzalloc(sizeof(*cr), GFP_KERNEL);
	if (!cr)
		return NULL;

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
	S_DEBUG("13-00-00:capture_real_free_instance IN\n");
	S_DEBUG("kfree 04\n");
	kfree(fi);
}

static struct usb_function_instance *capture_real_alloc_inst(void)
{
	struct usb_function_instance *func_inst;
	
	S_DEBUG("14-00-00:capture_real_alloc_inst IN\n");

	func_inst = kzalloc(sizeof(*func_inst), GFP_KERNEL);
	if (!func_inst)
		return ERR_PTR(-ENOMEM);
	func_inst->free_func_inst = capture_real_free_instance;
	return func_inst;
}

DECLARE_USB_FUNCTION(CaptureReal, capture_real_alloc_inst,
		capture_real_alloc_func);

/*********************************misc dev func***************************************/
void cap_dev_send_sig(void)
{
	S_DEBUG("cap_dev_send_sig IN\n");
	kill_fasync(&cap_dev_fasync_struct, SIGIO, POLL_IN);
}

static int cap_dev_open(struct inode *inodp, struct file *filp)
{
	if(cap_misc_dev_count > 0)
		return -1;
	memset(&pak_queue_state_struct, 0, sizeof(struct pak_queue_state));
	memset(&packets_list, 0, sizeof(struct pak_node));
	PAK_INIT_LIST_HEAD(&packets_list);
	capture_cmds = 0;
	capture_cmds |= MODE_CR;	/*默认功能*/
	cap_misc_dev_count++;
	return 0;
}

static ssize_t cap_dev_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
	if(copy_to_user(buff, &capture_cmds, count)) {
		return -1;
	}
	capture_cmds &= CMD_CLEAR_MASK;		/*读取后清除*/
	return 0;
}

static ssize_t cap_dev_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	//int i,j;/*TEST*/
	int copy_err;
	struct pak_node *node_new;

	S_DEBUG("00-00:cap_dev_write IN\n");
	
	node_new = kzalloc(sizeof(struct pak_node), GFP_ATOMIC);
	if(packets_list.length == PAK_QUEUE_LENGTH) {	/*控制队列长度*/
		pak_list_del(&packets_list);
	}
	node_new->data = kzalloc(count, GFP_ATOMIC);
	copy_err = copy_from_user(node_new->data, buff, count);
	if(copy_err){
		kfree(node_new);
		kfree(node_new->data);
		return copy_err;
	}
	node_new->length = count;
	pak_list_add_tail(node_new, &packets_list);
	printk("kdely1\n");
	mdelay(1000);
	printk("kdely2\n");
	return 0;
}

static int cap_dev_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &cap_dev_fasync_struct);
}

static int cap_dev_release(struct inode *inodp, struct file *filp)
{
	if(cap_misc_dev_count <= 0)
		return -1;
	cap_dev_fasync(-1, filp, 0);
	cap_misc_dev_count--;
	return 0;
}

static struct file_operations cap_dev_fops = {
    .owner = THIS_MODULE,
    .open = cap_dev_open,
    .release = cap_dev_release,
    .write = cap_dev_write,
    .read = cap_dev_read,
    .fasync = cap_dev_fasync,
};

static struct miscdevice cap_misc_dev = 
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "netBridge",
	.fops = &cap_dev_fops,
};

static int __init cap_misc_dev_init(void)
{
	int ret;
	cap_misc_dev_count = 0;
	ret = misc_register(&cap_misc_dev);
	if(ret) {
		return ret;
	}
	return ret;
}
static void __exit cap_misc_dev_exit(void)
{
	misc_deregister(&cap_misc_dev);
}
/*********************************END misc dev func END***************************************/

static int __init crcf_modinit(void)
{
	int ret;

	ret = usb_function_register(&CaptureRealusb_func);
	if (ret)
		return ret;
	ret = cf_modinit();
	if (ret)
		usb_function_unregister(&CaptureRealusb_func);
	ret = cap_misc_dev_init();
	if (ret)
		usb_function_unregister(&CaptureRealusb_func);
	return ret;
}
static void __exit crcf_modexit(void)
{
	cap_misc_dev_exit();
	usb_function_unregister(&CaptureRealusb_func);
	cf_modexit();
}

module_init(crcf_modinit);
module_exit(crcf_modexit);

MODULE_LICENSE("GPL");
