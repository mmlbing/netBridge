
struct usb_capture_options {
	unsigned pattern;
	unsigned bulk_buflen;
	unsigned qlen;
};

struct f_cr_opts {
	struct usb_function_instance func_inst;
	unsigned pattern;
	unsigned bulk_buflen;
};

struct f_cf_opts {
	struct usb_function_instance func_inst;
	unsigned bulk_buflen;
	unsigned qlen;
};

void cf_modexit(void);
int cf_modinit(void);

/* common utilities */
struct usb_request *alloc_ep_req(struct usb_ep *ep, int len);
void free_ep_req(struct usb_ep *ep, struct usb_request *req);
void disable_endpoints(struct usb_composite_dev *cdev, struct usb_ep *in, struct usb_ep *out);
void cap_dev_send_sig(void);
