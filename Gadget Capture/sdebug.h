
#define SDEBUG

#ifdef SDEBUG
	#define	S_DEBUG(x)	printk(x)
#else
	#define	S_DEBUG(x)	do {} while(0)
#endif
