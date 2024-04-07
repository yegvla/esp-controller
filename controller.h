#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "usb/usb_types_stack.h"
#include <cstdint>
#include <usb/usb_host.h>
#include <stdint.h>

/// Actions
typedef enum {
    ACTION_OPEN_DEV = 0x01,
    ACTION_GET_DEV_INFO = 0x02,
    ACTION_GET_DEV_DESC = 0x04,
    ACTION_GET_CONFIG_DESC = 0x08,
    ACTION_CLAIM_CONTROLLER = 0x10,
    ACTION_PREPARE_CONTROLLER = 0x20,
    ACTION_CLOSE_DEV = 0x40,
} controller_drv_act_t;


/// Controller status report
typedef struct {
	uint8_t report_type;
	uint8_t : 8; // reseved
        struct {
		unsigned select : 1;
		unsigned left_js : 1;
		unsigned right_js : 1;
		unsigned start : 1;
		unsigned up : 1;
		unsigned right : 1;
		unsigned down : 1;
		unsigned left : 1;
		unsigned l2 : 1;
		unsigned r2 : 1;
		unsigned l1 : 1;
		unsigned r1 : 1;
		unsigned triangle : 1;
		unsigned circle : 1;
		unsigned cross : 1;
		unsigned square : 1;
	} __attribute__((packed)) buttons;
        bool ps_button;
	uint8_t : 8; // reseved
	struct {
		uint8_t x;
		uint8_t y;
	} __attribute__((packed)) js_left;
	struct {
		uint8_t x;
		uint8_t y;
	} js_right;
	uint32_t : 32; // reseved
	struct {
		struct {
			uint8_t up;
			uint8_t right;
			uint8_t down;
			uint8_t left;
		} __attribute__((packed)) dpad;
		struct {
			uint8_t r1;
			uint8_t r2;
			uint8_t l1;
			uint8_t l2;
		} __attribute__((packed)) rear;
		struct {
			uint8_t triangle;
			uint8_t circle;
			uint8_t cross;
			uint8_t square;
		} __attribute__((packed)) symbols;
	} __attribute__((packed)) pressure;
	// TOOD: there is more stuff sent, but this is the most important.
} __attribute__((packed)) controller_report_t;

/// Controller driver instance
typedef struct {
	/// The handle for the client
	usb_host_client_handle_t client_hdl;
	/// The usb address of the device, 0 if not available
	uint8_t dev_addr;
	/// The claimed interface index
	uint8_t interface;
	/// The address of the INT endpoint for recieving data.
	uint8_t ep_addr;
	/// The handle for the device
	usb_device_handle_t dev_hdl;
	/// USB transfer object
	usb_transfer_t *transfer;
	/// The maximum packet size, used for transfer
	uint8_t max_packet_size;
	/// What to do... internal
	uint32_t actions;
	/// Set if the controller is ready to be operated.
	bool ready;
	/// Pointer to the buffer of the transfer struct.
	controller_report_t *report;
} controller_drv_t;

void controller_init(controller_drv_t *driver_obj);

void controller_task(controller_drv_t *driver_obj);

#endif
