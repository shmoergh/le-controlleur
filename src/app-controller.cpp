#include "app-controller.h"
#include "debug-log.h"

AppController::AppController() :
	mode_(AppMode::kMidiToCv),
	midi_to_cv_engine_(brain::io::AudioCvOutChannel::kChannelB, 1) {
	LOG_INFO("APP", "mode=MIDI2CV");
}

void AppController::update() {
	switch (mode_) {
		case AppMode::kMidiToCv:
			midi_to_cv_engine_.update();
			break;
		case AppMode::kSequencer:
			// Sequencer routing will be implemented in the next phase.
			break;
	}
}

AppMode AppController::mode() const {
	return mode_;
}
