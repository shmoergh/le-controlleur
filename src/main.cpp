#include <pico/stdlib.h>
#include <stdio.h>

#include "app-controller.h"

int main() {
	stdio_init_all();

	AppController app_controller;

	while (true) {
		app_controller.update();
	}

	return 0;
}
