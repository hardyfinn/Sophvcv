// SophieEngine.hpp - the Sophie percussion voice, with no VCV Rack dependencies.
//
// A C++ port of the DSP in mestela/schwung-sophie (src/dsp/sophie.c, v0.6.0,
// MIT). The sound code is kept line for line so the VCV module sounds the same
// as the Ableton Move version: test/engine_test.cpp renders both and compares
// them sample by sample. Changes for VCV Rack:
//   - any sample rate (the Ring delay buffer is sized for up to 192 kHz)
//   - each voice reports which pad it belongs to, for the per-pad poly output
//   - velocity is a float, so CV can set it
//
// Pipe, Boom and the smooth-feedback / brightness ideas in the original are
// adapted from soejrd/digisophie (MIT). See LICENSE.txt.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace sophie {

static const float PI = 3.14159265358979323846f;
static const float TAU = 2.0f * PI;
static const int VOICES = 12;
static const int PADS = 16;
static const int FIRST_NOTE = 36;
// 30 ms of Ring time at 192 kHz is 5760 samples.
static const int MAX_DELAY = 6144;

enum Model { FUSE, STACK, SPLIT, SHARD, PIPE, BOOM, NUM_MODELS };
enum FilterType { LPF, HPF, NOTCH, DJ, BPF, NUM_FILTERS };

struct Patch {
	int model = 0;
	float tune = 0.f;       // semitones, -24..24
	float color = 0.f;      // 0..100
	float metal = 0.f;      // 0..100
	float feedback = 0.f;   // 0..100
	float sweep = 0.f;      // -100..100
	float decay = 0.3f;     // seconds, 0.03..4
	float crush = 0.f;      // 0..100
	float drive = 0.f;      // 0..100
	float level = 100.f;    // 0..100
	float cutoff = 100.f;   // 0..100
	float resonance = 0.f;  // 0..100
	int filter_type = 0;
	float ring_time = 6.f;      // ms, 0.5..30
	float ring_feedback = 0.f;  // 0..95
	float ring_mix = 0.f;       // 0..100
	float ring_tone = 70.f;     // 0..100
	float metal_decay = 0.f;    // ms, 0 keeps the original pitch-linked contour
	int feedback_mode = 0;      // 0 Raw, 1 Smooth
	int tail_mode = 0;          // 0 Hit, 1 Live
};

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline float wrap_phase(float x) {
	if (x >= TAU) x -= TAU;
	if (x < 0.0f) x += TAU;
	return x;
}
inline float wrap_continuous(float x) { return x - std::floor(x / TAU) * TAU; }
inline float soft_clip(float x) { return x / (1.0f + std::fabs(x)); }

inline uint32_t xorshift32(uint32_t* state) {
	uint32_t x = *state ? *state : 0x6d2b79f5u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}
inline float noise_bipolar(uint32_t* state) {
	return (float)(xorshift32(state) & 0x00ffffffu) * (2.0f / 16777215.0f) - 1.0f;
}
inline float midi_hz(int note) { return 440.0f * std::exp2(((float)note - 69.0f) / 12.0f); }

inline Patch makePatch(int model, float tune, float color, float metal, float feedback,
                       float sweep, float decay, float crush, float drive) {
	Patch p;
	p.model = model; p.tune = tune; p.color = color; p.metal = metal; p.feedback = feedback;
	p.sweep = sweep; p.decay = decay; p.crush = crush; p.drive = drive;
	return p;
}

// The factory kit, in Ableton Drum Rack / General MIDI order (notes 36-51).
inline Patch factoryPatch(int pad) {
	switch (pad) {
		case 0:  return makePatch(0, -5, 24,  8,  6,  55, 0.28f,  0, 58); // Kick
		case 1:  return makePatch(3,  0, 76, 72, 34,  16, 0.09f, 48, 58); // Rim
		case 2:  return makePatch(3,  0, 52, 76, 18,  28, 0.24f, 30, 52); // Snare
		case 3:  return makePatch(2,  0, 84, 88, 42,  -8, 0.19f, 56, 68); // Clap
		case 4:  return makePatch(1,  0, 63, 82, 28,  24, 0.18f, 38, 58); // Snare 2
		case 5:  return makePatch(0,  0, 24, 44, 16,  58, 0.42f,  8, 48); // Low Tom
		case 6:  return makePatch(3,  0, 94, 98, 58,   0, 0.055f, 52, 62); // Closed HH
		case 7:  return makePatch(0,  0, 31, 48, 20,  54, 0.47f, 10, 50); // Floor Tom
		case 8:  return makePatch(3,  0, 88, 94, 50,   2, 0.10f, 46, 58); // Pedal HH
		case 9:  return makePatch(0,  0, 38, 52, 22,  48, 0.36f, 12, 52); // Mid Tom
		case 10: return makePatch(3,  0, 91, 96, 62,  -4, 0.48f, 42, 60); // Open HH
		case 11: return makePatch(0,  0, 45, 56, 25,  44, 0.31f, 14, 54); // Low-mid Tom
		case 12: return makePatch(0,  0, 51, 60, 28,  40, 0.27f, 16, 56); // High-mid Tom
		case 13: return makePatch(1,  0, 78, 92, 72, -12, 1.65f, 28, 54); // Crash
		case 14: return makePatch(0,  0, 58, 64, 30,  36, 0.23f, 18, 58); // High Tom
		default: return makePatch(2,  0, 69, 86, 66,  -6, 1.15f, 22, 52); // Ride
	}
}

inline const char* padName(int pad) {
	static const char* names[PADS] = {"Kick", "Rim", "Snare", "Clap", "Snare 2", "Low Tom",
		"Closed HH", "Floor Tom", "Pedal HH", "Mid Tom", "Open HH", "Low-Mid Tom",
		"High-Mid Tom", "Crash", "High Tom", "Ride"};
	return names[std::max(0, std::min(PADS - 1, pad))];
}

struct Voice {
	float carrier_phase, mod1_phase, mod2_phase, mod3_phase;
	float sub_phase, upper_phase, split_phase;
	float metal_env, metal_mul, amp_mul, pitch_mul;
	float color_s, metal_s, feedback_s, cutoff_s, resonance_s;
	float last, steal_tail;
	int steal_left;
	float amp_env, pitch_env, attack;
	float feedback_z;
	float filter_low, filter_band;
	float noise_z;
	float base_hz;
	float velocity;
	float crush_hold;
	int crush_count;
	float delay_buf[MAX_DELAY];
	float delay_lp;
	int delay_pos;
	int pad_index;
	Patch patch;
	uint32_t age;
	uint32_t rng;
	int active;
};

struct Engine {
	Voice voices[VOICES];
	float sample_rate = 44100.f;
	float control_slew = 0.f, feedback_slew = 0.f;
	int steal_samples = 1;
	uint32_t age_counter = 0;
	uint32_t rng = 0x534f5048u;
	// The sound each pad plays. Live tails follow these while they ring, so the
	// module writes the knob + CV values here every sample.
	Patch pads[PADS];

	Engine() {
		std::memset((void*)voices, 0, sizeof(voices));
		for (int i = 0; i < PADS; ++i) pads[i] = factoryPatch(i);
		setSampleRate(44100.f);
	}

	void setSampleRate(float sr) {
		sample_rate = std::max(1000.f, sr);
		control_slew = 1.0f - std::exp(-1.0f / (0.005f * sample_rate));
		feedback_slew = 1.0f - std::exp(std::log(0.875f) * 24000.0f / sample_rate);
		steal_samples = std::max(1, (int)(0.0015f * sample_rate));
	}

	void reset() {
		std::memset((void*)voices, 0, sizeof(voices));
		age_counter = 0;
		rng = 0x534f5048u;
	}

	int activeVoices() const {
		int n = 0;
		for (int i = 0; i < VOICES; ++i) n += voices[i].active ? 1 : 0;
		return n;
	}

	Voice* allocate_voice() {
		Voice* oldest = &voices[0];
		for (int i = 0; i < VOICES; ++i) {
			if (!voices[i].active) return &voices[i];
			if (voices[i].age < oldest->age) oldest = &voices[i];
		}
		return oldest;
	}

	// Play pad 0..15. velocity is 0..1 (MIDI velocity / 127).
	void trigger(int pad_index, float velocity) {
		if (pad_index < 0 || pad_index >= PADS) return;
		int note = FIRST_NOTE + pad_index;
		int vel127 = (int)std::lround(clampf(velocity, 0.f, 1.f) * 127.f);
		Voice* v = allocate_voice();
		float tail = v->active ? v->last : 0.0f;
		int stolen = v->active;
		std::memset((void*)v, 0, sizeof(*v));
		v->steal_tail = tail;
		v->steal_left = stolen ? steal_samples : 0;
		v->active = 1;
		v->age = ++age_counter;
		v->velocity = clampf(velocity, 0.01f, 1.0f);
		v->patch = pads[pad_index];
		v->pad_index = pad_index;
		v->color_s = v->patch.color; v->metal_s = v->patch.metal;
		v->feedback_s = v->patch.feedback; v->cutoff_s = v->patch.cutoff;
		v->resonance_s = v->patch.resonance;
		int legacy_kick = pad_index == 0 && v->patch.model < 4;
		float pitch_time = legacy_kick ? 0.075f : 0.028f;
		v->amp_mul = std::exp(std::log(0.00012f) / (clampf(v->patch.decay, 0.03f, 4) * sample_rate));
		v->pitch_mul = std::exp(-1.0f / (pitch_time * sample_rate));
		float metal_ms = v->patch.metal_decay > 0 ? std::max(5.0f, v->patch.metal_decay) : 135.0f;
		v->metal_mul = std::exp(-1.0f / (metal_ms * 0.001f * sample_rate));
		v->metal_env = 1.0f;
		v->base_hz = clampf(midi_hz(note) * std::exp2(v->patch.tune / 12.0f), 20.0f, 10000.0f);
		v->amp_env = 1.0f;
		v->pitch_env = 1.0f;
		v->attack = 0.0f;
		v->rng = xorshift32(&rng) ^ ((uint32_t)note << 16) ^ (uint32_t)vel127;
		// Slight phase variation retains impact while avoiding machine-gun repeats.
		v->mod2_phase = noise_bipolar(&v->rng) * 0.12f;
		v->mod3_phase = noise_bipolar(&v->rng) * 0.08f;
	}

	float render_voice(Voice* v) {
		if (!v->active) return 0.0f;
		const Patch* p = &v->patch;

		int legacy_kick = v->pad_index == 0 && p->model < 4;
		int shaped = p->metal_decay > 0 || p->model >= 4;
		int smooth = p->feedback_mode == 1;
		if (p->tail_mode == 1) {
			const Patch* target = &pads[v->pad_index];
			v->color_s += (target->color - v->color_s) * control_slew;
			v->metal_s += (target->metal - v->metal_s) * control_slew;
			v->feedback_s += (target->feedback - v->feedback_s) * control_slew;
			v->cutoff_s += (target->cutoff - v->cutoff_s) * control_slew;
			v->resonance_s += (target->resonance - v->resonance_s) * control_slew;
		}
		v->amp_env *= v->amp_mul;
		v->pitch_env *= v->pitch_mul;
		v->metal_env *= v->metal_mul;
		if (v->metal_env < 0.000001f) v->metal_env = 0.0f;
		v->attack += (1.0f - v->attack) * 0.085f;
		if (v->amp_env < 0.00012f) {
			v->active = 0;
			v->last = 0.f;
			return 0.0f;
		}

		float color = v->color_s * 0.01f;
		float metal = v->metal_s * 0.01f;
		float feedback = v->feedback_s * 0.01f;
		if (smooth) feedback *= feedback;
		float brightness = shaped ? v->metal_env : v->pitch_env;
		float sweep_range = legacy_kick ? 2.5f : 4.5f;
		float sweep_oct = p->sweep * 0.01f * sweep_range * v->pitch_env;
		float hz = clampf(v->base_hz * std::exp2(sweep_oct), 18.0f, 18000.0f);

		// Deliberately non-integer ratios: Color moves through useful clang zones.
		float r1 = 0.47f + color * color * 7.6f;
		if (p->model == 4) r1 = 1.31f + color * 6.0f;
		if (p->model == 5) r1 = 0.60f + color * 4.2f;
		float r2 = 1.31f + color * 11.17f;
		float r3 = 2.07f + (1.0f - color) * 15.73f;
		float inc = TAU * hz / sample_rate;

		v->carrier_phase = wrap_phase(v->carrier_phase + inc);
		v->mod1_phase = wrap_phase(v->mod1_phase + inc * r1);
		v->mod2_phase = wrap_phase(v->mod2_phase + inc * r2);
		v->mod3_phase = wrap_phase(v->mod3_phase + inc * r3);

		if (smooth || p->model >= 4) {
			v->carrier_phase = wrap_continuous(v->carrier_phase);
			v->mod1_phase = wrap_continuous(v->mod1_phase);
			v->mod2_phase = wrap_continuous(v->mod2_phase);
			v->mod3_phase = wrap_continuous(v->mod3_phase);
		}
		if (smooth) {
			v->sub_phase = wrap_continuous(v->sub_phase + inc * (legacy_kick ? 0.503f : 0.501f));
			v->upper_phase = wrap_continuous(v->upper_phase + inc * (legacy_kick ? 2.01f : 1.997f));
			v->split_phase = wrap_continuous(v->split_phase + inc * 1.003f);
		}
		float fb = v->feedback_z * feedback * (2.0f + metal * 10.0f);
		float m1 = std::sin(v->mod1_phase + fb);
		float m2 = std::sin(v->mod2_phase + m1 * metal * 2.5f);
		float m3 = std::sin(v->mod3_phase);
		float index = metal * metal * (2.0f + 24.0f * (0.25f + brightness * 0.75f));
		if (smooth) {
			// Heuristic FM bandwidth ceiling, not an alias-free guarantee.
			float bandwidth = std::max(100.0f, sample_rate * 0.45f - hz);
			float rate = hz * r1 * (1.0f + metal * 2.5f);
			index = std::min(index, bandwidth / std::max(rate, 1.0f));
		}
		float out;

		if (legacy_kick) {
			// A low sine body owns the pitch and weight; folded/FM material is a
			// quiet parallel texture.
			float body = std::sin(v->carrier_phase);
			float folded = std::sin(body * (1.0f + color * 2.2f));
			float texture = std::sin(v->carrier_phase + m2 * (0.18f + feedback * 1.35f));
			float n = noise_bipolar(&v->rng);
			v->noise_z += (n - v->noise_z) * 0.32f;
			float click = v->noise_z * v->pitch_env;
			out = body * (0.86f - color * 0.08f)
				+ folded * (0.14f + color * 0.08f)
				+ texture * metal * 0.18f * (shaped ? brightness : 1.0f)
				+ click * (0.012f + metal * 0.035f);
			if (p->model == 1)
				out += 0.16f * std::sin((smooth ? v->upper_phase : v->carrier_phase * 2.01f) + m1 * (0.4f + metal * 2.0f));
			else if (p->model == 2)
				out += 0.14f * std::sin((smooth ? v->sub_phase : v->carrier_phase * 0.503f) - m2 * (0.3f + metal * 1.5f));
			else if (p->model == 3) {
				float stair = std::floor((v->carrier_phase / TAU) * 24.0f) * (TAU / 24.0f);
				out += 0.12f * std::sin(stair + m1 * (0.5f + metal * 2.0f));
			}
		}
		else switch (p->model) {
			default: // Fuse: two modulators in series, highly responsive feedback.
				out = std::sin(v->carrier_phase + index * (m2 * 0.72f + m1 * 0.28f));
				break;
			case 1: // Stack: dense additive/FM stack.
				out = std::sin(v->carrier_phase + index * 0.62f * m1)
					+ 0.55f * std::sin((smooth ? v->sub_phase : v->carrier_phase * 0.501f) + index * 0.35f * m2)
					+ 0.32f * std::sin((smooth ? v->upper_phase : v->carrier_phase * 1.997f) + index * 0.22f * m3);
				out *= 0.56f;
				break;
			case 2: // Split: parallel operators with asymmetric modulation.
				out = 0.62f * std::sin(v->carrier_phase + index * m1)
					+ 0.38f * std::sin((smooth ? v->split_phase : v->carrier_phase * 1.003f) - index * 0.71f * m2)
					+ 0.18f * m3;
				break;
			case 4: { // Pipe: FM carrier mixed with an inharmonic ring.
				float body = std::sin(v->carrier_phase + index * m1);
				float ring_mix = 0.3125f + metal * 0.5f + brightness * 0.125f;
				out = (body * (1.0f - ring_mix) + body * m1 * ring_mix) * 1.25f;
				break;
			}
			case 5: { // Boom: clean fundamental with a receding metallic skin.
				float body = std::sin(v->carrier_phase);
				float edge = std::sin(v->carrier_phase + index * m1);
				float edge_mix = 0.0625f + metal * brightness * 0.5f;
				out = body * (1.0f - edge_mix) + edge * edge_mix;
				break;
			}
			case 3: { // Shard: 12-bit wavetable/noise hybrid.
				float stair = std::floor((v->carrier_phase / TAU) * 32.0f) * (TAU / 32.0f);
				float n = noise_bipolar(&v->rng);
				v->noise_z += (n - v->noise_z) * (0.02f + color * 0.35f);
				out = std::sin(stair + index * 0.55f * m1) * (0.85f - metal * 0.25f)
					+ v->noise_z * metal * 0.42f * (shaped ? brightness : 1.0f);
				break;
			}
		}
		float feedback_out = soft_clip(out * 1.4f);
		v->feedback_z = smooth ? v->feedback_z + (feedback_out - v->feedback_z) * feedback_slew : feedback_out;
		out *= v->amp_env * v->attack;

		out *= p->level * 0.01f;

		float crush = p->crush * 0.01f;
		if (crush >= 0.005f) {
			int rate_div = 1 + (int)(crush * crush * 31.0f);
			if (v->crush_count <= 0) {
				v->crush_hold = out;
				v->crush_count = rate_div;
			}
			out = v->crush_hold;
			v->crush_count--;
			float levels = std::exp2(15.0f - crush * 12.0f);
			out = std::floor(out * levels + 0.5f) / levels;
		}
		float drive = clampf(p->drive * 0.01f, 0.0f, 1.0f);
		float drive_units = drive <= 0.5f
			? drive * 100.0f
			: 50.0f + 250.0f * std::pow((drive - 0.5f) * 2.0f, 1.5f);
		float drive_gain = 1.0f + drive_units * 0.075f;
		float norm = soft_clip(drive_gain);
		if (drive > 0.5f) {
			float t = (drive - 0.5f) * 2.0f;
			norm += (1.0f - norm) * 0.72f * t;
		}
		float dry = (soft_clip(out * drive_gain) / norm) * v->velocity;

		// Per-voice short feedback delay: a resonant comb under 10 ms.
		float delay_samples = p->ring_time * 0.001f * sample_rate;
		delay_samples = clampf(delay_samples, 1.0f, (float)(MAX_DELAY - 2));
		float read = (float)v->delay_pos - delay_samples;
		while (read < 0.0f) read += (float)MAX_DELAY;
		int r0 = (int)read;
		int rnext = (r0 + 1) % MAX_DELAY;
		float frac = read - (float)r0;
		float delayed = v->delay_buf[r0] * (1.0f - frac) + v->delay_buf[rnext] * frac;
		float tone = 0.02f + p->ring_tone * 0.0098f;
		v->delay_lp += tone * (delayed - v->delay_lp);
		delayed = v->delay_lp;
		float ring_fb = p->ring_feedback * 0.01f;
		v->delay_buf[v->delay_pos] = clampf(dry + delayed * ring_fb, -1.0f, 1.0f);
		v->delay_pos = (v->delay_pos + 1) % MAX_DELAY;
		float mix = p->ring_mix * 0.01f;
		out = dry * (1.0f - mix) + delayed * mix;

		// The per-pad filter is last, so it can tame everything Crush, Drive
		// and Ring add.
		float fc = 40.0f * std::pow(400.0f, v->cutoff_s * 0.01f);
		float f = clampf(2.0f * std::sin(PI * fc / sample_rate), 0.001f, 0.95f);
		float q = clampf(1.0f - v->resonance_s * 0.009f, 0.05f, 1.0f);
		v->filter_low += f * v->filter_band;
		float high = out - v->filter_low - q * v->filter_band;
		v->filter_band += f * high;
		float notch = high + v->filter_low;
		if (p->filter_type == 0) out = v->filter_low;
		else if (p->filter_type == 1) out = high;
		else if (p->filter_type == 2) out = notch;
		else if (p->filter_type == 4) out = v->filter_band;
		else if (p->filter_type == 3) {
			// DJ: low-pass at the left, band-pass in the middle, high-pass at the right.
			float pos = clampf(v->cutoff_s * 0.01f, 0.0f, 1.0f);
			if (pos < 0.5f) {
				float t = pos * 2.0f;
				out = v->filter_low * (1.0f - t) + v->filter_band * t;
			}
			else {
				float t = (pos - 0.5f) * 2.0f;
				out = v->filter_band * (1.0f - t) + high * t;
			}
		}
		if (v->steal_left) {
			float t = (float)v->steal_left-- / steal_samples;
			out = out * (1.0f - t) + v->steal_tail * t;
		}
		v->last = out;
		return out;
	}

	// One sample. padOut (16 floats, may be null) gets each pad's own sound.
	// Returns the mono mix, scaled like the original (before its +-1 clamp).
	float process(float* padOut) {
		if (padOut) std::fill(padOut, padOut + PADS, 0.f);
		float mono = 0.0f;
		for (int j = 0; j < VOICES; ++j) {
			if (!voices[j].active) continue;
			float s = render_voice(&voices[j]);
			mono += s;
			if (padOut) padOut[voices[j].pad_index] += s * 0.24f;
		}
		return mono * 0.24f;
	}

	// How loud each pad is ringing right now (0..1), for the pad lights.
	void padActivity(float* act) const {
		std::fill(act, act + PADS, 0.f);
		for (int j = 0; j < VOICES; ++j) {
			const Voice& v = voices[j];
			if (!v.active) continue;
			act[v.pad_index] = std::max(act[v.pad_index], v.amp_env * v.velocity);
		}
	}
};

} // namespace sophie
