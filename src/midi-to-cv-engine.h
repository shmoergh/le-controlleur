#ifndef MIDI_TO_CV_ENGINE_H
#define MIDI_TO_CV_ENGINE_H

#include <vector>

#include "brain-common/brain-gpio-setup.h"
#include "brain-utils/midi-to-cv.h"
#include "brain-ui/leds.h"
#include "brain-ui/pot-multi-function.h"
#include "brain-ui/pots.h"
#include "brain-utils/helpers.h"

using brain::utils::MidiToCV;
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

enum State {
	kDefault = 0,
	kSetMidiChannel = 1,
	kSetCVChannel = 2
};

class MidiToCVEngine : public MidiToCV
{
public:
	MidiToCVEngine(brain::io::AudioCvOutChannel cv_channel, uint8_t midi_channel);
	void update();
	void panic();
	void play_startup_animation();
	void on_button_a_press();
	void on_button_a_release();
	void on_button_b_press();
	void on_button_b_release();
	State get_state() const;
	uint8_t get_midi_channel() const;

private:
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
	bool has_persisted_midi_channel_;
	uint8_t persisted_midi_channel_;

	void set_leds_from_mask(uint8_t mask);

	void init_pot_functions();
	void set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function);
	void reset_pot_function_context();

	void update_midi_channel_setting();
	void update_cv_channel_setting();
	void update_cc_setting();
	void load_settings();
	void persist_midi_channel_if_needed();
};

#endif
