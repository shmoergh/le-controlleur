#ifndef SEQUENCER_ENGINE_H
#define SEQUENCER_ENGINE_H

#include <array>
#include <cstdint>

#include "brain-io/audio-cv-out.h"
#include "brain-io/pulse.h"
#include "brain-ui/pots.h"

struct Step {
	float voltage;
	bool gate;
};

struct Sequence {
	static constexpr uint8_t kMaxSteps = 64;
	std::array<Step, kMaxSteps> steps;
	uint8_t length;
	uint8_t position;
};

class SequencerEngine {
public:
	SequencerEngine();
	void update();
	void on_mode_enter();
	void on_mode_exit();
	void on_button_a_short_press();

private:
	static constexpr uint8_t POT_INDEX_BPM = 0;
	static constexpr uint16_t BPM_MIN = 30;
	static constexpr uint16_t BPM_MAX = 240;
	static constexpr uint32_t GATE_PULSE_US = 20000;

	Sequence sequence_;
	brain::ui::Pots pots_;
	brain::io::AudioCvOut dac_;
	brain::io::Pulse gate_;

	bool initialized_;
	bool playing_;
	bool gate_active_;
	uint16_t bpm_;
	uint32_t tick_interval_us_;
	uint64_t last_tick_time_us_;
	uint64_t gate_off_time_us_;

	void init_sequence();
	void init_io();
	void update_bpm_from_pot(bool force_log = false);
	void tick(uint64_t now_us);
	void reset_transport();
};

#endif
