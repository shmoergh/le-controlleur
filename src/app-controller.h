#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "midi-to-cv-engine.h"

enum class AppMode {
	kMidiToCv = 0,
	kSequencer = 1
};

class AppController {
public:
	AppController();
	void update();
	AppMode mode() const;

private:
	AppMode mode_;
	MidiToCVEngine midi_to_cv_engine_;
};

#endif
