#include <pico/stdlib.h>
#include <stdio.h>

#include "basic-midi2cv.h"
#include "debug-log.h"

int main() {
	stdio_init_all();

	LOG_INFO("APP", "brain-basic-midi2cv started version=%s git=%s", LE_CONTROLLEUR_VERSION, LE_CONTROLLEUR_GIT_HASH);

	BasicMidi2CV midi_2_cv(brain::io::AudioCvOutChannel::kChannelB, 1);

	while (true) {
		midi_2_cv.update();
	}

	return 0;
}
