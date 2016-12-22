


struct usb_capture_options {
	unsigned pattern;
	unsigned isoc_interval;
	unsigned isoc_maxpacket;
	unsigned isoc_mult;
	unsigned isoc_maxburst;
	unsigned bulk_buflen;
	unsigned qlen;
};



struct f_cr_opts {
	struct usb_function_instance func_inst;
	unsigned pattern;
	unsigned isoc_interval;
	unsigned isoc_maxpacket;
	unsigned isoc_mult;
	unsigned isoc_maxburst;
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
void disable_endpoints(struct usb_composite_dev *cdev,
		struct usb_ep *in, struct usb_ep *out,
		struct usb_ep *iso_in, struct usb_ep *iso_out);


