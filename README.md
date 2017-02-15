2.1
	数据结构改为void *类型，节省空间；
	其它改进

	====> 接下来试验3.0版本（MMAP版本）

2.0
	修正自定义控制传输错误；
	程序简化；
	混杂设备；
	异步通知

1.0
	移植Gadget



==》关于Gadget Capture的Makefile和Kconfig
	Kconfig		+source "*"
	Makefile	+
obj-$(CONFIG_*)	+= gadget_capture/