// Offline test: renders the original Sophie C code (test/reference/sophie.c)
// and the VCV port (src/SophieEngine.hpp) with the same notes and settings,
// and checks they produce the same samples. Also writes demo WAVs.
//   cc -O2 -c test/reference/sophie.c -o /tmp/sophie_ref.o
//   c++ -std=c++11 -O2 -Isrc -Itest/reference test/engine_test.cpp /tmp/sophie_ref.o -o /tmp/engine_test
//   /tmp/engine_test [wav_dir]
#include "SophieEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#include "plugin_api_v1.h"
plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t* host);
}

static int passed = 0, failed = 0;
#define CHECK(c, msg) do { if (c) passed++; else { failed++; std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

struct Pair {
	plugin_api_v2_t* api;
	void* ref;
	sophie::Engine eng;
	int maxDiff = 0;
	long long energy = 0;
	Pair() {
		api = move_plugin_init_v2(nullptr);
		ref = api->create_instance(".", nullptr);
	}
	~Pair() { api->destroy_instance(ref); }
	void hit(int pad, int vel) {
		uint8_t msg[3] = {0x90, (uint8_t)(36 + pad), (uint8_t)vel};
		api->on_midi(ref, msg, 3, 0);
		eng.trigger(pad, vel / 127.f);
	}
	// set one field on both: key like "model", value as in Schwung
	void set(int pad, const char* key, float value) {
		char k[48], v[48];
		std::snprintf(k, sizeof(k), "p%02d_%s", pad + 1, key);
		std::snprintf(v, sizeof(v), "%.6f", value);
		api->set_param(ref, k, v);
		sophie::Patch& p = eng.pads[pad];
		std::string s = key;
		if (s == "model") p.model = (int)value;
		else if (s == "tune") p.tune = value;
		else if (s == "color") p.color = value;
		else if (s == "metal") p.metal = value;
		else if (s == "feedback") p.feedback = value;
		else if (s == "sweep") p.sweep = value;
		else if (s == "decay") p.decay = value;
		else if (s == "crush") p.crush = value;
		else if (s == "drive") p.drive = value;
		else if (s == "level") p.level = value;
		else if (s == "cutoff") p.cutoff = value;
		else if (s == "resonance") p.resonance = value;
		else if (s == "filter_type") p.filter_type = (int)value;
		else if (s == "ring_time") p.ring_time = value;
		else if (s == "ring_feedback") p.ring_feedback = value;
		else if (s == "ring_mix") p.ring_mix = value;
		else if (s == "ring_tone") p.ring_tone = value;
		else if (s == "metal_decay") p.metal_decay = value;
		else if (s == "feedback_mode") p.feedback_mode = (int)value;
		else if (s == "tail_mode") p.tail_mode = (int)value;
	}
	void run(int frames, std::vector<float>* wav = nullptr) {
		const int B = 64;
		int16_t buf[B * 2];
		while (frames > 0) {
			int n = frames < B ? frames : B;
			api->render_block(ref, buf, n);
			for (int i = 0; i < n; ++i) {
				float m = eng.process(nullptr);
				int mine = (int)lrintf(sophie::clampf(m, -1.f, 1.f) * 30000.0f);
				int d = std::abs(mine - buf[i * 2]);
				if (d > maxDiff) maxDiff = d;
				energy += std::abs(mine);
				if (wav) wav->push_back(sophie::clampf(m, -1.f, 1.f));
			}
			frames -= n;
		}
	}
};

static void writeWav(const std::string& path, const std::vector<float>& x) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) return;
	uint32_t n = (uint32_t)x.size(), sr = 44100, bytes = n * 2;
	auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
	std::fwrite("data", 1, 4, f); w32(bytes);
	for (float s : x) { int16_t v = (int16_t)lrintf(s * 30000.f); std::fwrite(&v, 2, 1, f); }
	std::fclose(f);
}

int main(int argc, char** argv) {
	std::string wavDir = argc > 1 ? argv[1] : "";
	const int SR = 44100;

	// 1. every factory pad on its own
	{
		Pair p;
		std::vector<float> wav;
		for (int pad = 0; pad < 16; ++pad) {
			p.hit(pad, 127);
			p.run(SR / 2, &wav);
		}
		std::printf("factory kit: max difference %d / 30000, energy %lld\n", p.maxDiff, p.energy);
		CHECK(p.maxDiff <= 2, "factory pads match the original");
		CHECK(p.energy > 1000000, "factory pads make sound");
		if (!wavDir.empty()) writeWav(wavDir + "/factory-kit.wav", wav);
	}

	// 2. every model on a kick pad and a tom pad, with Raw and Smooth, Metal Decay on/off
	{
		Pair p;
		for (int pad : {0, 9}) for (int model = 0; model < 6; ++model) for (int mode = 0; mode < 2; ++mode)
			for (float md : {0.f, 250.f}) {
				p.set(pad, "model", (float)model);
				p.set(pad, "feedback_mode", (float)mode);
				p.set(pad, "metal_decay", md);
				p.set(pad, "feedback", 70);
				p.set(pad, "decay", 0.4f);
				p.hit(pad, 100);
				p.run(SR / 5);
			}
		std::printf("models x modes: max difference %d\n", p.maxDiff);
		CHECK(p.maxDiff <= 2, "all models and modes match");
	}

	// 3. filters, crush, drive, ring
	{
		Pair p;
		std::vector<float> wav;
		for (int ft = 0; ft < 5; ++ft) {
			p.set(2, "filter_type", (float)ft);
			p.set(2, "cutoff", 55);
			p.set(2, "resonance", 80);
			p.set(2, "crush", 70);
			p.set(2, "drive", 90);
			p.set(2, "ring_time", 4.5f);
			p.set(2, "ring_feedback", 85);
			p.set(2, "ring_mix", 60);
			p.set(2, "decay", 0.6f);
			p.hit(2, 90);
			p.run(SR / 3, &wav);
		}
		std::printf("filter / crush / drive / ring: max difference %d\n", p.maxDiff);
		CHECK(p.maxDiff <= 2, "effects chain matches");
		if (!wavDir.empty()) writeWav(wavDir + "/snare-effects.wav", wav);
	}

	// 4. voice stealing: a fast roll on long sounds
	{
		Pair p;
		std::vector<float> wav;
		for (int i = 0; i < 64; ++i) {
			p.hit((i * 7) % 16, 40 + (i * 13) % 87);
			p.run(SR / 16, &wav);
		}
		p.run(SR, &wav);
		std::printf("voice stealing roll: max difference %d\n", p.maxDiff);
		CHECK(p.maxDiff <= 2, "voice stealing matches");
		if (!wavDir.empty()) writeWav(wavDir + "/roll.wav", wav);
	}

	// 5. Live tails follow edits; Hit tails ignore them
	{
		Pair p;
		p.set(13, "tail_mode", 1);
		p.hit(13, 127);
		p.run(SR / 4);
		p.set(13, "cutoff", 20);
		p.set(13, "metal", 10);
		p.run(SR / 2);
		p.set(15, "tail_mode", 0);
		p.hit(15, 127);
		p.run(SR / 4);
		p.set(15, "cutoff", 10);
		p.run(SR / 2);
		std::printf("live / hit tails: max difference %d\n", p.maxDiff);
		CHECK(p.maxDiff <= 2, "Live and Hit tail modes match");
	}

	// 6. other sample rates run and stay bounded
	for (float sr : {48000.f, 96000.f, 192000.f}) {
		sophie::Engine e;
		e.setSampleRate(sr);
		for (int pad = 0; pad < 16; ++pad) e.pads[pad].ring_time = 30.f, e.pads[pad].ring_mix = 50.f, e.pads[pad].ring_feedback = 95.f;
		float peak = 0.f;
		bool finite = true;
		for (int i = 0; i < 16; ++i) {
			e.trigger(i, 1.f);
			for (int n = 0; n < (int)(sr / 8); ++n) {
				float pads[16];
				float m = e.process(pads);
				finite = finite && std::isfinite(m);
				peak = std::max(peak, std::fabs(m));
			}
		}
		std::printf("%.0f Hz: peak %.3f\n", sr, peak);
		CHECK(finite && peak > 0.05f && peak < 4.f, "other sample rates are sane");
	}

	std::printf("%d passed, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
