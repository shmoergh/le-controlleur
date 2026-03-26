#include <pico/stdlib.h>
#include <stdio.h>

#include "basic-midi2cv.h"

int main() {
	stdio_init_all();

	printf("brain-basic-midi2cv started\n");

	BasicMidi2CV midi_2_cv(brain::io::AudioCvOutChannel::kChannelB, 1);

	while (true) {
		midi_2_cv.update();
	}

	return 0;
}
