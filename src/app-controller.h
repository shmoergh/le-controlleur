#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <pico/stdlib.h>

#include "brain-common/brain-gpio-setup.h"
#include "brain-ui/button.h"
#include "midi-to-cv-engine.h"

constexpr uint32_t BUTTON_SHORT_PRESS_MAX_MS = 400;
constexpr uint32_t BUTTON_LONG_PRESS_MIN_MS = 1500;
constexpr uint32_t BUTTON_DUAL_PRESS_WINDOW_MS = 120;

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
	brain::ui::Button button_a_;
	brain::ui::Button button_b_;
	AppMode mode_;
	MidiToCVEngine midi_to_cv_engine_;
	bool button_a_pressed_;
	bool button_b_pressed_;
	bool dual_button_active_;
	bool dual_button_action_handled_;
	absolute_time_t first_button_pressed_at_;
	absolute_time_t dual_button_started_at_;

	void on_button_a_press();
	void on_button_a_release();
	void on_button_b_press();
	void on_button_b_release();
	void start_dual_button_press(absolute_time_t started_at);
	void clear_dual_button_tracking();
	void maybe_toggle_mode();
	void maybe_handle_short_dual_button_on_release(absolute_time_t released_at);
	void set_mode(AppMode mode);
};

#endif
