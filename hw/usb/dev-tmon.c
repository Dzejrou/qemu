#include "qemu/osdep.h"
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"

#include <sys/time.h>

#define DIAGNOSTIC_DEVICE_CLASS 0xDC

#define INT_PACKET_SIZE 512
#define BULK_PACKET_SIZE 512
#define ISOC_PACKET_SIZE 512

typedef enum
{
	TRANSFER_CONTROL,
	TRANSFER_INT,
	TRANSFER_BULK,
	TRANSFER_ISOC,
	TRANSFER_NONE
} ttype_t;

typedef struct USBTMonState
{
	USBDevice dev;

	uint64_t data_in;
	uint64_t data_out;
	long time_in;
	long time_out;

	ttype_t transfer_type;
} USBTMonState;

static long get_now_sec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return (long)tv.tv_sec;
}

static long get_now_usec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return (long)tv.tv_sec * 1000 + (long)tv.tv_usec;
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

static void usb_tmon_repl(USBPacket *p, uint8_t data, uint32_t count)
{
	uint8_t *buf = (uint8_t *)malloc(count);
	for (uint32_t i = 0; i < count; ++i)
		buf[i] = data;
	usb_packet_copy(p, (void *)buf, count);
	free(buf);
}

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)

static uint8_t descriptor[] = {
	0x12,                    /* bLength */
	0x01,                    /* bDescriptorType */
	U16(0x0300),             /* bcdUSB */
	DIAGNOSTIC_DEVICE_CLASS, /* bDeviceClass */
	0x00,                    /* bDeviceSubClass */
	0x00,                    /* bDeviceProtocol */
	0x09,                    /* bMaxPacketSize */
	U16(0x1337),             /* idVendor */
	U16(0x1337),             /* idProduct */
	U16(0x1337),             /* bcdDevice*/
	0x01,                    /* iManufacturer */
	0x02,                    /* iProduct */
	0x03,                    /* iSerialNumber */
	0x01                     /* bNumConfigurations */
};

static void usb_tmon_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
	printf("length = %d | index = %d | value = %d | request = %d\n", length, index, value, request);
	int rc = usb_desc_handle_control(dev, p, request, value, index, length, data);

	if (rc >= 0)
		return;
	return;

	if (request == 32774 && value == 256 && length == 18)
	{
		printf("Got descriptor request!\n");
		memcpy(data, descriptor, sizeof(descriptor));
		p->actual_length = sizeof(descriptor);
	}
	else
		usb_desc_handle_control(dev, p, request, value, index, length, data);
}

static void usb_tmon_int_out(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;

	if (state->time_out == 0 || state->transfer_type != TRANSFER_INT)
	{
		state->data_out = 0;
		state->time_out = get_now_usec();
		state->transfer_type = TRANSFER_INT;
	}
	else
	{
		long now = get_now_usec();
		printf("[INT][OUT] Packet received after %ld usecs.", (now - state->time_out));
		state->time_out = now;
	}
}

static void usb_tmon_int_in(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;

	if (state->time_in == 0 || state->transfer_type != TRANSFER_INT)
	{
		state->data_in = 0;
		state->time_in = get_now_usec();
		state->transfer_type = TRANSFER_INT;
	}
	else
	{
		long now = get_now_usec();
		printf("[INT][IN] Packet received after %ld usecs.\n", (now - state->time_in));
		state->time_in = now;
	}
}

static void usb_tmon_bulk_out(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;

	if (state->time_out == 0 || state->transfer_type != TRANSFER_BULK)
	{
		state->data_out = 0;
		state->time_out = get_now_sec();
		state->transfer_type = TRANSFER_BULK;
	}
	else if (state->time_out != get_now_sec())
	{
		printf("[BULK][OUT] Transferred %ld bytes out the last second.\n", state->data_out);
		state->data_out = 0;
		state->time_out = get_now_sec();
	}
}

static void usb_tmon_bulk_in(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;

	if (state->time_in == 0 || state->transfer_type != TRANSFER_BULK)
	{
		state->data_in = 0;
		state->time_in = get_now_sec();
		state->transfer_type = TRANSFER_BULK;
	}
	else if (state->time_in != get_now_sec())
	{
		printf("[BULK][IN] Transferred %ld bytes in the last second.\n", state->data_in);
		state->data_in = 0;
		state->time_in = get_now_sec();
	}
}

static void usb_tmon_isoc_out(USBDevice *dev, USBPacket *p)
{
	/* USBTMonState* state = (USBTMonState *)dev; */
}

static void usb_tmon_isoc_in(USBDevice *dev, USBPacket *p)
{
	/* USBTMonState* state = (USBTMonState *)dev; */
}

static void usb_tmon_handle_data(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;

	switch (p->pid)
	{
		case USB_TOKEN_OUT:
		{
			/* uint8_t data = 0; */
			/* uint32_t count = 1; */

			switch (p->ep->nr)
			{
				case 2:
					usb_tmon_int_out(dev, p);
					/* data = 1; */
					/* count = INT_PACKET_SIZE; */
					break;
				case 4:
					usb_tmon_bulk_out(dev, p);
					/* data = 2; */
					/* count = BULK_PACKET_SIZE; */
					break;
				case 6:
					usb_tmon_isoc_out(dev, p);
					/* data = 3; */
					/* count = ISOC_PACKET_SIZE; */
					break;
			}
			state->data_out += p->iov.size;

			/* for (size_t i = 0; i < count; ++i) */
			/* { */
			/* 	if ( != data) */
			/* 		printf("Oops, something went wrong :)\n"); */
			/* } */
			break;
		}
		case USB_TOKEN_IN:
		{
			uint8_t data = 0;
			uint32_t count = 0;

			switch (p->ep->nr)
			{
				case 1:
					usb_tmon_int_in(dev, p);
					data = 1;
					count = INT_PACKET_SIZE;
					break;
				case 3:
					usb_tmon_bulk_in(dev, p);
					data = 2;
					count = BULK_PACKET_SIZE;
					break;
				case 5:
					usb_tmon_isoc_in(dev, p);
					data = 3;
					count = ISOC_PACKET_SIZE;
					break;
			}

			usb_tmon_repl(p, data, count);
			p->actual_length = count;

			state->data_in += count;
			break;
		}
	}
}

static void usb_tmon_handle_reset(USBDevice *dev)
{
	// TODO: Reset?
}

static const USBDescIface desc_iface_tmon = {
	.bInterfaceNumber = 0,
	.bNumEndpoints = 6,
	.bInterfaceClass = DIAGNOSTIC_DEVICE_CLASS,
	.bInterfaceSubClass = USB_SUBCLASS_UNDEFINED,
	.bInterfaceProtocol = 0x01,
	.eps = (USBDescEndpoint[]) {
		{
			.bEndpointAddress = USB_DIR_IN | 0x01,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = INT_PACKET_SIZE,
			.bInterval = 7,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | 0x02,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = INT_PACKET_SIZE,
			.bInterval = 7,
		},
		{
			.bEndpointAddress = USB_DIR_IN | 0x03,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = BULK_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | 0x04,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = BULK_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_IN | 0x05,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = ISOC_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | 0x06,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = ISOC_PACKET_SIZE,
		},
	},
};

/* static uint8_t bla[] = {*/
/* 	0x12,                     bLength */
/* 	0x01,                     bDescriptorType */
/* 	U16(0x0300),              bcdUSB */
/* 	DIAGNOSTIC_DEVICE_CLASS,  bDeviceClass */
/* 	0x00,                     bDeviceSubClass */
/* 	0x00,                     bDeviceProtocol */
/* 	0x09,                     bMaxPacketSize */
/* 	U16(0x1337),              idVendor */
/* 	U16(0x1337),              idProduct */
/* 	U16(0x1337),              bcdDevice*/
/* 	0x01,                     iManufacturer */
/* 	0x02,                     iProduct */
/* 	0x03,                     iSerialNumber */
/* 	0x01                      bNumConfigurations */
/* };*/

static const USBDescDevice desc_device_tmon = {
    .bcdUSB                        = 0x0300,
    .bMaxPacketSize0               = 9,
    .bDeviceClass                  = DIAGNOSTIC_DEVICE_CLASS,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_TMON,
            .bmAttributes          = USB_CFG_ATT_ONE,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_tmon,
        },
    },
};

static const USBDesc desc_tmon = {
    .id = {
        .idVendor          = 0x1337,
        .idProduct         = 0x1337,
        .bcdDevice         = 0x1337,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_TMON,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .super = &desc_device_tmon,
    .full = &desc_device_tmon,
    .high = &desc_device_tmon,
    .str = desc_strings,
};

static void usb_tmon_realize(USBDevice *dev, Error **errp)
{
	printf("TMON REALIZING!\n");
	dev->usb_desc = &desc_tmon;
	dev->device = desc_tmon.super;
	dev->speedmask = USB_SPEED_MASK_SUPER
		| USB_SPEED_MASK_FULL
		| USB_SPEED_MASK_HIGH;

	USBTMonState *state = (USBTMonState *)dev;
	state->data_in = 0;
	state->data_out = 0;
	state->time_in = 0;
	state->time_out = 0;
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
        VMSTATE_UINT64(data_in, USBTMonState),
        VMSTATE_UINT64(data_out, USBTMonState),
        VMSTATE_INT64(time_in, USBTMonState),
        VMSTATE_INT64(time_out, USBTMonState),
        VMSTATE_END_OF_LIST()
    }
};

static void usb_tmon_class_init(ObjectClass *klass, void *data)
{
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
