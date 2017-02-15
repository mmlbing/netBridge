
struct usb_capture_options {
	unsigned bulk_buflen;
	unsigned qlen;
};

struct f_cf_opts {
	struct usb_function_instance func_inst;
	unsigned bulk_buflen;
	unsigned qlen;
};

int cf_modinit(void);
void cf_modexit(void);

/* common utilities */
void cap_dev_send_sig(void);
