#include "sequencer-engine.h"

#include "pico/time.h"

#include "brain-ui/pots.h"
#include "debug-log.h"

SequencerEngine::SequencerEngine() :
	initialized_(false),
	playing_(false),
	gate_active_(false),
	bpm_(120),
	tick_interval_us_(500000),
	last_tick_time_us_(0),
	gate_off_time_us_(0) {
	init_sequence();
	init_io();
}

void SequencerEngine::update() {
	if (!initialized_) {
		return;
	}

	update_bpm_from_pot();

	const uint64_t now_us = time_us_64();

	if (gate_active_ && now_us >= gate_off_time_us_) {
		gate_.set(false);
		gate_active_ = false;
	}

	if (!playing_) {
		return;
	}

	if (last_tick_time_us_ == 0 || (now_us - last_tick_time_us_) >= tick_interval_us_) {
		tick(now_us);
	}
}

void SequencerEngine::on_mode_enter() {
	if (!initialized_) {
		return;
	}

	reset_transport();
	update_bpm_from_pot(true);
	LOG_INFO("SEQ", "init length=%u bpm=%u", sequence_.length, bpm_);
}

void SequencerEngine::on_mode_exit() {
	if (!initialized_) {
		return;
	}

	reset_transport();
}

void SequencerEngine::on_button_a_short_press() {
	if (!initialized_) {
		return;
	}

	playing_ = !playing_;

	if (playing_) {
		last_tick_time_us_ = 0;
		LOG_INFO("SEQ", "transport=PLAY");
	} else {
		gate_.set(false);
		gate_active_ = false;
		LOG_INFO("SEQ", "transport=PAUSE");
	}
}

void SequencerEngine::init_sequence() {
	sequence_.steps.fill({0.0f, false});
	sequence_.length = 8;
	sequence_.position = 0;

	// Deterministic bring-up pattern.
	sequence_.steps[0] = {0.0f, true};
	sequence_.steps[1] = {0.5f, false};
	sequence_.steps[2] = {1.0f, true};
	sequence_.steps[3] = {1.5f, false};
	sequence_.steps[4] = {2.0f, true};
	sequence_.steps[5] = {2.5f, false};
	sequence_.steps[6] = {3.0f, true};
	sequence_.steps[7] = {3.5f, true};
}

void SequencerEngine::init_io() {
	brain::ui::PotsConfig pots_config = brain::ui::create_default_config();
	pots_config.simple = true;
	pots_config.output_resolution = 8;
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

	initialized_ = true;
	update_bpm_from_pot(true);
}

void SequencerEngine::update_bpm_from_pot(bool force_log) {
	const uint8_t pot_value = pots_.get(POT_INDEX_BPM);
	const uint16_t span = static_cast<uint16_t>(BPM_MAX - BPM_MIN);
	const uint16_t mapped_bpm = static_cast<uint16_t>(BPM_MIN + ((static_cast<uint32_t>(pot_value) * span) / 255));

	if (!force_log && mapped_bpm == bpm_) {
		return;
	}

	bpm_ = mapped_bpm;
	tick_interval_us_ = 60000000u / bpm_;
	LOG_INFO("SEQ", "bpm=%u", bpm_);
}

void SequencerEngine::tick(uint64_t now_us) {
	last_tick_time_us_ = now_us;

	const uint8_t step_index = sequence_.position;
	const Step& step = sequence_.steps[step_index];

	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, step.voltage);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, 0.0f);

	if (step.gate) {
		gate_.set(true);
		gate_active_ = true;
		gate_off_time_us_ = now_us + GATE_PULSE_US;
	} else {
		gate_.set(false);
		gate_active_ = false;
	}

	LOG_INFO(
		"CLK",
		"tick step=%u/%u bpm=%u voltage=%.2f gate=%u",
		static_cast<unsigned>(step_index + 1),
		static_cast<unsigned>(sequence_.length),
		static_cast<unsigned>(bpm_),
		step.voltage,
		step.gate ? 1u : 0u
	);

	sequence_.position = static_cast<uint8_t>((step_index + 1) % sequence_.length);
}

void SequencerEngine::reset_transport() {
	playing_ = false;
	sequence_.position = 0;
	last_tick_time_us_ = 0;
	gate_off_time_us_ = 0;
	gate_active_ = false;
	gate_.set(false);
}
