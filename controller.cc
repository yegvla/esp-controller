#include <cassert>
#include <cstring>
#include "controller.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "usb/usb_types_stack.h"
#include "esp_log.h"

static const char *TAG = "CONTROLLER";
static const TickType_t EVENT_TIMEOUT = 1;

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
	controller_drv_t *driver_obj = (controller_drv_t *)arg;
	switch (event_msg->event) {
	case USB_HOST_CLIENT_EVENT_NEW_DEV:
		ESP_LOGI(TAG, "New Device was discovered.");
		if (driver_obj->dev_addr == 0) {
			driver_obj->dev_addr = event_msg->new_dev.address;
			driver_obj->actions |= ACTION_OPEN_DEV;
		}
		break;
	case USB_HOST_CLIENT_EVENT_DEV_GONE:
		ESP_LOGI(TAG, "Device has gone.");
		if (driver_obj->dev_hdl != nullptr) {
			driver_obj->actions = ACTION_CLOSE_DEV;
		}
		break;
	default:
		ESP_LOGE(TAG, "Client event callback reported inexistant event...");
	}
}

static void controller_recv_cb(usb_transfer_t *transfer) {
	ESP_LOGV(TAG, "transfered: %i bytes", transfer->actual_num_bytes);
	if(transfer->context == nullptr) {
		ESP_LOGI(TAG, "Sent control msg to controller.");
	}
	// else {
	// 	// just for testing
	// 	controller_drv_t *driver_obj = (controller_drv_t *)transfer->context;
	// 	ESP_LOGI(TAG, "JS right: x: %i", driver_obj->report->js_right.x);
	// 	ESP_LOGI(TAG, "          y: %i", driver_obj->report->js_right.y);
	// }
}

static void action_open_dev(controller_drv_t *driver_obj) {
	assert(driver_obj->dev_addr != 0);
	ESP_LOGI(TAG, "Opening device at address %d", driver_obj->dev_addr);
	ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
	driver_obj->actions &= ~ACTION_OPEN_DEV;
	driver_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(controller_drv_t *driver_obj) {
	assert(driver_obj->dev_hdl != nullptr);
	ESP_LOGI(TAG, "Getting device information...");
	usb_device_info_t dev_info;
	ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
	ESP_LOGI(TAG, "New %s-Speed device connected.", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
	driver_obj->actions &= ~ACTION_GET_DEV_INFO;
	driver_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(controller_drv_t *driver_obj) {
	assert(driver_obj->dev_hdl != nullptr);
	ESP_LOGI(TAG, "Getting device descriptor");
	const usb_device_desc_t *dev_desc;
	ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
	if(esp_log_level_get(TAG) == ESP_LOG_VERBOSE)
		usb_print_device_descriptor(dev_desc);
	driver_obj->actions &= ~ACTION_GET_DEV_DESC;
	driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(controller_drv_t *driver_obj) {
	assert(driver_obj->dev_hdl != nullptr);
	ESP_LOGI(TAG, "Getting config descriptor");
	const usb_config_desc_t *config_desc;
	ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
	if(esp_log_level_get(TAG) == ESP_LOG_VERBOSE)
		usb_print_config_descriptor(config_desc, nullptr);
	driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
	driver_obj->actions |= ACTION_CLAIM_CONTROLLER;
}

static void action_claim_controller(controller_drv_t *driver_obj) {
	assert(driver_obj->dev_hdl != nullptr);
	ESP_LOGI(TAG, "Claiming controller...");
	const usb_config_desc_t *config_desc;
	ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
	int offset = 0;
	uint16_t wTotalLength = config_desc->wTotalLength;
	const usb_standard_desc_t *next_desc = (const usb_standard_desc_t *)config_desc;
	do {
		const usb_intf_desc_t *intf = (const usb_intf_desc_t*)next_desc;
		const usb_ep_desc_t *ep = (const usb_ep_desc_t*)next_desc;
		
		switch (next_desc->bDescriptorType) {
		case USB_B_DESCRIPTOR_TYPE_INTERFACE: {
			// Hid interface, might be the controller interface.
			if(intf->bInterfaceClass == USB_CLASS_HID) {
				ESP_LOGI(TAG, "Claiming HID interface #%i... ", intf->bInterfaceNumber);
				ESP_ERROR_CHECK(usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl, intf->bInterfaceNumber, intf->bAlternateSetting));
				driver_obj->interface = intf->bInterfaceNumber;
			}
			break;
		}
		case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
			// If device is INT device and IN then it might be a controller...
			if(ep->bEndpointAddress & 0x80 && ep->bmAttributes == 0x3) {
				ESP_LOGI(TAG, "Found possible controller at endpoint 0x%x.", ep->bEndpointAddress);
				driver_obj->ep_addr = ep->bEndpointAddress;
				driver_obj->max_packet_size = ep->wMaxPacketSize;
				break;
			}
			break;
		}
		next_desc = usb_parse_next_descriptor(next_desc, wTotalLength, &offset);
	} while(next_desc != nullptr);

	driver_obj->actions &= ~ACTION_CLAIM_CONTROLLER;
	driver_obj->actions |= ACTION_PREPARE_CONTROLLER;
}

static void action_prepare_controller(controller_drv_t *driver_obj) {
	assert(driver_obj->dev_addr != 0);
	assert(driver_obj->ep_addr != 0);
	ESP_LOGI(TAG, "Preparing controller...");
	ESP_ERROR_CHECK(usb_host_transfer_alloc(driver_obj->max_packet_size, 0, &driver_obj->transfer));
	// Send some stuff to make it work.
	// Adapted from Linux, relevant ressources:
	// - https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=4a1a4d8b87389e35c3af04c0d0a95f6a0391b964
	// - https://github.com/torvalds/linux/blob/bee0e7762ad2c6025b9f5245c040fcc36ef2bde8/drivers/usb/core/message.c#L137
	driver_obj->transfer->device_handle = driver_obj->dev_hdl;
	// Control transfers must be targeted at EP 0.
	driver_obj->transfer->bEndpointAddress = 0;
	driver_obj->transfer->context = nullptr;
	driver_obj->transfer->callback = controller_recv_cb;
	// Needs to be same as wLength + the underlying packet.
	driver_obj->transfer->num_bytes = 17 + sizeof(usb_setup_packet_t);
	usb_setup_packet_t *req = (usb_setup_packet_t*)driver_obj->transfer->data_buffer;
	req->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
	req->bRequest = 0x01; // HID_REQ_GET_REPORT constant from Linux.
	req->wValue = (3 << 8) | 0xf2; // See Linux commit in 1st URL.
	req->wIndex = driver_obj->interface;
	req->wLength = 17;
	ESP_ERROR_CHECK(usb_host_transfer_submit_control(driver_obj->client_hdl, driver_obj->transfer));
	memset(driver_obj->transfer->data_buffer, 0x00, driver_obj->max_packet_size);
	driver_obj->transfer->bEndpointAddress = driver_obj->ep_addr;
	driver_obj->transfer->num_bytes = driver_obj->max_packet_size;
	driver_obj->transfer->context = driver_obj; // We don't really need it.

	// The controller is now ready
	driver_obj->report = (controller_report_t *)driver_obj->transfer->data_buffer;
	driver_obj->ready = true;
	driver_obj->actions &= ~ACTION_PREPARE_CONTROLLER;
}


static void action_close_dev(controller_drv_t *driver_obj) {
	if(driver_obj->transfer != nullptr) {
		ESP_ERROR_CHECK(usb_host_transfer_free(driver_obj->transfer));
		driver_obj->transfer = nullptr;
	}
	// TODO: The interface gets claimed *before* ready is set,
	// this can lead to a race-condition, resetting the processor.
	if(driver_obj->ready) {
		ESP_ERROR_CHECK(usb_host_interface_release(driver_obj->client_hdl, driver_obj->dev_hdl, driver_obj->interface));
	}
	ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
	driver_obj->dev_hdl = nullptr;
	driver_obj->dev_addr = 0;
	driver_obj->ep_addr = 0;
	driver_obj->ready = false;
	driver_obj->actions &= ~ACTION_CLOSE_DEV;
}

void controller_init(controller_drv_t *driver_obj) {
	const usb_host_config_t config = {
		.skip_phy_setup = false,
		.intr_flags = ESP_INTR_FLAG_LEVEL1,
	};

        ESP_ERROR_CHECK(usb_host_install(&config));

	const usb_host_client_config_t client_config = {
		.is_synchronous = false,
		.max_num_event_msg = 5,
		.async = {
			.client_event_callback = client_event_cb,
			.callback_arg = (void*) driver_obj
		}
	};

        ESP_ERROR_CHECK(usb_host_client_register(&client_config, &(driver_obj->client_hdl)));
}

void controller_task(controller_drv_t *driver_obj) {
	if (driver_obj->ready) {
		driver_obj->transfer->num_bytes = driver_obj->max_packet_size;
		esp_err_t err = usb_host_transfer_submit(driver_obj->transfer);
	}
	uint32_t event_flags;
	esp_err_t err = usb_host_lib_handle_events(EVENT_TIMEOUT, &event_flags);
	if(err != ESP_ERR_TIMEOUT) ESP_ERROR_CHECK_WITHOUT_ABORT(err);
	if (driver_obj->actions == 0) {
		usb_host_client_handle_events(driver_obj->client_hdl, EVENT_TIMEOUT);
        } else {
		if (driver_obj->actions & ACTION_OPEN_DEV) {
			action_open_dev(driver_obj);
		}
		if (driver_obj->actions & ACTION_GET_DEV_INFO) {
			action_get_info(driver_obj);
		}
		if (driver_obj->actions & ACTION_GET_DEV_DESC) {
			action_get_dev_desc(driver_obj);
		}
		if (driver_obj->actions & ACTION_GET_CONFIG_DESC) {
			action_get_config_desc(driver_obj);
		}
		if (driver_obj->actions & ACTION_CLAIM_CONTROLLER) {
			action_claim_controller(driver_obj);
		}
		if (driver_obj->actions & ACTION_PREPARE_CONTROLLER) {
			action_prepare_controller(driver_obj);
		}
		if (driver_obj->actions & ACTION_CLOSE_DEV) {
			action_close_dev(driver_obj);
		}
        }
}
