#ifndef BASIC_MIDI_TO_CV_H
#define BASIC_MIDI_TO_CV_H

#include <vector>
#include <math.h>

#include "brain-common/brain-gpio-setup.h"
#include "brain-utils/midi-to-cv.h"
#include "brain-ui/button.h"
#include "brain-ui/leds.h"
#include "brain-ui/pot-multi-function.h"
#include "brain-ui/pots.h"
#include "brain-utils/helpers.h"

using brain::utils::MidiToCV;
using brain::ui::Button;
using brain::ui::Leds;
using brain::ui::PotMultiFunction;
using brain::ui::PotMode;
using brain::ui::Pots;

constexpr uint8_t POT_CV_CHANNEL_THRESHOLD = 127;
constexpr uint8_t LED_MASK_CHANNEL_A 		= 0b000001;
constexpr uint8_t LED_MASK_CHANNEL_B 		= 0b000010;
constexpr uint8_t LED_MASK_MODE_VELOCITY 	= 0b001000;
constexpr uint8_t LED_MASK_MODE_MODWHEEL 	= 0b010000;
constexpr uint8_t LED_MASK_MODE_UNISON 		= 0b011000;
constexpr uint8_t LED_MASK_MODE_DUO 		= 0b100000;
constexpr uint8_t POT_MIDI_CHANNEL = 0;
constexpr uint8_t POT_CV_CHANNEL = 1;
constexpr uint8_t POT_MODE = 2;
constexpr uint8_t POT_FUNCTION_ID_NONE = 255;
constexpr uint8_t POT_FUNCTION_ID_MIDI_CHANNEL = 1;
constexpr uint8_t POT_FUNCTION_ID_CV_CHANNEL = 2;
constexpr uint8_t POT_FUNCTION_ID_MODE = 3;
constexpr uint8_t POT_FUNCTION_PICKUP_HYSTERESIS = 1;
constexpr uint8_t NUM_POTS = 3;
constexpr uint32_t PANIC_HOLD_THRESHOLD_MS = 2000;

enum State {
	kDefault = 0,
	kSetMidiChannel = 1,
	kSetCVChannel = 2,
	kPanicStarted = 3
};

class BasicMidi2CV : public MidiToCV
{
public:
	BasicMidi2CV(brain::io::AudioCvOutChannel cv_channel, uint8_t midi_channel);
	void update();
	State get_state() const;
	uint8_t get_midi_channel() const;

private:
	Button button_a_;
	Button button_b_;
	bool button_a_pressed_;
	bool button_b_pressed_;
	Pots pots_;
	PotMultiFunction pot_multi_function_;
	Leds leds_;

	uint8_t midi_channel_;
	brain::io::AudioCvOutChannel cv_channel_;
	MidiToCV::Mode mode_;
	State state_;
	uint8_t key_pressed_;
	uint8_t playhead_led_;
	bool reset_leds_;
	absolute_time_t panic_timer_start_;

	void set_leds_from_mask(uint8_t mask);

	void button_a_on_press();
	void button_a_on_release();

	void button_b_on_press();
	void button_b_on_release();

	void reset_panic();
	void init_pot_functions();
	void set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function);
	void reset_pot_function_context();

	void update_midi_channel_setting();
	void update_cv_channel_setting();
	void update_cc_setting();
	void load_settings();
};

#endif
