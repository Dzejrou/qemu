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

#define INT_PACKET_SIZE 8
#define BULK_PACKET_SIZE 1024
#define ISOC_PACKET_SIZE 64
#define BUFFER_SIZE 512

#define CHECK 0xDEADBEEF
static uint8_t buffer[BUFFER_SIZE];
static uint8_t check_buffer[BUFFER_SIZE];

#define INT_INTERVAL 7
#define ISOC_INTERVAL 7

#define EP_INT_IN   0x1
#define EP_INT_OUT  0x1
#define EP_BULK_IN  0x2
#define EP_BULK_OUT 0x2
#define EP_ISOC_IN  0x3
#define EP_ISOC_OUT 0x3

#define CHECK_EP_INT_IN   0x5
#define CHECK_EP_INT_OUT  0x5
#define CHECK_EP_BULK_IN  0x6
#define CHECK_EP_BULK_OUT 0x6
#define CHECK_EP_ISOC_IN  0x7
#define CHECK_EP_ISOC_OUT 0x7

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

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)

static void usb_tmon_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
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
		printf("[INT][OUT] Packet received after %ld usecs.\n", (now - state->time_out));
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
	printf("ISOC OUT\n");
}

static void usb_tmon_isoc_in(USBDevice *dev, USBPacket *p)
{
	/* USBTMonState* state = (USBTMonState *)dev; */
	printf("ISOC IN\n");
}

static void usb_tmon_handle_data(USBDevice *dev, USBPacket *p)
{
	USBTMonState* state = (USBTMonState *)dev;

	switch (p->pid)
	{
		case USB_TOKEN_OUT:
		{
			switch (p->ep->nr)
			{
				case EP_INT_OUT:
				case CHECK_EP_INT_OUT:
					usb_tmon_int_out(dev, p);
					break;
				case EP_BULK_OUT:
				case CHECK_EP_BULK_OUT:
					usb_tmon_bulk_out(dev, p);
					break;
				case EP_ISOC_OUT:
				case CHECK_EP_ISOC_OUT:
					usb_tmon_isoc_out(dev, p);
					break;
				default:
					printf("Invalid endpoint number %d\n", (int)p->ep->nr);
					return;
			}
			state->data_out += p->iov.size;

			if (p->ep->nr == 0x8 || p->ep->nr == 0xA || p->ep->nr == 0xC)
			{
				for (size_t i = 0; i < p->iov.niov; ++i)
				{
					uint32_t *buf = (uint32_t *)p->iov.iov[i].iov_base;
					for (size_t j = 0; j < p->iov.iov[i].iov_len / sizeof(CHECK); ++j)
					{
						if (buf[j] != CHECK)
							printf("Oops, something went wrong :) [0x%X != 0xDEADBEEF]\n", buf[j]);
					}
				}
			}
			break;
		}
		case USB_TOKEN_IN:
		{
			uint32_t count = 0;

			switch (p->ep->nr)
			{
				case EP_INT_IN:
				case CHECK_EP_INT_IN:
					usb_tmon_int_in(dev, p);
					count = INT_PACKET_SIZE;
					break;
				case EP_BULK_IN:
				case CHECK_EP_BULK_IN:
					usb_tmon_bulk_in(dev, p);
					count = BULK_PACKET_SIZE;
					break;
				case EP_ISOC_IN:
				case CHECK_EP_ISOC_IN:
					usb_tmon_isoc_in(dev, p);
					count = ISOC_PACKET_SIZE;
					break;
				default:
					printf("Invalid endpoint number %d\n", (int)p->ep->nr);
					return;
			}

			if (p->ep->nr == 0x7 || p->ep->nr == 0x9 || p->ep->nr == 0xB)
				usb_packet_copy(p, (void *)buffer, count);
			else
				p->actual_length += count;
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
	.bNumEndpoints = 12,
	.bInterfaceClass = DIAGNOSTIC_DEVICE_CLASS,
	.bInterfaceSubClass = USB_SUBCLASS_UNDEFINED,
	.bInterfaceProtocol = 0x01,
	.eps = (USBDescEndpoint[]) {
		{
			.bEndpointAddress = USB_DIR_IN | EP_INT_IN,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = INT_PACKET_SIZE,
			.bInterval = INT_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | EP_INT_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = INT_PACKET_SIZE,
			.bInterval = INT_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_IN | EP_BULK_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = BULK_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | EP_BULK_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = BULK_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_IN | EP_ISOC_IN,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = ISOC_PACKET_SIZE,
			.bInterval = ISOC_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | EP_ISOC_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = ISOC_PACKET_SIZE,
			.bInterval = ISOC_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_IN | CHECK_EP_INT_IN,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = INT_PACKET_SIZE,
			.bInterval = INT_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | CHECK_EP_INT_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.wMaxPacketSize = INT_PACKET_SIZE,
			.bInterval = INT_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_IN | CHECK_EP_BULK_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = BULK_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | CHECK_EP_BULK_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = BULK_PACKET_SIZE,
		},
		{
			.bEndpointAddress = USB_DIR_IN | CHECK_EP_ISOC_IN,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = ISOC_PACKET_SIZE,
			.bInterval = ISOC_INTERVAL,
		},
		{
			.bEndpointAddress = USB_DIR_OUT | CHECK_EP_ISOC_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_ISOC,
			.wMaxPacketSize = ISOC_PACKET_SIZE,
			.bInterval = ISOC_INTERVAL,
		},
	},
};

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
	printf("USB Transfer Monitor attached.\n");
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

	memset(buffer, CHECK, BUFFER_SIZE / sizeof(CHECK));
	memset(check_buffer, 42, BUFFER_SIZE);
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
