#include "qemu/osdep.h"
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"

enum {
	STR_MANUFACTURER = 1,
	STR_PRODUCT_TMON,
	STR_SERIALNUMBER,
	STR_CONFIG_TMON,
};

static const USBDescStrings desc_strings = {
	[STR_MANUFACTURER] = "QEMU",
	[STR_PRODUCT_TMON] = "QEMU USB Transfer Monitor",
	[STR_SERIALNUMBER] = "1337",
	[STR_CONFIG_TMON]  = "Transfer Monitor"
};

static void usb_tmon_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
	printf("HANDLE_CONTROL\n");
}

static void usb_tmon_handle_data(USBDevice *dev, USBPacket *p)
{
	// Note: This is both bulk and isoc.
	printf("HANDLE DATA\n");
}

static void usb_tmon_handle_reset(USBDevice *dev)
{
	printf("HANDLE RESET\n");
}

static const USBDescIface desc_iface_tmon = {
	.bInterfaceNumber = 0,
	.bNumEndpoints = 6,
	.bInterfaceClass = USB_CLASS_APP_SPEC,
	.bInterfaceSubClass = USB_SUBCLASS_UNDEFINED,
	.bInterfaceProtocol = 0x01,
	.ndesc = 1,
	.descs = (USBDescOther[]) {
		{ // NO FUCKING IDEA...
			.data = (uint8_t[]) {
				0x03,
				USB_DT_DEVICE,
				USB_CLASS_APP_SPEC,
			},
		},
	},
	.eps = (USBDescEndpoint[]) {
		{
			.bEndpointAddress = USB_DIR_IN | 0x01,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = 4,
			.bInterval = 7,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | 0x02,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = 4,
			.bInterval = 7,
		},
		{
			.bEndpointAddress = USB_DIR_IN | 0x03,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = 64,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | 0x04,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = 64,
		},
		{
			.bEndpointAddress = USB_DIR_IN | 0x05,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = 4,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | 0x06,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = 4,
		},
	},
};

static const USBDescDevice desc_device_tmon = {
    .bcdUSB                        = 0x0100,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_TMON,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_tmon,
        },
    },
};

static const USBDescMSOS desc_msos_suspend = {
    .SelectiveSuspendEnabled = true,
};

static const USBDesc desc_tmon = {
    .id = {
        .idVendor          = 0x1337,
        .idProduct         = 0x0042,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_TMON,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_tmon,
    .str = desc_strings,
    .msos = &desc_msos_suspend,
};

static void usb_tmon_realize(USBDevice *dev, Error **errp)
{
	printf("TMON REALIZING!\n");
	dev->usb_desc = &desc_tmon;
	dev->speedmask = USB_SPEED_MASK_SUPER
		| USB_SPEED_MASK_FULL
		| USB_SPEED_MASK_HIGH;
}

static void usb_tmon_handle_attach(USBDevice *dev)
{
	printf("TMON ATTACHED\n");
}

static void usb_tmon_class_init(ObjectClass *klass, void *data)
{
	printf("TMON IS HERE, BABY!\n");
	DeviceClass *dc = DEVICE_CLASS(klass);
	USBDeviceClass *uc = USB_DEVICE_CLASS(klass);
	(void)dc;


	uc->realize = usb_tmon_realize;
	uc->product_desc = "QEMU USB Transfer Monitor";
	uc->handle_attach = usb_tmon_handle_attach;
	uc->handle_control = usb_tmon_handle_control;
	uc->handle_data = usb_tmon_handle_data;
	uc->handle_reset = usb_tmon_handle_reset;

	set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo usb_tmon_info = {
	.name = "usb-tmon",
	.parent = TYPE_USB_DEVICE,
	.class_init = usb_tmon_class_init
};

static void usb_tmon_register_types(void)
{
    type_register_static(&usb_tmon_info);
    usb_legacy_register("usb-tmon", "transfer-monitor", NULL);
}

type_init(usb_tmon_register_types)
