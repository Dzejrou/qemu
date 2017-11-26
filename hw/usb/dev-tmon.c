#include "qemu/osdep.h"
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"

typedef struct USBTMonState
{
	USBDevice dev;
	uint64_t bw_int_in;
	uint64_t bw_int_out;
	uint64_t bw_bulk_in;
	uint64_t bw_bulk_out;
	uint64_t bw_isoc_in;
	uint64_t bw_isoc_out;
} USBTMonState;

static void report(USBTMonState *state)
{
	printf("============\n");
	printf("TMON Report");
	printf("| bw_int_in:   %lu\n", state->bw_int_in);
	printf("| bw_int_out:  %lu\n", state->bw_int_out);
	printf("| bw_bulk_in:  %lu\n", state->bw_bulk_in);
	printf("| bw_bulk_out: %lu\n", state->bw_bulk_out);
	printf("| bw_isoc_in:  %lu\n", state->bw_isoc_in);
	printf("| bw_isoc_out: %lu\n", state->bw_isoc_out);
	printf("============\n");
}

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
	report((USBTMonState *)dev);
}

// TODO: p->actual_length vs p->iov.size
static void usb_tmon_int_in(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;
	state->bw_int_in += p->actual_length;
}

static void usb_tmon_int_out(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;
	state->bw_int_out += p->actual_length;
}

static void usb_tmon_bulk_in(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;
	state->bw_bulk_in += p->actual_length;
}

static void usb_tmon_bulk_out(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;
	state->bw_bulk_out += p->actual_length;
}

static void usb_tmon_isoc_in(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;
	state->bw_isoc_in += p->actual_length;
}

static void usb_tmon_isoc_out(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;
	state->bw_isoc_out += p->actual_length;
}

static void usb_tmon_handle_data(USBDevice *dev, USBPacket *p)
{
	// Note: This is both bulk and isoc.
	printf("HANDLE DATA\n");

	switch (p->pid)
	{
		case USB_TOKEN_IN:
			switch (p->ep->nr)
			{
				case 1:
					usb_tmon_int_in(dev, p);
					break;
				case 3:
					usb_tmon_bulk_in(dev, p);
					break;
				case 5:
					usb_tmon_isoc_in(dev, p);
					break;
			}
			break;
		case USB_TOKEN_OUT:
			switch (p->ep->nr)
			{
				case 2:
					usb_tmon_int_out(dev, p);
					break;
				case 4:
					usb_tmon_bulk_out(dev, p);
					break;
				case 6:
					usb_tmon_isoc_out(dev, p);
					break;
			}
			break;
	}
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

	USBTMonState *state = (USBTMonState *)dev;
	state->bw_int_in = 0;
	state->bw_int_out = 0;
	state->bw_bulk_in = 0;
	state->bw_bulk_out = 0;
	state->bw_isoc_in = 0;
	state->bw_isoc_out = 0;
}

static void usb_tmon_handle_attach(USBDevice *dev)
{
	printf("TMON ATTACHED\n");
}

static const VMStateDescription vmstate_usb_tmon = {
    .name = "usb-tmon",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBTMonState),
        VMSTATE_UINT64(bw_int_in, USBTMonState),
        VMSTATE_UINT64(bw_int_out, USBTMonState),
        VMSTATE_UINT64(bw_bulk_in, USBTMonState),
        VMSTATE_UINT64(bw_bulk_out, USBTMonState),
        VMSTATE_UINT64(bw_isoc_in, USBTMonState),
        VMSTATE_UINT64(bw_isoc_out, USBTMonState),
        VMSTATE_END_OF_LIST()
    }
};

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
	dc->vmsd = &vmstate_usb_tmon;
}

static const TypeInfo usb_tmon_info = {
	.name = "usb-tmon",
	.parent = TYPE_USB_DEVICE,
	.instance_size = sizeof(USBTMonState),
	.class_init = usb_tmon_class_init
};

static void usb_tmon_register_types(void)
{
    type_register_static(&usb_tmon_info);
    usb_legacy_register("usb-tmon", "transfer-monitor", NULL);
}

type_init(usb_tmon_register_types)
