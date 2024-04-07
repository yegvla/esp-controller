#include <Arduino.h>
#include <elapsedMillis.h>
#include <esp_err.h>
#include "controller.h"
#include "esp_log.h"

/// If set the program will not start until a controller is connected.
#define WAIT_FOR_CONTROLLER 1

#define CONTROLLER_TASK_INTERVAL (100)
#define CONTROLLER_EVAL_INTERVAL (100)

elapsedMillis s_controller_task;
elapsedMillis s_eval_task;

/// Controller driver struct
controller_drv_t controller_drv = {};

void setup(void) {
	Serial.begin(115200);
	esp_log_level_set("*", ESP_LOG_INFO);
	delay(3000);
	ESP_LOGI("SETUP", "Initializing controller...");
	controller_init(&controller_drv);
#	if WAIT_FOR_CONTROLLER
	ESP_LOGI("SETUP", "Waiting for controller to connect...");
	// Block until ready.
	while(!controller_drv.ready) {
		// The controller task needs to be executed,
		// or else the connection will never establish.
		controller_task(&controller_drv);
		// Delay with the timer interval, as we are only
		// waiting for the controller to become available.
		delay(CONTROLLER_TASK_INTERVAL);
	}
#	endif
	ESP_LOGI("SETUP", "Completed.");
}

void loop(void) {
	// Run the controller task at a set interval.
	if(s_controller_task >= CONTROLLER_TASK_INTERVAL) {
		s_controller_task = 0;
		controller_task(&controller_drv);
	}

	// Do something with the controller...
	if(controller_drv.ready && s_eval_task >= CONTROLLER_EVAL_INTERVAL) {
		s_eval_task = 0;
		// Print everything
		ESP_LOGI("test", "ps%1i jsl: %3i %3i jsr: %3i %3i l1: %3i l2: %3i r1: %3i r2: %3i "
			 "↑%3i →%3i ↓%3i ←%3i △%3i ○%3i ⛌%3i □%3i "
			 "sel%1i jslb%1i jsrb%1i sta%1i ↑%1i →%1i ↓%1i ←%1i △%1i ○%1i ⛌%1i □%1i "
			 "l1b%1i l2b%1i r1b%1i r2b%1i",
			 controller_drv.report->ps_button,
			 controller_drv.report->js_left.x,
			 controller_drv.report->js_left.y,
			 controller_drv.report->js_right.x,
			 controller_drv.report->js_right.y,
			 controller_drv.report->pressure.rear.l1,
			 controller_drv.report->pressure.rear.r1,
			 controller_drv.report->pressure.rear.l2,
			 controller_drv.report->pressure.rear.r2,
			 controller_drv.report->pressure.dpad.up,
			 controller_drv.report->pressure.dpad.right,
			 controller_drv.report->pressure.dpad.down,
			 controller_drv.report->pressure.dpad.left,
			 controller_drv.report->pressure.symbols.triangle,
			 controller_drv.report->pressure.symbols.circle, 
			 controller_drv.report->pressure.symbols.cross,
			 controller_drv.report->pressure.symbols.square,
			 controller_drv.report->buttons.select,
			 controller_drv.report->buttons.left_js,
			 controller_drv.report->buttons.right_js,
			 controller_drv.report->buttons.start,
			 controller_drv.report->buttons.up,
			 controller_drv.report->buttons.right,
			 controller_drv.report->buttons.down,
			 controller_drv.report->buttons.left,
			 controller_drv.report->buttons.triangle,
			 controller_drv.report->buttons.circle,
			 controller_drv.report->buttons.cross,
			 controller_drv.report->buttons.square,
			 controller_drv.report->buttons.l1,
			 controller_drv.report->buttons.l2,
			 controller_drv.report->buttons.r1,
			 controller_drv.report->buttons.r2);
	}
}
