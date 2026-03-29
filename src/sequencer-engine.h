#ifndef SEQUENCER_ENGINE_H
#define SEQUENCER_ENGINE_H

#include <array>
#include <cstdint>

#include "brain-io/audio-cv-out.h"
#include "brain-io/pulse.h"
#include "brain-ui/button-led.h"
#include "brain-ui/leds.h"
#include "brain-ui/pot-multi-function.h"
#include "brain-ui/pots.h"

struct Step {
	uint16_t pitch_q8;
	bool gate;
};

struct Sequence {
	static constexpr uint8_t kMaxSteps = 64;
	std::array<Step, kMaxSteps> steps;
	uint8_t length;
	uint8_t position;
};

enum class QuantizationMode : uint8_t {
	kUnquantized = 0,
	kChromatic = 1,
	kMajor = 2,
	kMinor = 3,
	kPentatonic = 4,
	kExtra = 5
};

enum class ExternalClockSource : uint8_t {
	kInternal = 0,
	kExternalPulse = 1,
	kExternalMidi = 2
};

class SequencerEngine {
public:
	SequencerEngine();
	void update();
	void on_mode_enter();
	void on_mode_exit();
	void on_button_a_short_press();
	void on_button_b_press();
	void on_button_b_release();
	void arm_root_edit();
	void release_root_edit();
	void on_root_edit_midi_note(uint8_t note);
	void on_root_edit_note_pot_value(uint8_t value);
	void on_root_edit_octave_pot_value(uint8_t value);
	void on_midi_clock_tick(uint64_t event_us);
	void on_midi_transport_start();
	void on_midi_transport_continue();
	void on_midi_transport_stop();
	void on_external_step_event(ExternalClockSource source, uint64_t event_us);
	uint16_t tempo_bpm() const;
	bool is_playing() const;
	bool external_sync_enabled() const;
	ExternalClockSource external_clock_source() const;
	bool midi_transport_running() const;
	float swing() const;
	float randomness() const;
	uint8_t sequence_length() const;
	uint8_t range_octaves() const;
	const char* quantization_mode_name() const;
	const char* root_note_name() const;
	uint8_t octave_transpose() const;
	bool root_edit_armed() const;
	float last_raw_voltage() const;
	float last_quantized_voltage() const;
	uint32_t base_interval_us() const;
	uint32_t current_interval_us() const;
	const char* gate_history() const;

private:
	static constexpr uint8_t POT_INDEX_BPM = 0;
	static constexpr uint8_t POT_INDEX_RANGE_OR_QUANTIZATION = 1;
	static constexpr uint8_t POT_INDEX_RANDOMNESS_OR_LENGTH = 2;
	static constexpr uint8_t POT_FUNCTION_ID_NONE = 255;
	static constexpr uint8_t POT_FUNCTION_ID_BPM = 1;
	static constexpr uint8_t POT_FUNCTION_ID_SWING = 2;
	static constexpr uint8_t POT_FUNCTION_ID_RANGE = 3;
	static constexpr uint8_t POT_FUNCTION_ID_RANDOMNESS = 4;
	static constexpr uint8_t POT_FUNCTION_ID_QUANTIZATION = 5;
	static constexpr uint8_t POT_FUNCTION_ID_LENGTH = 6;
	static constexpr uint8_t POT_FUNCTION_PICKUP_HYSTERESIS = 1;
	static constexpr uint8_t NUM_POTS = 3;
	static constexpr uint16_t BPM_MIN = 60;
	static constexpr uint16_t BPM_MAX = 240;
	static constexpr uint8_t SEQUENCE_LENGTH_MIN = 2;
	static constexpr uint8_t SEQUENCE_LENGTH_MAX = 32;
	static constexpr uint8_t RANGE_OCTAVES_MIN = 0;
	static constexpr uint8_t RANGE_OCTAVES_MAX = 6;
	static constexpr uint8_t QUANTIZATION_MODE_COUNT = 6;
	static constexpr uint8_t ROOT_NOTE_COUNT = 12;
	static constexpr uint8_t OCTAVE_TRANSPOSE_MAX = 5;
	static constexpr uint8_t STEPS_PER_QUARTER_NOTE = 4;
	static constexpr uint32_t GATE_PULSE_US = 20000;
	static constexpr uint32_t BUTTON_LED_BLINK_MS = 80;
	static constexpr uint32_t BUTTON_LED_BLINK_INTERVAL_MS = 40;
	static constexpr uint8_t SWING_MAX_NUMERATOR = 1;   // 50%
	static constexpr uint8_t SWING_MAX_DENOMINATOR = 2;
	static constexpr uint8_t EXTERNAL_CLOCK_EMA_SHIFT = 3;  // 1/8 new sample
	static constexpr uint8_t MIDI_CLOCKS_PER_SEQUENCER_STEP = 6;
	static constexpr uint8_t POT_LED_SOFT_BRIGHTNESS = 48;
	static constexpr uint16_t PITCH_Q8_PER_SEMITONE = 256;
	static constexpr uint16_t SEMITONES_PER_OCTAVE = 12;
	static constexpr uint16_t SOURCE_RANGE_OCTAVES = 5;
	static constexpr uint16_t RANDOM_MAX_Q8 = SOURCE_RANGE_OCTAVES * SEMITONES_PER_OCTAVE * PITCH_Q8_PER_SEMITONE;
	static constexpr uint64_t POT_LED_OVERLAY_HOLD_US = 1000ULL * 1000ULL;
	static constexpr uint8_t POT_LED_ACTIVITY_RAW_THRESHOLD = 3;
	static constexpr uint8_t ROOT_EDIT_POT_RAW_THRESHOLD = 3;
	static constexpr uint32_t EXTERNAL_EVENT_INTERVAL_MIN_US = 1000u;
	static constexpr uint32_t EXTERNAL_EVENT_INTERVAL_MAX_US = 2000000u;
	static constexpr uint32_t MIDI_CLOCK_INTERVAL_MIN_US = 500u;
	static constexpr uint32_t MIDI_CLOCK_INTERVAL_MAX_US = 500000u;
	static constexpr uint32_t EXTERNAL_SOURCE_TIMEOUT_MIN_US = 150000u;
	static constexpr uint32_t EXTERNAL_SOURCE_TIMEOUT_MAX_US = 2000000u;

	Sequence sequence_a_;
	std::array<Step, Sequence::kMaxSteps> sequence_b_steps_;
	brain::ui::Pots pots_;
	brain::ui::PotMultiFunction pot_multi_function_;
	brain::io::AudioCvOut dac_;
	brain::io::Pulse gate_;
	brain::ui::Leds leds_;
	brain::ui::ButtonLed button_led_;
	bool calibrated_output_enabled_;

	bool initialized_;
	bool playing_;
	bool gate_active_;
	uint16_t bpm_;
	uint32_t tick_interval_us_;
	uint64_t next_tick_due_us_;
	uint64_t gate_off_time_us_;
	uint32_t tick_counter_;
	uint32_t current_step_interval_us_;
	bool shift_active_;
	bool last_shift_context_;
	uint8_t swing_pot_value_;
	uint8_t range_octaves_;
	QuantizationMode quantization_mode_;
	uint8_t root_note_;
	uint8_t octave_transpose_;
	bool has_persisted_root_note_;
	uint8_t persisted_root_note_;
	bool root_edit_armed_;
	uint8_t root_edit_octave_pot_reference_raw_;
	uint8_t root_edit_note_pot_reference_raw_;
	uint64_t root_edit_octave_overlay_until_us_;
	uint8_t randomness_pot_value_;
	uint8_t mutation_threshold_;
	bool external_sync_enabled_;
	bool last_pulse_in_high_;
	uint64_t last_external_tick_us_;  // Last external pulse edge timestamp.
	uint32_t external_interval_us_;    // Pulse-derived external step interval estimate.
	uint64_t last_midi_clock_tick_us_;
	uint64_t last_midi_event_us_;
	uint32_t midi_interval_us_;
	uint8_t midi_clock_ticks_since_step_;
	uint8_t pending_midi_step_events_;
	bool midi_transport_running_;
	ExternalClockSource external_clock_source_;
	bool external_swing_tick_pending_;
	uint64_t external_swing_tick_due_us_;
	float last_raw_voltage_;
	float last_quantized_voltage_;
	bool pot_led_overlay_active_;
	uint64_t pot_led_overlay_last_change_us_;
	std::array<uint8_t, NUM_POTS> pot_raw_values_;
	std::array<uint8_t, NUM_POTS> last_pot_raw_values_;
	std::array<uint8_t, brain::ui::NO_OF_LEDS> gate_history_fifo_;
	char gate_history_text_[brain::ui::NO_OF_LEDS + 1];
	uint32_t rng_state_a_;
	uint32_t rng_state_b_;

	void init_sequence();
	void init_io();
	void scan_pots_snapshot();
	void init_pot_functions();
	void set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function);
	void update_pot_mappings(bool force_apply = false);
	void update_bpm_from_pot(bool force_apply = false);
	void update_swing_from_pot1(bool force_apply = false);
	void update_range_or_quantization_from_pot2(bool force_apply = false);
	void update_randomness_or_length_from_pot3(bool force_apply = false);
	void check_root_edit_pot_input();
	void set_root_note_live(uint8_t root_note, bool update_reference = true);
	void set_octave_transpose_live(uint8_t octave);
	void refresh_output_after_root_change();
	void persist_root_note_if_needed();
	uint8_t map_sequence_length_with_soft_snap(uint8_t pot_value) const;
	bool is_power_of_two_sequence_length(uint8_t length) const;
	uint8_t sequence_length_led_count(uint8_t length) const;
	void apply_mutation_for_step(uint8_t step_index);
	void update_pot_led_overlay(uint64_t now_us);
	void show_active_pot_overlay(uint8_t pot_index);
	void show_transpose_overlay();
	void show_octave_transpose_overlay();
	uint8_t active_pot_percent_255(uint8_t pot_index) const;
	uint8_t active_pot_led_mask(uint8_t pot_index) const;
	uint8_t percent_to_led_mask(uint8_t percent_255) const;
	void reset_gate_history();
	void push_gate_history(bool gate_high);
	void refresh_gate_history_view();
	uint8_t gate_history_mask() const;
	void write_pitch_voltage(brain::io::AudioCvOutChannel channel, float voltage);
	void tick(uint64_t now_us);
	void update_external_clock_source(uint64_t now_us);
	void handle_external_pulse_edge(uint64_t now_us);
	void handle_pending_external_swing_tick(uint64_t now_us);
	void clear_external_swing_pending();
	void reset_transport();
	uint32_t compute_swing_delta_us(uint32_t base_interval_us) const;
	uint32_t compute_external_source_timeout_us(uint32_t estimated_step_interval_us) const;
	bool is_external_source_active(uint64_t now_us, uint64_t last_event_us, uint32_t estimated_step_interval_us) const;
	uint16_t apply_pitch_range(uint16_t source_q8) const;
	uint16_t quantize_pitch(uint16_t pitch_q8) const;
	uint32_t compute_next_step_interval_us() const;
	static const char* quantization_mode_to_string(QuantizationMode mode);
	static const char* root_note_to_string(uint8_t root_note);
	static uint32_t next_random(uint32_t& state);
	static uint8_t random_u8(uint32_t& state);
	static float pitch_q8_to_voltage(uint16_t pitch_q8);
};

#endif
