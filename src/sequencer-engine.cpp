#include "sequencer-engine.h"

#include "pico/time.h"

#include "brain-ui/pots.h"
#include "debug-log.h"
#include "settings-storage.h"

SequencerEngine::SequencerEngine() :
	leds_(false),
	initialized_(false),
	playing_(false),
	gate_active_(false),
	bpm_(120),
	tick_interval_us_(125000),
	next_tick_due_us_(0),
	gate_off_time_us_(0),
	tick_counter_(0),
	current_step_interval_us_(125000),
	shift_active_(false),
	last_shift_context_(false),
	swing_pot_value_(0),
	range_octaves_(3),
	quantization_mode_(QuantizationMode::kChromatic),
	root_note_(0),
	octave_transpose_(2),
	has_persisted_root_note_(false),
	persisted_root_note_(0),
	root_edit_armed_(false),
	root_edit_octave_pot_reference_raw_(0),
	root_edit_note_pot_reference_raw_(0),
	root_edit_octave_overlay_until_us_(0),
	randomness_pot_value_(0),
	mutation_threshold_(0),
	external_sync_enabled_(false),
	last_pulse_in_high_(false),
	last_external_tick_us_(0),
	external_interval_us_(125000),
	last_midi_clock_tick_us_(0),
	last_midi_event_us_(0),
	midi_interval_us_(125000),
	midi_clock_ticks_since_step_(0),
	pending_midi_step_events_(0),
	midi_transport_running_(false),
	external_clock_source_(ExternalClockSource::kInternal),
	external_swing_tick_pending_(false),
	external_swing_tick_due_us_(0),
	last_raw_voltage_(0.0f),
	last_quantized_voltage_(0.0f),
	pot_led_overlay_active_(false),
	pot_led_overlay_last_change_us_(0),
	pot_raw_values_{0, 0, 0},
	last_pot_raw_values_{0, 0, 0},
	gate_history_fifo_{0, 0, 0, 0, 0, 0},
	gate_history_text_{'0', '0', '0', '0', '0', '0', '\0'},
	rng_state_a_(0x12345678u),
	rng_state_b_(0x87654321u) {
	init_sequence();
	init_io();
}

void SequencerEngine::update() {
	if (!initialized_) {
		return;
	}

	leds_.update();
	button_led_.update();
	scan_pots_snapshot();
	update_pot_mappings();

	const uint64_t now_us = time_us_64();
	check_root_edit_pot_input();
	update_pot_led_overlay(now_us);
	if (root_edit_armed_) {
		if (now_us < root_edit_octave_overlay_until_us_) {
			show_octave_transpose_overlay();
		} else {
			show_transpose_overlay();
		}
		pot_led_overlay_active_ = true;
		pot_led_overlay_last_change_us_ = now_us;
	}
	const bool pulse_in_high = gate_.read();
	if (pulse_in_high && !last_pulse_in_high_) {
		handle_external_pulse_edge(now_us);
	}
	last_pulse_in_high_ = pulse_in_high;
	update_external_clock_source(now_us);
	if (external_clock_source_ == ExternalClockSource::kExternalPulse) {
		pending_midi_step_events_ = 0;
	}

	if (gate_active_ && now_us >= gate_off_time_us_) {
		gate_.set(false);
		gate_active_ = false;
	}
	handle_pending_external_swing_tick(now_us);

	if (!playing_) {
		return;
	}

	if (external_sync_enabled_) {
		if (external_clock_source_ == ExternalClockSource::kExternalMidi) {
			if (pending_midi_step_events_ > 0) {
				--pending_midi_step_events_;
				on_external_step_event(ExternalClockSource::kExternalMidi, now_us);
			}
		}
		return;
	}

	if (next_tick_due_us_ == 0) {
		next_tick_due_us_ = now_us;
	}

	uint8_t safety_counter = 0;
	while (now_us >= next_tick_due_us_ && safety_counter < 8) {
		tick(next_tick_due_us_);
		current_step_interval_us_ = compute_next_step_interval_us();
		next_tick_due_us_ += current_step_interval_us_;
		++safety_counter;
	}
}

void SequencerEngine::scan_pots_snapshot() {
	pots_.scan();
	for (uint8_t i = 0; i < NUM_POTS; ++i) {
		pot_raw_values_[i] = static_cast<uint8_t>(pots_.get_buffered(i));
	}
}

void SequencerEngine::on_mode_enter() {
	if (!initialized_) {
		return;
	}

	// Shared 6-LED bus: sequencer uses PWM for dim overlays.
	leds_.set_mode(brain::ui::LedMode::kPwm);
	reset_transport();
	root_edit_armed_ = false;
	shift_active_ = false;
	last_shift_context_ = shift_active_;
	update_pot_mappings(true);
}

void SequencerEngine::on_mode_exit() {
	if (!initialized_) {
		return;
	}

	reset_transport();
	// Return shared LEDs to simple GPIO mode for MIDI 2 CV.
	leds_.set_mode(brain::ui::LedMode::kSimple);
	leds_.off_all();
	if (root_edit_armed_) {
		persist_root_note_if_needed();
	}
	root_edit_armed_ = false;
	shift_active_ = false;
	last_shift_context_ = shift_active_;
}

void SequencerEngine::on_button_a_short_press() {
	if (!initialized_) {
		return;
	}

	playing_ = !playing_;

	if (playing_) {
		next_tick_due_us_ = 0;
		tick_counter_ = 0;
		clear_external_swing_pending();
	} else {
		gate_.set(false);
		gate_active_ = false;
		clear_external_swing_pending();
		midi_clock_ticks_since_step_ = 0;
		pending_midi_step_events_ = 0;
		last_midi_clock_tick_us_ = 0;
		last_midi_event_us_ = 0;
		button_led_.off();
	}
}

void SequencerEngine::on_button_b_press() {
	if (!initialized_ || shift_active_) {
		return;
	}

	shift_active_ = true;
	update_pot_mappings();
}

void SequencerEngine::on_button_b_release() {
	if (!initialized_ || !shift_active_) {
		return;
	}

	shift_active_ = false;
	update_pot_mappings();
}

void SequencerEngine::arm_root_edit() {
	if (!initialized_) {
		return;
	}

	root_edit_armed_ = true;
	root_edit_octave_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANGE_OR_QUANTIZATION];
	root_edit_note_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANDOMNESS_OR_LENGTH];
	root_edit_octave_overlay_until_us_ = 0;
}

void SequencerEngine::release_root_edit() {
	if (!initialized_) {
		return;
	}

	if (!root_edit_armed_) {
		return;
	}

	root_edit_armed_ = false;
	root_edit_octave_overlay_until_us_ = 0;
	persist_root_note_if_needed();
}

void SequencerEngine::on_root_edit_midi_note(uint8_t note) {
	if (!initialized_ || !root_edit_armed_) {
		return;
	}

	set_root_note_live(static_cast<uint8_t>(note % ROOT_NOTE_COUNT), false);
}

void SequencerEngine::on_root_edit_note_pot_value(uint8_t value) {
	if (!initialized_ || !root_edit_armed_) {
		return;
	}

	const uint8_t root_note = static_cast<uint8_t>((static_cast<uint32_t>(value) * ROOT_NOTE_COUNT) / 256u);
	set_root_note_live(root_note);
}

void SequencerEngine::on_root_edit_octave_pot_value(uint8_t value) {
	if (!initialized_ || !root_edit_armed_) {
		return;
	}

	// 6 hard bins: +0..+5 octaves (+0V .. +5V)
	const uint8_t octave = static_cast<uint8_t>((static_cast<uint32_t>(value) * (OCTAVE_TRANSPOSE_MAX + 1u)) / 256u);
	set_octave_transpose_live(octave);
}

void SequencerEngine::on_midi_clock_tick(uint64_t event_us) {
	if (!initialized_ || !midi_transport_running_ || !playing_) {
		return;
	}

	if (last_midi_clock_tick_us_ != 0 && event_us > last_midi_clock_tick_us_) {
		const uint32_t midi_clock_interval_us = static_cast<uint32_t>(event_us - last_midi_clock_tick_us_);
		if (midi_clock_interval_us >= MIDI_CLOCK_INTERVAL_MIN_US
			&& midi_clock_interval_us <= MIDI_CLOCK_INTERVAL_MAX_US) {
			const uint32_t midi_step_interval_us = midi_clock_interval_us * MIDI_CLOCKS_PER_SEQUENCER_STEP;
			const int32_t error =
				static_cast<int32_t>(midi_step_interval_us) - static_cast<int32_t>(midi_interval_us_);
			midi_interval_us_ = static_cast<uint32_t>(
				static_cast<int32_t>(midi_interval_us_) + (error >> EXTERNAL_CLOCK_EMA_SHIFT)
			);
		}
	}

	last_midi_clock_tick_us_ = event_us;
	last_midi_event_us_ = event_us;

	if (!external_sync_enabled_) {
		return;
	}

	++midi_clock_ticks_since_step_;
	if (midi_clock_ticks_since_step_ < MIDI_CLOCKS_PER_SEQUENCER_STEP) {
		return;
	}
	midi_clock_ticks_since_step_ = 0;
	if (pending_midi_step_events_ < 255u) {
		++pending_midi_step_events_;
	}
}

void SequencerEngine::on_midi_transport_start() {
	if (!initialized_) {
		return;
	}
	if (!external_sync_enabled_) {
		return;
	}

	midi_transport_running_ = true;
	midi_clock_ticks_since_step_ = 0;
	pending_midi_step_events_ = 0;
	clear_external_swing_pending();

	if (!external_sync_enabled_ || external_clock_source_ == ExternalClockSource::kExternalPulse) {
		return;
	}

	sequence_a_.position = 0;
	tick_counter_ = 0;
	playing_ = true;
	next_tick_due_us_ = 0;
	gate_.set(false);
	gate_active_ = false;
}

void SequencerEngine::on_midi_transport_continue() {
	if (!initialized_) {
		return;
	}
	if (!external_sync_enabled_) {
		return;
	}

	midi_transport_running_ = true;
	midi_clock_ticks_since_step_ = 0;
	pending_midi_step_events_ = 0;
	clear_external_swing_pending();

	if (!external_sync_enabled_ || external_clock_source_ == ExternalClockSource::kExternalPulse) {
		return;
	}

	playing_ = true;
	next_tick_due_us_ = 0;
}

void SequencerEngine::on_midi_transport_stop() {
	if (!initialized_) {
		return;
	}
	if (!external_sync_enabled_) {
		return;
	}

	midi_transport_running_ = false;
	midi_clock_ticks_since_step_ = 0;
	pending_midi_step_events_ = 0;
	clear_external_swing_pending();

	if (!external_sync_enabled_ || external_clock_source_ == ExternalClockSource::kExternalPulse) {
		return;
	}

	playing_ = false;
	gate_.set(false);
	gate_active_ = false;
	button_led_.off();
}

void SequencerEngine::on_external_step_event(ExternalClockSource source, uint64_t event_us) {
	if (!initialized_ || !external_sync_enabled_ || !playing_) {
		return;
	}

	uint32_t base_interval_us = tick_interval_us_;
	switch (source) {
		case ExternalClockSource::kExternalPulse:
			base_interval_us = external_interval_us_;
			last_external_tick_us_ = event_us;
			break;
		case ExternalClockSource::kExternalMidi:
			base_interval_us = midi_interval_us_;
			last_midi_event_us_ = event_us;
			break;
		case ExternalClockSource::kInternal:
		default:
			return;
	}

	tick_interval_us_ = base_interval_us;

	if (external_swing_tick_pending_) {
		// Avoid dropping a delayed swung step when external steps arrive faster than expected.
		external_swing_tick_pending_ = false;
		tick(event_us);
	}

	const uint32_t swing_delta = compute_swing_delta_us(base_interval_us);
	const bool delay_this_step = (swing_delta > 0u) && ((tick_counter_ & 1u) == 1u);
	if (delay_this_step) {
		external_swing_tick_pending_ = true;
		external_swing_tick_due_us_ = event_us + swing_delta;
		current_step_interval_us_ = base_interval_us + swing_delta;
		return;
	}

	current_step_interval_us_ = base_interval_us;
	tick(event_us);
}

void SequencerEngine::update_external_clock_source(uint64_t now_us) {
	const ExternalClockSource previous_source = external_clock_source_;
	if (!external_sync_enabled_) {
		external_clock_source_ = ExternalClockSource::kInternal;
		return;
	}

	const bool pulse_active = is_external_source_active(now_us, last_external_tick_us_, external_interval_us_);
	const bool midi_active = midi_transport_running_
		&& is_external_source_active(now_us, last_midi_event_us_, midi_interval_us_);

	if (pulse_active) {
		external_clock_source_ = ExternalClockSource::kExternalPulse;
	} else if (midi_active) {
		external_clock_source_ = ExternalClockSource::kExternalMidi;
	} else {
		external_clock_source_ = ExternalClockSource::kInternal;
	}

	if (external_clock_source_ != previous_source) {
		clear_external_swing_pending();
		if (external_clock_source_ != ExternalClockSource::kExternalMidi) {
			pending_midi_step_events_ = 0;
		}
	}
}

void SequencerEngine::handle_external_pulse_edge(uint64_t now_us) {
	if (last_external_tick_us_ != 0 && now_us > last_external_tick_us_) {
		const uint32_t pulse_interval_us = static_cast<uint32_t>(now_us - last_external_tick_us_);
		if (pulse_interval_us >= EXTERNAL_EVENT_INTERVAL_MIN_US && pulse_interval_us <= EXTERNAL_EVENT_INTERVAL_MAX_US) {
			const int32_t error = static_cast<int32_t>(pulse_interval_us) - static_cast<int32_t>(external_interval_us_);
			external_interval_us_ = static_cast<uint32_t>(
				static_cast<int32_t>(external_interval_us_) + (error >> EXTERNAL_CLOCK_EMA_SHIFT)
			);
		}
	}
	last_external_tick_us_ = now_us;
	if (!external_sync_enabled_) {
		return;
	}
	on_external_step_event(ExternalClockSource::kExternalPulse, now_us);
}

void SequencerEngine::handle_pending_external_swing_tick(uint64_t now_us) {
	if (!external_sync_enabled_ || !playing_ || !external_swing_tick_pending_) {
		return;
	}

	if (now_us < external_swing_tick_due_us_) {
		return;
	}

	external_swing_tick_pending_ = false;
	tick(now_us);
}

void SequencerEngine::clear_external_swing_pending() {
	external_swing_tick_pending_ = false;
	external_swing_tick_due_us_ = 0;
}

void SequencerEngine::init_sequence() {
	sequence_a_.steps.fill({0, false});
	sequence_b_steps_.fill({0, false});
	sequence_a_.length = 8;
	sequence_a_.position = 0;

	// Deterministic bring-up pattern.
	sequence_a_.steps[0] = {0u * PITCH_Q8_PER_SEMITONE, true};
	sequence_a_.steps[1] = {6u * PITCH_Q8_PER_SEMITONE, false};
	sequence_a_.steps[2] = {12u * PITCH_Q8_PER_SEMITONE, true};
	sequence_a_.steps[3] = {18u * PITCH_Q8_PER_SEMITONE, false};
	sequence_a_.steps[4] = {24u * PITCH_Q8_PER_SEMITONE, true};
	sequence_a_.steps[5] = {30u * PITCH_Q8_PER_SEMITONE, false};
	sequence_a_.steps[6] = {36u * PITCH_Q8_PER_SEMITONE, true};
	sequence_a_.steps[7] = {42u * PITCH_Q8_PER_SEMITONE, true};

	sequence_b_steps_[0] = {3u * PITCH_Q8_PER_SEMITONE, true};
	sequence_b_steps_[1] = {9u * PITCH_Q8_PER_SEMITONE, true};
	sequence_b_steps_[2] = {15u * PITCH_Q8_PER_SEMITONE, false};
	sequence_b_steps_[3] = {21u * PITCH_Q8_PER_SEMITONE, true};
	sequence_b_steps_[4] = {27u * PITCH_Q8_PER_SEMITONE, false};
	sequence_b_steps_[5] = {33u * PITCH_Q8_PER_SEMITONE, true};
	sequence_b_steps_[6] = {39u * PITCH_Q8_PER_SEMITONE, false};
	sequence_b_steps_[7] = {45u * PITCH_Q8_PER_SEMITONE, true};
}

void SequencerEngine::init_io() {
	brain::ui::PotsConfig pots_config = brain::ui::create_default_config();
	// Sequencer reads Pot 1 and Pot 3 continuously; keep mux settling enabled
	// to avoid cross-channel bleed between adjacent reads.
	pots_config.simple = false;
	pots_config.output_resolution = 8;
	pots_config.settling_delay_us = 150;
	pots_config.samples_per_read = 3;
	pots_.init(pots_config);

	if (!dac_.init()) {
		LOG_ERROR("SEQ", "DAC init failed");
		initialized_ = false;
		return;
	}

	dac_.set_coupling(brain::io::AudioCvOutChannel::kChannelA, brain::io::AudioCvOutCoupling::kDcCoupled);
	dac_.set_coupling(brain::io::AudioCvOutChannel::kChannelB, brain::io::AudioCvOutCoupling::kDcCoupled);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, 0.0f);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, 0.0f);

	gate_.begin();
	gate_.set(false);
	// Keep simple mode when sequencer is inactive so MIDI 2 CV LED writes work.
	leds_.init(brain::ui::LedMode::kSimple);
	leds_.off_all();
	button_led_.init();
	button_led_.off();
	reset_gate_history();
	scan_pots_snapshot();
	last_pot_raw_values_ = pot_raw_values_;
	root_edit_octave_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANGE_OR_QUANTIZATION];
	root_edit_note_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANDOMNESS_OR_LENGTH];

	uint8_t persisted_root_note = 0;
	if (load_persisted_root_note(persisted_root_note) && persisted_root_note < ROOT_NOTE_COUNT) {
		root_note_ = persisted_root_note;
		has_persisted_root_note_ = true;
		persisted_root_note_ = persisted_root_note;
	} else {
		root_note_ = 0;
		has_persisted_root_note_ = false;
		persisted_root_note_ = 0;
	}

	init_pot_functions();
	initialized_ = true;
	update_pot_mappings(true);
}

void SequencerEngine::init_pot_functions() {
	pot_multi_function_.init();

	brain::ui::PotFunctionConfig bpm_cfg;
	bpm_cfg.function_id = POT_FUNCTION_ID_BPM;
	bpm_cfg.pot_index = POT_INDEX_BPM;
	bpm_cfg.min_value = 0;
	bpm_cfg.max_value = 255;
	bpm_cfg.initial_value = static_cast<uint8_t>((static_cast<uint32_t>(bpm_ - BPM_MIN) * 255u) / (BPM_MAX - BPM_MIN));
	bpm_cfg.mode = brain::ui::PotMode::kValueScale;
	bpm_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(bpm_cfg);

	brain::ui::PotFunctionConfig swing_cfg;
	swing_cfg.function_id = POT_FUNCTION_ID_SWING;
	swing_cfg.pot_index = POT_INDEX_BPM;
	swing_cfg.min_value = 0;
	swing_cfg.max_value = 255;
	swing_cfg.initial_value = 0;
	swing_cfg.mode = brain::ui::PotMode::kValueScale;
	swing_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(swing_cfg);

	brain::ui::PotFunctionConfig range_cfg;
	range_cfg.function_id = POT_FUNCTION_ID_RANGE;
	range_cfg.pot_index = POT_INDEX_RANGE_OR_QUANTIZATION;
	range_cfg.min_value = 0;
	range_cfg.max_value = 255;
	range_cfg.initial_value = static_cast<uint8_t>((static_cast<uint32_t>(range_octaves_) * 255u) / RANGE_OCTAVES_MAX);
	range_cfg.mode = brain::ui::PotMode::kValueScale;
	range_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(range_cfg);

	brain::ui::PotFunctionConfig quant_cfg;
	quant_cfg.function_id = POT_FUNCTION_ID_QUANTIZATION;
	quant_cfg.pot_index = POT_INDEX_RANGE_OR_QUANTIZATION;
	quant_cfg.min_value = 0;
	quant_cfg.max_value = 255;
	quant_cfg.initial_value = static_cast<uint8_t>((static_cast<uint32_t>(static_cast<uint8_t>(quantization_mode_)) * 255u) / (QUANTIZATION_MODE_COUNT - 1));
	quant_cfg.mode = brain::ui::PotMode::kValueScale;
	quant_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(quant_cfg);

	brain::ui::PotFunctionConfig randomness_cfg;
	randomness_cfg.function_id = POT_FUNCTION_ID_RANDOMNESS;
	randomness_cfg.pot_index = POT_INDEX_RANDOMNESS_OR_LENGTH;
	randomness_cfg.min_value = 0;
	randomness_cfg.max_value = 255;
	randomness_cfg.initial_value = randomness_pot_value_;
	randomness_cfg.mode = brain::ui::PotMode::kValueScale;
	randomness_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(randomness_cfg);

	brain::ui::PotFunctionConfig length_cfg;
	length_cfg.function_id = POT_FUNCTION_ID_LENGTH;
	length_cfg.pot_index = POT_INDEX_RANDOMNESS_OR_LENGTH;
	length_cfg.min_value = 0;
	length_cfg.max_value = 255;
	length_cfg.initial_value = static_cast<uint8_t>(
		((static_cast<uint32_t>(sequence_a_.length - SEQUENCE_LENGTH_MIN)) * 255u)
		/ static_cast<uint32_t>(SEQUENCE_LENGTH_MAX - SEQUENCE_LENGTH_MIN)
	);
	length_cfg.mode = brain::ui::PotMode::kValueScale;
	length_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(length_cfg);
}

void SequencerEngine::set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function) {
	const uint8_t active_functions[NUM_POTS] = {pot_0_function, pot_1_function, pot_2_function};
	pot_multi_function_.set_active_functions(active_functions, NUM_POTS);
	pot_multi_function_.update(pots_);
}

void SequencerEngine::update_pot_mappings(bool force_apply) {
	const bool shift_context_changed = (shift_active_ != last_shift_context_);
	if (shift_active_) {
		set_active_pot_functions(POT_FUNCTION_ID_SWING, POT_FUNCTION_ID_QUANTIZATION, POT_FUNCTION_ID_LENGTH);
	} else {
		set_active_pot_functions(POT_FUNCTION_ID_BPM, POT_FUNCTION_ID_RANGE, POT_FUNCTION_ID_RANDOMNESS);
	}

	if (shift_context_changed) {
		pot_multi_function_.clear_changed_flags();
		last_shift_context_ = shift_active_;
		if (!force_apply) {
			return;
		}
	}

	update_bpm_from_pot(force_apply);
	update_swing_from_pot1(force_apply);
	if (!root_edit_armed_) {
		update_range_or_quantization_from_pot2(force_apply);
		update_randomness_or_length_from_pot3(force_apply);
	}
	pot_multi_function_.clear_changed_flags();
}

void SequencerEngine::update_bpm_from_pot(bool force_apply) {
	if (!force_apply && !pot_multi_function_.get_changed(POT_FUNCTION_ID_BPM)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(POT_FUNCTION_ID_BPM);
	const bool was_external_sync_enabled = external_sync_enabled_;
	external_sync_enabled_ = (pot_value == 0u);
	if (external_sync_enabled_) {
		if (!was_external_sync_enabled) {
			// Entering external sync: stop internal scheduler state once.
			next_tick_due_us_ = 0;
			last_external_tick_us_ = 0;
			external_interval_us_ = tick_interval_us_;
			last_midi_event_us_ = 0;
			last_midi_clock_tick_us_ = 0;
			midi_interval_us_ = tick_interval_us_;
			midi_clock_ticks_since_step_ = 0;
			pending_midi_step_events_ = 0;
			midi_transport_running_ = false;
			external_clock_source_ = ExternalClockSource::kInternal;
			clear_external_swing_pending();
		}
		return;
	}

	const uint16_t span = static_cast<uint16_t>(BPM_MAX - BPM_MIN);
	const uint16_t mapped_bpm = static_cast<uint16_t>(BPM_MIN + ((static_cast<uint32_t>(pot_value) * span) / 255));

	bpm_ = mapped_bpm;
	tick_interval_us_ = 60000000u / (bpm_ * STEPS_PER_QUARTER_NOTE);
	current_step_interval_us_ = compute_next_step_interval_us();
	if (was_external_sync_enabled) {
		// Leaving external sync: restart internal scheduler from "now".
		next_tick_due_us_ = 0;
		external_clock_source_ = ExternalClockSource::kInternal;
		clear_external_swing_pending();
	}
}

void SequencerEngine::update_swing_from_pot1(bool force_apply) {
	if (!shift_active_) {
		return;
	}
	if (!force_apply && !pot_multi_function_.get_changed(POT_FUNCTION_ID_SWING)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(POT_FUNCTION_ID_SWING);
	swing_pot_value_ = pot_value;
	current_step_interval_us_ = compute_next_step_interval_us();
}

void SequencerEngine::update_range_or_quantization_from_pot2(bool force_apply) {
	const uint8_t function_id = shift_active_ ? POT_FUNCTION_ID_QUANTIZATION : POT_FUNCTION_ID_RANGE;
	if (!force_apply && !pot_multi_function_.get_changed(function_id)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(function_id);

	if (shift_active_) {
		const uint8_t mode_index = static_cast<uint8_t>((static_cast<uint32_t>(pot_value) * QUANTIZATION_MODE_COUNT) / 256u);
		quantization_mode_ = static_cast<QuantizationMode>(mode_index);
		return;
	}

	const uint8_t span = static_cast<uint8_t>(RANGE_OCTAVES_MAX - RANGE_OCTAVES_MIN + 1);
	const uint8_t mapped_range = static_cast<uint8_t>(
		RANGE_OCTAVES_MIN + ((static_cast<uint32_t>(pot_value) * span) / 256u)
	);
	range_octaves_ = mapped_range;
}

void SequencerEngine::update_randomness_or_length_from_pot3(bool force_apply) {
	const uint8_t function_id = shift_active_ ? POT_FUNCTION_ID_LENGTH : POT_FUNCTION_ID_RANDOMNESS;
	if (!force_apply && !pot_multi_function_.get_changed(function_id)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(function_id);

	if (shift_active_) {
		const uint8_t previous_length = sequence_a_.length;
		const uint8_t new_length = map_sequence_length_with_soft_snap(pot_value);
		if (new_length > previous_length) {
			for (uint8_t i = previous_length; i < new_length; ++i) {
				sequence_a_.steps[i].pitch_q8 = static_cast<uint16_t>(next_random(rng_state_a_) % (RANDOM_MAX_Q8 + 1u));
				sequence_a_.steps[i].gate = random_u8(rng_state_a_) >= 102u;
				sequence_b_steps_[i].pitch_q8 = static_cast<uint16_t>(next_random(rng_state_b_) % (RANDOM_MAX_Q8 + 1u));
				sequence_b_steps_[i].gate = random_u8(rng_state_b_) >= 102u;
			}
		}
		sequence_a_.length = new_length;
		if (sequence_a_.position >= sequence_a_.length) {
			sequence_a_.position = 0;
		}
		return;
	}

	randomness_pot_value_ = pot_value;
	mutation_threshold_ = randomness_pot_value_;
}

void SequencerEngine::check_root_edit_pot_input() {
	if (!root_edit_armed_) {
		return;
	}

	const uint8_t octave_raw = pot_raw_values_[POT_INDEX_RANGE_OR_QUANTIZATION];
	const uint8_t octave_delta = (octave_raw > root_edit_octave_pot_reference_raw_)
		? static_cast<uint8_t>(octave_raw - root_edit_octave_pot_reference_raw_)
		: static_cast<uint8_t>(root_edit_octave_pot_reference_raw_ - octave_raw);
	if (octave_delta >= ROOT_EDIT_POT_RAW_THRESHOLD) {
		on_root_edit_octave_pot_value(octave_raw);
	}

	const uint8_t note_raw = pot_raw_values_[POT_INDEX_RANDOMNESS_OR_LENGTH];
	const uint8_t note_delta = (note_raw > root_edit_note_pot_reference_raw_)
		? static_cast<uint8_t>(note_raw - root_edit_note_pot_reference_raw_)
		: static_cast<uint8_t>(root_edit_note_pot_reference_raw_ - note_raw);
	if (note_delta >= ROOT_EDIT_POT_RAW_THRESHOLD) {
		on_root_edit_note_pot_value(note_raw);
	}
}

void SequencerEngine::set_root_note_live(uint8_t root_note, bool update_reference) {
	const uint8_t new_root = static_cast<uint8_t>(root_note % ROOT_NOTE_COUNT);
	if (new_root == root_note_) {
		if (update_reference) {
			root_edit_note_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANDOMNESS_OR_LENGTH];
		}
		return;
	}

	root_note_ = new_root;
	refresh_output_after_root_change();
	if (update_reference) {
		root_edit_note_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANDOMNESS_OR_LENGTH];
	}
}

void SequencerEngine::set_octave_transpose_live(uint8_t octave) {
	const uint8_t new_octave = (octave > OCTAVE_TRANSPOSE_MAX) ? OCTAVE_TRANSPOSE_MAX : octave;
	if (new_octave == octave_transpose_) {
		root_edit_octave_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANGE_OR_QUANTIZATION];
		return;
	}

	octave_transpose_ = new_octave;
	refresh_output_after_root_change();
	root_edit_octave_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANGE_OR_QUANTIZATION];
	root_edit_octave_overlay_until_us_ = time_us_64() + POT_LED_OVERLAY_HOLD_US;
}

void SequencerEngine::refresh_output_after_root_change() {
	if (!initialized_ || sequence_a_.length == 0u) {
		return;
	}

	uint8_t step_index = sequence_a_.position;
	if (tick_counter_ > 0u) {
		step_index = static_cast<uint8_t>((sequence_a_.position + sequence_a_.length - 1u) % sequence_a_.length);
	}

	const Step& step_a = sequence_a_.steps[step_index];
	const Step& step_b = sequence_b_steps_[step_index];
	const uint16_t scaled_pitch_q8_a = apply_pitch_range(step_a.pitch_q8);
	const uint16_t scaled_pitch_q8_b = apply_pitch_range(step_b.pitch_q8);
	const uint16_t output_pitch_q8_a = quantize_pitch(scaled_pitch_q8_a);
	const uint16_t output_pitch_q8_b = quantize_pitch(scaled_pitch_q8_b);
	const float output_voltage_a = pitch_q8_to_voltage(output_pitch_q8_a);
	const float output_voltage_b = pitch_q8_to_voltage(output_pitch_q8_b);

	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, output_voltage_a);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, output_voltage_b);
	last_raw_voltage_ = pitch_q8_to_voltage(scaled_pitch_q8_a);
	last_quantized_voltage_ = output_voltage_a;
}

void SequencerEngine::persist_root_note_if_needed() {
	if (has_persisted_root_note_ && persisted_root_note_ == root_note_) {
		return;
	}

	if (save_persisted_root_note(root_note_)) {
		has_persisted_root_note_ = true;
		persisted_root_note_ = root_note_;
		return;
	}

	LOG_ERROR("SEQ", "failed to save root_note=%u to flash", static_cast<unsigned>(root_note_));
}

uint8_t SequencerEngine::map_sequence_length_with_soft_snap(uint8_t pot_value) const {
	static constexpr uint8_t SNAP_VALUES[] = {2, 4, 8, 16, 32};
	const uint8_t mapped = static_cast<uint8_t>(
		SEQUENCE_LENGTH_MIN
		+ ((static_cast<uint32_t>(pot_value) * (SEQUENCE_LENGTH_MAX - SEQUENCE_LENGTH_MIN)) / 255u)
	);

	uint8_t snapped = mapped;
	uint8_t best_distance = 255;
	bool has_snap_candidate = false;

	for (uint8_t i = 0; i < (sizeof(SNAP_VALUES) / sizeof(SNAP_VALUES[0])); ++i) {
		const uint8_t target = SNAP_VALUES[i];
		const uint8_t distance = (mapped > target) ? (mapped - target) : (target - mapped);
		uint8_t snap_window = static_cast<uint8_t>(target / 16u);
		if (snap_window < 1u) {
			snap_window = 1u;
		}

		if (distance <= snap_window && distance < best_distance) {
			best_distance = distance;
			snapped = target;
			has_snap_candidate = true;
		}
	}

	return has_snap_candidate ? snapped : mapped;
}

bool SequencerEngine::is_power_of_two_sequence_length(uint8_t length) const {
	return length > 0u && (length & static_cast<uint8_t>(length - 1u)) == 0u;
}

uint8_t SequencerEngine::sequence_length_led_count(uint8_t length) const {
	if (length <= 2u) return 1u;
	if (length <= 4u) return 2u;
	if (length <= 8u) return 3u;
	if (length <= 16u) return 4u;
	if (length <= 31u) return 5u;
	return 6u;
}

void SequencerEngine::apply_mutation_for_step(uint8_t step_index) {
	Step& step_a = sequence_a_.steps[step_index];
	Step& step_b = sequence_b_steps_[step_index];

	if (mutation_threshold_ == 0) {
		return;
	}

	if (random_u8(rng_state_a_) < mutation_threshold_) {
		step_a.pitch_q8 = static_cast<uint16_t>(next_random(rng_state_a_) % (RANDOM_MAX_Q8 + 1u));
	}

	if (random_u8(rng_state_b_) < mutation_threshold_) {
		step_b.pitch_q8 = static_cast<uint16_t>(next_random(rng_state_b_) % (RANDOM_MAX_Q8 + 1u));
	}

	if (random_u8(rng_state_a_) < mutation_threshold_) {
		// Keep the historical ~60% probability for HIGH gate after mutation.
		step_a.gate = random_u8(rng_state_a_) >= 102u;
	}
}

void SequencerEngine::update_pot_led_overlay(uint64_t now_us) {
	uint8_t moved_pot = NUM_POTS;
	uint8_t max_delta = 0;

	for (uint8_t pot_index = 0; pot_index < NUM_POTS; ++pot_index) {
		const uint8_t raw = pot_raw_values_[pot_index];
		const uint8_t previous = last_pot_raw_values_[pot_index];
		const uint8_t delta = (raw > previous) ? (raw - previous) : (previous - raw);
		if (delta > max_delta) {
			max_delta = delta;
			moved_pot = pot_index;
		}
		last_pot_raw_values_[pot_index] = raw;
	}

	if (moved_pot < NUM_POTS && max_delta >= POT_LED_ACTIVITY_RAW_THRESHOLD) {
		show_active_pot_overlay(moved_pot);
		pot_led_overlay_active_ = true;
		pot_led_overlay_last_change_us_ = now_us;
		return;
	}

	if (!pot_led_overlay_active_) {
		return;
	}

	if ((now_us - pot_led_overlay_last_change_us_) < POT_LED_OVERLAY_HOLD_US) {
		return;
	}

	pot_led_overlay_active_ = false;
	leds_.set_from_mask(gate_history_mask());
}

void SequencerEngine::show_active_pot_overlay(uint8_t pot_index) {
	if (root_edit_armed_ && pot_index == POT_INDEX_RANGE_OR_QUANTIZATION) {
		show_octave_transpose_overlay();
		return;
	}

	if (root_edit_armed_ && pot_index == POT_INDEX_RANDOMNESS_OR_LENGTH) {
		show_transpose_overlay();
		return;
	}

	if (shift_active_ && pot_index == POT_INDEX_RANDOMNESS_OR_LENGTH) {
		const uint8_t led_count = sequence_length_led_count(sequence_a_.length);
		const uint8_t brightness = is_power_of_two_sequence_length(sequence_a_.length)
			? 255u
			: POT_LED_SOFT_BRIGHTNESS;
		for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
			if (i < led_count) {
				leds_.set_brightness(i, brightness);
			} else {
				leds_.off(i);
			}
		}
		return;
	}

	const uint8_t mask = active_pot_led_mask(pot_index);
	leds_.set_from_mask(mask);
}

void SequencerEngine::show_transpose_overlay() {
	static constexpr uint8_t FULL_LED_COUNT[ROOT_NOTE_COUNT] = {
		0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6
	};
	static constexpr int8_t DIM_LED_INDEX[ROOT_NOTE_COUNT] = {
		-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1
	};

	const uint8_t note = static_cast<uint8_t>(root_note_ % ROOT_NOTE_COUNT);
	const uint8_t full_count = FULL_LED_COUNT[note];
	const int8_t dim_index = DIM_LED_INDEX[note];

	for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
		if (i < full_count) {
			leds_.set_brightness(i, 255u);
		} else {
			leds_.off(i);
		}
	}

	if (dim_index >= 0 && dim_index < static_cast<int8_t>(brain::ui::NO_OF_LEDS)) {
		leds_.set_brightness(static_cast<uint8_t>(dim_index), POT_LED_SOFT_BRIGHTNESS);
	}
}

void SequencerEngine::show_octave_transpose_overlay() {
	const uint8_t lit_count = static_cast<uint8_t>(octave_transpose_ + 1u);
	for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
		if (i < lit_count) {
			leds_.set_brightness(i, 255u);
		} else {
			leds_.off(i);
		}
	}
}

uint8_t SequencerEngine::active_pot_percent_255(uint8_t pot_index) const {
	uint32_t percent_255 = 0;

	switch (pot_index) {
		case POT_INDEX_BPM:
			if (shift_active_) {
				percent_255 = swing_pot_value_;
			} else {
				const uint16_t span = static_cast<uint16_t>(BPM_MAX - BPM_MIN);
				percent_255 = (span == 0) ? 0 : ((static_cast<uint32_t>(bpm_ - BPM_MIN) * 255u) / span);
			}
			break;

		case POT_INDEX_RANGE_OR_QUANTIZATION:
			if (shift_active_) {
				percent_255 =
					(static_cast<uint32_t>(static_cast<uint8_t>(quantization_mode_)) * 255u)
					/ static_cast<uint32_t>(QUANTIZATION_MODE_COUNT - 1);
			} else {
				percent_255 =
					(static_cast<uint32_t>(range_octaves_) * 255u) / static_cast<uint32_t>(RANGE_OCTAVES_MAX);
			}
			break;

			case POT_INDEX_RANDOMNESS_OR_LENGTH:
				if (shift_active_) {
					percent_255 =
						(static_cast<uint32_t>(sequence_a_.length - SEQUENCE_LENGTH_MIN) * 255u)
						/ static_cast<uint32_t>(SEQUENCE_LENGTH_MAX - SEQUENCE_LENGTH_MIN);
				} else {
					percent_255 = randomness_pot_value_;
				}
			break;

		default:
			percent_255 = 0;
			break;
	}

	if (percent_255 > 255u) {
		percent_255 = 255u;
	}
	return static_cast<uint8_t>(percent_255);
}

uint8_t SequencerEngine::active_pot_led_mask(uint8_t pot_index) const {
	// In shift+pot2 (quantization selection), show one-hot LED index 1..6.
	if (shift_active_ && pot_index == POT_INDEX_RANGE_OR_QUANTIZATION) {
		uint8_t mode_index = static_cast<uint8_t>(quantization_mode_);
		if (mode_index >= brain::ui::NO_OF_LEDS) {
			mode_index = brain::ui::NO_OF_LEDS - 1;
		}
		return static_cast<uint8_t>(1u << mode_index);
	}

	return percent_to_led_mask(active_pot_percent_255(pot_index));
}

uint8_t SequencerEngine::percent_to_led_mask(uint8_t percent_255) const {
	if (percent_255 == 0) {
		return 0;
	}
	uint8_t lit_count = static_cast<uint8_t>(
		(static_cast<uint32_t>(percent_255) * brain::ui::NO_OF_LEDS + 127u) / 255u
	);
	if (lit_count == 0) {
		lit_count = 1;
	}
	if (lit_count > brain::ui::NO_OF_LEDS) {
		lit_count = brain::ui::NO_OF_LEDS;
	}

	uint8_t mask = 0;
	for (uint8_t i = 0; i < lit_count; ++i) {
		mask |= static_cast<uint8_t>(1u << i);
	}
	return mask;
}

void SequencerEngine::reset_gate_history() {
	gate_history_fifo_.fill(0);
	refresh_gate_history_view();
	if (!pot_led_overlay_active_) {
		leds_.off_all();
	}
}

void SequencerEngine::push_gate_history(bool gate_high) {
	for (uint8_t i = 0; i < (brain::ui::NO_OF_LEDS - 1); ++i) {
		gate_history_fifo_[i] = gate_history_fifo_[i + 1];
	}
	gate_history_fifo_[brain::ui::NO_OF_LEDS - 1] = gate_high ? 1 : 0;
	refresh_gate_history_view();
	if (!pot_led_overlay_active_) {
		leds_.set_from_mask(gate_history_mask());
	}
}

void SequencerEngine::refresh_gate_history_view() {
	for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
		gate_history_text_[i] = gate_history_fifo_[i] ? '1' : '0';
	}
	gate_history_text_[brain::ui::NO_OF_LEDS] = '\0';
}

uint8_t SequencerEngine::gate_history_mask() const {
	uint8_t mask = 0;
	for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
		if (gate_history_fifo_[i] != 0) {
			mask |= static_cast<uint8_t>(1u << i);
		}
	}
	return mask;
}

void SequencerEngine::tick(uint64_t now_us) {
	const uint8_t step_index = sequence_a_.position;
	apply_mutation_for_step(step_index);
	const Step& step_a = sequence_a_.steps[step_index];
	const Step& step_b = sequence_b_steps_[step_index];
	const uint16_t scaled_pitch_q8_a = apply_pitch_range(step_a.pitch_q8);
	const uint16_t scaled_pitch_q8_b = apply_pitch_range(step_b.pitch_q8);
	const uint16_t output_pitch_q8_a = quantize_pitch(scaled_pitch_q8_a);
	const uint16_t output_pitch_q8_b = quantize_pitch(scaled_pitch_q8_b);
	const float output_voltage_a = pitch_q8_to_voltage(output_pitch_q8_a);
	const float output_voltage_b = pitch_q8_to_voltage(output_pitch_q8_b);

	if (step_a.gate) {
		dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, output_voltage_a);
		dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, output_voltage_b);
		last_raw_voltage_ = pitch_q8_to_voltage(scaled_pitch_q8_a);
		last_quantized_voltage_ = output_voltage_a;
		gate_.set(true);
		gate_active_ = true;
		gate_off_time_us_ = now_us + GATE_PULSE_US;
	} else {
		gate_.set(false);
		gate_active_ = false;
	}
	push_gate_history(step_a.gate);

	if ((tick_counter_ % STEPS_PER_QUARTER_NOTE) == 0) {
		button_led_.blink_duration(BUTTON_LED_BLINK_MS, BUTTON_LED_BLINK_INTERVAL_MS);
	}

	tick_counter_++;
	sequence_a_.position = static_cast<uint8_t>((step_index + 1) % sequence_a_.length);
}

void SequencerEngine::reset_transport() {
	playing_ = false;
	midi_transport_running_ = false;
	sequence_a_.position = 0;
	next_tick_due_us_ = 0;
	gate_off_time_us_ = 0;
	gate_active_ = false;
	tick_counter_ = 0;
	current_step_interval_us_ = tick_interval_us_;
	gate_.set(false);
	pot_led_overlay_active_ = false;
	pot_led_overlay_last_change_us_ = 0;
	last_pot_raw_values_ = pot_raw_values_;
	root_edit_octave_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANGE_OR_QUANTIZATION];
	root_edit_note_pot_reference_raw_ = pot_raw_values_[POT_INDEX_RANDOMNESS_OR_LENGTH];
	last_pulse_in_high_ = gate_.read();
	last_external_tick_us_ = 0;
	external_interval_us_ = tick_interval_us_;
	last_midi_clock_tick_us_ = 0;
	last_midi_event_us_ = 0;
	midi_interval_us_ = tick_interval_us_;
	midi_clock_ticks_since_step_ = 0;
	pending_midi_step_events_ = 0;
	external_clock_source_ = ExternalClockSource::kInternal;
	clear_external_swing_pending();
	reset_gate_history();
	button_led_.off();
}

uint32_t SequencerEngine::compute_swing_delta_us(uint32_t base_interval_us) const {
	if (swing_pot_value_ == 0u || base_interval_us == 0u) {
		return 0u;
	}

	// swing_delta = base * ((pot/255) * SWING_MAX), with SWING_MAX = 50%.
	const uint32_t swing_divisor = 255u * SWING_MAX_DENOMINATOR;
	const uint32_t swing_round = swing_divisor / 2u;
	return static_cast<uint32_t>(
		(static_cast<uint64_t>(base_interval_us) * swing_pot_value_ * SWING_MAX_NUMERATOR + swing_round) / swing_divisor
	);
}

uint32_t SequencerEngine::compute_external_source_timeout_us(uint32_t estimated_step_interval_us) const {
	uint32_t base_interval_us = estimated_step_interval_us;
	if (base_interval_us == 0u) {
		base_interval_us = tick_interval_us_;
	}

	uint64_t timeout_us = static_cast<uint64_t>(base_interval_us) * 3u;
	if (timeout_us < EXTERNAL_SOURCE_TIMEOUT_MIN_US) {
		timeout_us = EXTERNAL_SOURCE_TIMEOUT_MIN_US;
	}
	if (timeout_us > EXTERNAL_SOURCE_TIMEOUT_MAX_US) {
		timeout_us = EXTERNAL_SOURCE_TIMEOUT_MAX_US;
	}
	return static_cast<uint32_t>(timeout_us);
}

bool SequencerEngine::is_external_source_active(
	uint64_t now_us,
	uint64_t last_event_us,
	uint32_t estimated_step_interval_us
) const {
	if (last_event_us == 0) {
		return false;
	}

	if (now_us <= last_event_us) {
		return true;
	}

	const uint64_t event_age_us = now_us - last_event_us;
	const uint32_t timeout_us = compute_external_source_timeout_us(estimated_step_interval_us);
	return event_age_us <= timeout_us;
}

uint32_t SequencerEngine::compute_next_step_interval_us() const {
	const uint32_t base = tick_interval_us_;
	const uint32_t swing_delta = compute_swing_delta_us(base);
	if (swing_delta == 0u) {
		return base;
	}

	if ((tick_counter_ & 1u) == 0u) {
		return (base > swing_delta) ? (base - swing_delta) : 1u;
	}
	return base + swing_delta;
}

uint16_t SequencerEngine::apply_pitch_range(uint16_t source_q8) const {
	if (range_octaves_ == 0) {
		return 0;
	}

	uint32_t scaled = (static_cast<uint32_t>(source_q8) * range_octaves_ + (SOURCE_RANGE_OCTAVES / 2u))
		/ SOURCE_RANGE_OCTAVES;
	const uint32_t max_q8 = static_cast<uint32_t>(range_octaves_) * SEMITONES_PER_OCTAVE * PITCH_Q8_PER_SEMITONE;
	if (scaled > max_q8) {
		scaled = max_q8;
	}
	return static_cast<uint16_t>(scaled);
}

uint16_t SequencerEngine::quantize_pitch(uint16_t pitch_q8) const {
	const uint32_t source_max_q8 = static_cast<uint32_t>(range_octaves_) * SEMITONES_PER_OCTAVE * PITCH_Q8_PER_SEMITONE;
	const uint16_t source_pitch_q8 = (pitch_q8 > source_max_q8) ? static_cast<uint16_t>(source_max_q8) : pitch_q8;

	static constexpr uint8_t CHROMATIC[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	static constexpr uint8_t MAJOR[] = {0, 2, 4, 5, 7, 9, 11};
	static constexpr uint8_t MINOR[] = {0, 2, 3, 5, 7, 8, 10};
	static constexpr uint8_t PENTATONIC[] = {0, 2, 4, 7, 9};
	static constexpr uint8_t EXTRA[] = {0, 1, 4, 5, 7, 8, 11};

	const uint8_t* scale = CHROMATIC;
	uint8_t scale_size = sizeof(CHROMATIC);

	switch (quantization_mode_) {
		case QuantizationMode::kChromatic:
			scale = CHROMATIC;
			scale_size = sizeof(CHROMATIC);
			break;
		case QuantizationMode::kMajor:
			scale = MAJOR;
			scale_size = sizeof(MAJOR);
			break;
		case QuantizationMode::kMinor:
			scale = MINOR;
			scale_size = sizeof(MINOR);
			break;
		case QuantizationMode::kPentatonic:
			scale = PENTATONIC;
			scale_size = sizeof(PENTATONIC);
			break;
			case QuantizationMode::kExtra:
				scale = EXTRA;
				scale_size = sizeof(EXTRA);
				break;
			case QuantizationMode::kUnquantized:
				break;
			default:
				scale = CHROMATIC;
				scale_size = sizeof(CHROMATIC);
				break;
		}

	int32_t quantized_q8 = static_cast<int32_t>(source_pitch_q8);
	if (quantization_mode_ != QuantizationMode::kUnquantized) {
		const int32_t rounded = static_cast<int32_t>((static_cast<uint32_t>(source_pitch_q8) + (PITCH_Q8_PER_SEMITONE / 2u))
			/ PITCH_Q8_PER_SEMITONE);
		const int32_t octave_center = rounded / 12;

		int32_t best_distance = 0x7FFFFFFF;
		int32_t best_semitone = rounded;

		for (int32_t octave = octave_center - 1; octave <= octave_center + 1; ++octave) {
			for (uint8_t i = 0; i < scale_size; ++i) {
				const int32_t candidate = (octave * 12) + static_cast<int32_t>(scale[i]);
				const int32_t candidate_q8 = candidate * static_cast<int32_t>(PITCH_Q8_PER_SEMITONE);
				const int32_t diff = candidate_q8 - static_cast<int32_t>(source_pitch_q8);
				const int32_t distance = (diff < 0) ? -diff : diff;
				if (distance < best_distance) {
					best_distance = distance;
					best_semitone = candidate;
				}
			}
		}

		quantized_q8 = best_semitone * static_cast<int32_t>(PITCH_Q8_PER_SEMITONE);
	}

	const int32_t transpose_q8 = (static_cast<int32_t>(octave_transpose_) * static_cast<int32_t>(SEMITONES_PER_OCTAVE)
		+ static_cast<int32_t>(root_note_)) * static_cast<int32_t>(PITCH_Q8_PER_SEMITONE);
	int32_t transposed_q8 = quantized_q8 + transpose_q8;
	static constexpr int32_t output_max_q8 = 10 * static_cast<int32_t>(SEMITONES_PER_OCTAVE)
		* static_cast<int32_t>(PITCH_Q8_PER_SEMITONE);
	if (transposed_q8 < 0) {
		transposed_q8 = 0;
	}
	if (transposed_q8 > output_max_q8) {
		transposed_q8 = output_max_q8;
	}
	return static_cast<uint16_t>(transposed_q8);
}

const char* SequencerEngine::quantization_mode_to_string(QuantizationMode mode) {
	switch (mode) {
		case QuantizationMode::kUnquantized:
			return "Unquantized";
		case QuantizationMode::kChromatic:
			return "Chromatic";
		case QuantizationMode::kMajor:
			return "Major";
		case QuantizationMode::kMinor:
			return "Minor";
		case QuantizationMode::kPentatonic:
			return "Pentatonic";
		case QuantizationMode::kExtra:
			return "Extra";
		default:
			return "Unknown";
	}
}

const char* SequencerEngine::root_note_to_string(uint8_t root_note) {
	static constexpr const char* ROOT_NAMES[ROOT_NOTE_COUNT] = {
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};
	return ROOT_NAMES[root_note % ROOT_NOTE_COUNT];
}

uint32_t SequencerEngine::next_random(uint32_t& state) {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

uint8_t SequencerEngine::random_u8(uint32_t& state) {
	return static_cast<uint8_t>(next_random(state) >> 24);
}

float SequencerEngine::pitch_q8_to_voltage(uint16_t pitch_q8) {
	return static_cast<float>(pitch_q8) / static_cast<float>(SEMITONES_PER_OCTAVE * PITCH_Q8_PER_SEMITONE);
}

uint16_t SequencerEngine::tempo_bpm() const {
	return bpm_;
}

bool SequencerEngine::is_playing() const {
	return playing_;
}

bool SequencerEngine::external_sync_enabled() const {
	return external_sync_enabled_;
}

ExternalClockSource SequencerEngine::external_clock_source() const {
	return external_clock_source_;
}

bool SequencerEngine::midi_transport_running() const {
	return midi_transport_running_;
}

float SequencerEngine::swing() const {
	return static_cast<float>(static_cast<uint16_t>(swing_pot_value_) * SWING_MAX_NUMERATOR)
		/ static_cast<float>(255u * SWING_MAX_DENOMINATOR);
}

float SequencerEngine::randomness() const {
	return static_cast<float>(mutation_threshold_) / 255.0f;
}

uint8_t SequencerEngine::sequence_length() const {
	return sequence_a_.length;
}

uint8_t SequencerEngine::range_octaves() const {
	return range_octaves_;
}

const char* SequencerEngine::quantization_mode_name() const {
	return quantization_mode_to_string(quantization_mode_);
}

const char* SequencerEngine::root_note_name() const {
	return root_note_to_string(root_note_);
}

uint8_t SequencerEngine::octave_transpose() const {
	return octave_transpose_;
}

bool SequencerEngine::root_edit_armed() const {
	return root_edit_armed_;
}

float SequencerEngine::last_raw_voltage() const {
	return last_raw_voltage_;
}

float SequencerEngine::last_quantized_voltage() const {
	return last_quantized_voltage_;
}

uint32_t SequencerEngine::base_interval_us() const {
	return tick_interval_us_;
}

uint32_t SequencerEngine::current_interval_us() const {
	return current_step_interval_us_;
}

const char* SequencerEngine::gate_history() const {
	return gate_history_text_;
}
