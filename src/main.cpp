#include <pico/stdlib.h>
#include <stdio.h>

#include "app-controller.h"
#include "debug-log.h"

int main() {
	stdio_init_all();

	LOG_INFO("APP", "le-controlleur started version=%s git=%s", LE_CONTROLLEUR_VERSION, LE_CONTROLLEUR_GIT_HASH);

	AppController app_controller;

	while (true) {
		app_controller.update();
	}

	return 0;
}
