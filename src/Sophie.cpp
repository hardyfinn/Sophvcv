// Sophie: a sixteen-pad metallic FM percussion synthesizer for VCV Rack.
// A port of mestela/schwung-sophie (Ableton Move). The sound engine is in
// SophieEngine.hpp; this file is the panel, the jacks and the pad memory.
#include "plugin.hpp"
#include "SophieEngine.hpp"

using sophie::Patch;
using sophie::PADS;

// ---- panel positions in mm (must match res/make_panel.py) ----
static const float PAD_X[4] = {16.f, 36.f, 56.f, 76.f};
static const float PAD_ROW_Y[4] = {46.f, 66.f, 86.f, 106.f};  // top row first
static const float PAD_BTN_DY = -2.6f, PAD_JACK_DY = 5.6f;
static const float KX[5] = {99.f, 116.f, 133.f, 150.f, 167.f};
static const float KY[4] = {27.f, 51.f, 75.f, 99.f};
static const float NOTE_X[3] = {101.f, 118.f, 135.f};
static const float NOTE_Y = 115.f;
static const float CV_JX[2] = {188.f, 214.f};
static const float CV_TX[2] = {197.5f, 223.5f};
static const float CV_Y[5] = {27.f, 41.f, 55.f, 69.f, 83.f};
static const float OUT_X[3] = {192.f, 208.f, 224.f};
static const float OUT_Y = 109.f;

// Pad 1 is bottom left, pad 16 top right, like a Drum Rack or Ableton Move.
static int padAt(int row, int col) { return (3 - row) * 4 + col; }

static const char* MODEL_NAMES[6] = {"Fuse", "Stack", "Split", "Shard", "Pipe", "Boom"};
static const char* MODEL_HINTS[6] = {
	"two modulators in series",
	"dense FM stack, sub + octave",
	"two parallel operators",
	"stepped 12-bit wave + noise",
	"FM plus ring modulation",
	"clean body, metal attack"};
static const char* FILTER_NAMES[5] = {"LPF", "HPF", "Notch", "DJ", "BPF"};

// Shows "Auto" at zero.
struct MetalDecayQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		if (getValue() < 0.5f) return "Auto";
		return ParamQuantity::getDisplayValueString();
	}
};

struct Sophie : Module {
	enum ParamId {
		// These edit the selected pad. Order matters: see toParam()/fromParam().
		MODEL_PARAM, TUNE_PARAM, SWEEP_PARAM, DECAY_PARAM, LEVEL_PARAM,
		COLOR_PARAM, METAL_PARAM, FEEDBACK_PARAM, METAL_DECAY_PARAM, FB_MODE_PARAM,
		CRUSH_PARAM, DRIVE_PARAM, CUTOFF_PARAM, RESO_PARAM, FILTER_PARAM,
		RING_TIME_PARAM, RING_FB_PARAM, RING_TONE_PARAM, RING_MIX_PARAM, TAIL_MODE_PARAM,
		NUM_EDIT_PARAMS,
		VOLUME_PARAM = NUM_EDIT_PARAMS,
		ENUMS(PAD_PARAM, PADS),
		ENUMS(CV_ATT_PARAM, 10),
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(TRIG_INPUT, PADS),
		NOTE_INPUT, GATE_INPUT, VEL_INPUT,
		ENUMS(CV_INPUT, 10),
		INPUTS_LEN
	};
	enum OutputId { MIX_OUTPUT, PADS_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(PAD_LIGHT, PADS * 3), LIGHTS_LEN };
	enum CvId { CV_TUNE, CV_DECAY, CV_MODEL, CV_COLOR, CV_METAL, CV_FEEDBACK, CV_SWEEP, CV_CRUSH, CV_DRIVE, CV_CUTOFF };

	sophie::Engine engine;
	Patch base[PADS];       // what the knobs have set for each pad
	int selected = 0;       // the pad the knobs edit
	bool needSync = true;   // load the selected pad onto the knobs
	bool dirty = true;
	float cache[NUM_EDIT_PARAMS] = {};
	float sampleRate = 0.f;

	// options
	bool heightVelocity = false;  // trigger voltage sets velocity
	bool selectOnHit = false;     // trigger jacks select their pad

	dsp::SchmittTrigger trig[PADS];
	dsp::BooleanTrigger padButton[PADS];
	dsp::SchmittTrigger gateTrig[16];
	dsp::ClockDivider lightDivider;
	float hitFlash[PADS] = {};

	Sophie() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(MODEL_PARAM, 0.f, 5.f, 0.f, "Model", {"Fuse", "Stack", "Split", "Shard", "Pipe", "Boom"});
		configParam(TUNE_PARAM, -24.f, 24.f, 0.f, "Tune", " semitones");
		getParamQuantity(TUNE_PARAM)->snapEnabled = true;
		configParam(SWEEP_PARAM, -100.f, 100.f, 0.f, "Sweep (pitch envelope)", "%");
		// Decay knob is 0..1, shown as 0.03 s .. 4 s on an exponential curve.
		configParam(DECAY_PARAM, 0.f, 1.f, decayToKnob(0.3f), "Decay", " s", 4.f / 0.03f, 0.03f);
		configParam(LEVEL_PARAM, 0.f, 100.f, 100.f, "Level", "%");
		configParam(COLOR_PARAM, 0.f, 100.f, 0.f, "Color (frequency ratios)", "%");
		configParam(METAL_PARAM, 0.f, 100.f, 0.f, "Metal (FM depth)", "%");
		configParam(FEEDBACK_PARAM, 0.f, 100.f, 0.f, "Feedback", "%");
		configParam<MetalDecayQuantity>(METAL_DECAY_PARAM, 0.f, 2000.f, 0.f, "Metal decay (brightness fade)", " ms");
		configSwitch(FB_MODE_PARAM, 0.f, 1.f, 0.f, "Feedback mode", {"Raw", "Smooth"});
		configParam(CRUSH_PARAM, 0.f, 100.f, 0.f, "Crush (bits + sample rate)", "%");
		configParam(DRIVE_PARAM, 0.f, 100.f, 0.f, "Drive", "%");
		// Cutoff 0..100 is 40 Hz .. 16 kHz, as in the original.
		configParam(CUTOFF_PARAM, 0.f, 100.f, 100.f, "Cutoff", " Hz", std::pow(400.f, 0.01f), 40.f);
		configParam(RESO_PARAM, 0.f, 100.f, 0.f, "Resonance", "%");
		configSwitch(FILTER_PARAM, 0.f, 4.f, 0.f, "Filter type", {"LPF", "HPF", "Notch", "DJ (LP > BP > HP)", "BPF"});
		configParam(RING_TIME_PARAM, 0.5f, 30.f, 6.f, "Ring time", " ms");
		configParam(RING_FB_PARAM, 0.f, 95.f, 0.f, "Ring feedback", "%");
		configParam(RING_TONE_PARAM, 0.f, 100.f, 70.f, "Ring tone", "%");
		configParam(RING_MIX_PARAM, 0.f, 100.f, 0.f, "Ring mix", "%");
		configSwitch(TAIL_MODE_PARAM, 0.f, 1.f, 0.f, "Tail mode", {"Hit (each hit keeps its settings)", "Live (ringing hits follow the knobs)"});
		configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume", " dB", -10.f, 40.f);
		getParamQuantity(VOLUME_PARAM)->randomizeEnabled = false;

		for (int i = 0; i < PADS; ++i) {
			std::string n = string::f("Pad %d %s", i + 1, sophie::padName(i));
			configButton(PAD_PARAM + i, n + " (select and play)");
			configInput(TRIG_INPUT + i, n + " trigger");
		}
		static const char* cvNames[10] = {"Tune (1 V/oct)", "Decay (+1 V = x1.4)", "Model (1 V per model)", "Color", "Metal",
			"Feedback", "Sweep", "Crush", "Drive", "Cutoff"};
		for (int i = 0; i < 10; ++i) {
			configInput(CV_INPUT + i, std::string(cvNames[i]) + " CV. Mono: all pads. Poly: channel N goes to pad N");
			configParam(CV_ATT_PARAM + i, -1.f, 1.f, 1.f, std::string(cvNames[i]) + " CV amount", "%", 0.f, 100.f);
			getParamQuantity(CV_ATT_PARAM + i)->randomizeEnabled = false;
		}
		configInput(NOTE_INPUT, "Note (V/oct, poly). C2..D#3 = pads 1..16, as MIDI notes 36..51");
		configInput(GATE_INPUT, "Gate (poly)");
		configInput(VEL_INPUT, "Velocity (poly, 0..10 V)");
		configOutput(MIX_OUTPUT, "Mix");
		configOutput(PADS_OUTPUT, "Pads (16-channel poly, channel N = pad N)");

		lightDivider.setDivision(64);
		for (int i = 0; i < PADS; ++i) base[i] = sophie::factoryPatch(i);
	}

	static float decayToKnob(float s) { return std::log(sophie::clampf(s, 0.03f, 4.f) / 0.03f) / std::log(4.f / 0.03f); }
	static float knobToDecay(float k) { return 0.03f * std::pow(4.f / 0.03f, sophie::clampf(k, 0.f, 1.f)); }

	static float toParam(int id, const Patch& p) {
		switch (id) {
			case MODEL_PARAM: return (float)p.model;
			case TUNE_PARAM: return p.tune;
			case SWEEP_PARAM: return p.sweep;
			case DECAY_PARAM: return decayToKnob(p.decay);
			case LEVEL_PARAM: return p.level;
			case COLOR_PARAM: return p.color;
			case METAL_PARAM: return p.metal;
			case FEEDBACK_PARAM: return p.feedback;
			case METAL_DECAY_PARAM: return p.metal_decay;
			case FB_MODE_PARAM: return (float)p.feedback_mode;
			case CRUSH_PARAM: return p.crush;
			case DRIVE_PARAM: return p.drive;
			case CUTOFF_PARAM: return p.cutoff;
			case RESO_PARAM: return p.resonance;
			case FILTER_PARAM: return (float)p.filter_type;
			case RING_TIME_PARAM: return p.ring_time;
			case RING_FB_PARAM: return p.ring_feedback;
			case RING_TONE_PARAM: return p.ring_tone;
			case RING_MIX_PARAM: return p.ring_mix;
			case TAIL_MODE_PARAM: return (float)p.tail_mode;
		}
		return 0.f;
	}

	static void fromParam(int id, float v, Patch& p) {
		switch (id) {
			case MODEL_PARAM: p.model = (int)std::round(sophie::clampf(v, 0, 5)); break;
			case TUNE_PARAM: p.tune = std::round(v); break;
			case SWEEP_PARAM: p.sweep = v; break;
			case DECAY_PARAM: p.decay = knobToDecay(v); break;
			case LEVEL_PARAM: p.level = v; break;
			case COLOR_PARAM: p.color = v; break;
			case METAL_PARAM: p.metal = v; break;
			case FEEDBACK_PARAM: p.feedback = v; break;
			case METAL_DECAY_PARAM: p.metal_decay = v < 0.5f ? 0.f : v; break;
			case FB_MODE_PARAM: p.feedback_mode = v > 0.5f; break;
			case CRUSH_PARAM: p.crush = v; break;
			case DRIVE_PARAM: p.drive = v; break;
			case CUTOFF_PARAM: p.cutoff = v; break;
			case RESO_PARAM: p.resonance = v; break;
			case FILTER_PARAM: p.filter_type = (int)std::round(sophie::clampf(v, 0, 4)); break;
			case RING_TIME_PARAM: p.ring_time = v; break;
			case RING_FB_PARAM: p.ring_feedback = v; break;
			case RING_TONE_PARAM: p.ring_tone = v; break;
			case RING_MIX_PARAM: p.ring_mix = v; break;
			case TAIL_MODE_PARAM: p.tail_mode = v > 0.5f; break;
		}
	}

	void selectPad(int pad) {
		if (pad == selected) return;
		selected = pad;
		needSync = true;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < PADS; ++i) base[i] = sophie::factoryPatch(i);
		selected = 0;
		needSync = true;
		engine.reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
		sampleRate = e.sampleRate;
	}

	float cv(int which, int pad) {
		Input& in = inputs[CV_INPUT + which];
		if (!in.isConnected()) return 0.f;
		return in.getPolyVoltage(pad) * params[CV_ATT_PARAM + which].getValue();
	}

	// knobs + CV for one pad
	Patch effective(int pad) {
		Patch e = base[pad];
		float v;
		if ((v = cv(CV_TUNE, pad)) != 0.f) e.tune = sophie::clampf(e.tune + 12.f * v, -60.f, 60.f);
		if ((v = cv(CV_DECAY, pad)) != 0.f) e.decay = sophie::clampf(e.decay * std::exp2(v * 0.5f), 0.03f, 4.f);
		if ((v = cv(CV_MODEL, pad)) != 0.f) e.model = (int)sophie::clampf((float)e.model + std::round(v), 0.f, 5.f);
		if ((v = cv(CV_COLOR, pad)) != 0.f) e.color = sophie::clampf(e.color + 10.f * v, 0.f, 100.f);
		if ((v = cv(CV_METAL, pad)) != 0.f) e.metal = sophie::clampf(e.metal + 10.f * v, 0.f, 100.f);
		if ((v = cv(CV_FEEDBACK, pad)) != 0.f) e.feedback = sophie::clampf(e.feedback + 10.f * v, 0.f, 100.f);
		if ((v = cv(CV_SWEEP, pad)) != 0.f) e.sweep = sophie::clampf(e.sweep + 20.f * v, -100.f, 100.f);
		if ((v = cv(CV_CRUSH, pad)) != 0.f) e.crush = sophie::clampf(e.crush + 10.f * v, 0.f, 100.f);
		if ((v = cv(CV_DRIVE, pad)) != 0.f) e.drive = sophie::clampf(e.drive + 10.f * v, 0.f, 100.f);
		if ((v = cv(CV_CUTOFF, pad)) != 0.f) e.cutoff = sophie::clampf(e.cutoff + 10.f * v, 0.f, 100.f);
		return e;
	}

	void hit(int pad, float velocity) {
		engine.trigger(pad, velocity);
		hitFlash[pad] = 1.f;
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != sampleRate) {
			sampleRate = args.sampleRate;
			engine.setSampleRate(sampleRate);
		}

		// Knobs <-> selected pad
		if (needSync) {
			needSync = false;
			for (int i = 0; i < NUM_EDIT_PARAMS; ++i) {
				float v = toParam(i, base[selected]);
				params[i].setValue(v);
				cache[i] = v;
			}
			dirty = true;
		}
		else {
			for (int i = 0; i < NUM_EDIT_PARAMS; ++i) {
				float v = params[i].getValue();
				if (v != cache[i]) {
					cache[i] = v;
					fromParam(i, v, base[selected]);
					dirty = true;
				}
			}
		}

		// The engine plays engine.pads: knobs + CV, refreshed every sample when CV is patched.
		bool anyCv = false;
		for (int i = 0; i < 10; ++i) anyCv = anyCv || inputs[CV_INPUT + i].isConnected();
		if (anyCv || dirty) {
			for (int p = 0; p < PADS; ++p) engine.pads[p] = effective(p);
			dirty = anyCv;  // when CV is unplugged, refresh once more
		}

		// Pad buttons: select and audition
		for (int i = 0; i < PADS; ++i) {
			if (padButton[i].process(params[PAD_PARAM + i].getValue() > 0.f)) {
				selectPad(i);
				hit(i, 1.f);
			}
		}

		// Trigger jacks
		for (int i = 0; i < PADS; ++i) {
			Input& in = inputs[TRIG_INPUT + i];
			if (!in.isConnected()) continue;
			float v = in.getVoltage();
			if (trig[i].process(v, 0.1f, 1.f)) {
				hit(i, heightVelocity ? sophie::clampf(v / 10.f, 0.f, 1.f) : 1.f);
				if (selectOnHit) selectPad(i);
			}
		}

		// Notes (from MIDI > CV or a sequencer): C2..D#3 play pads 1..16
		int gateCh = inputs[GATE_INPUT].getChannels();
		for (int c = 0; c < gateCh; ++c) {
			if (gateTrig[c].process(inputs[GATE_INPUT].getVoltage(c), 0.1f, 1.f)) {
				int note = (int)std::round(60.f + 12.f * inputs[NOTE_INPUT].getPolyVoltage(c));
				int pad = note - sophie::FIRST_NOTE;
				if (pad >= 0 && pad < PADS) {
					float vel = inputs[VEL_INPUT].isConnected() ? sophie::clampf(inputs[VEL_INPUT].getPolyVoltage(c) / 10.f, 0.f, 1.f) : 1.f;
					hit(pad, vel);
					if (selectOnHit) selectPad(pad);
				}
			}
		}
		for (int c = gateCh; c < 16; ++c) gateTrig[c].reset();

		// Sound
		float padOut[PADS];
		float mix = engine.process(outputs[PADS_OUTPUT].isConnected() ? padOut : nullptr);
		float gain = params[VOLUME_PARAM].getValue();
		gain *= gain;  // the Volume knob has an audio taper
		outputs[MIX_OUTPUT].setVoltage(sophie::clampf(mix, -1.f, 1.f) * 5.f * gain);
		if (outputs[PADS_OUTPUT].isConnected()) {
			outputs[PADS_OUTPUT].setChannels(PADS);
			for (int i = 0; i < PADS; ++i) outputs[PADS_OUTPUT].setVoltage(padOut[i] * 5.f * gain, i);
		}

		// Pad lights: pink = playing, blue = the pad the knobs edit
		if (lightDivider.process()) {
			float act[PADS];
			engine.padActivity(act);
			float dt = args.sampleTime * lightDivider.getDivision();
			for (int i = 0; i < PADS; ++i) {
				hitFlash[i] = std::max(0.f, hitFlash[i] - dt * 8.f);
				float a = std::min(1.f, std::max(hitFlash[i], std::sqrt(act[i])));
				float s = i == selected ? 0.55f : 0.f;
				lights[PAD_LIGHT + i * 3 + 0].setBrightness(a);
				lights[PAD_LIGHT + i * 3 + 1].setBrightness(a * 0.3f + s * 0.25f);
				lights[PAD_LIGHT + i * 3 + 2].setBrightness(std::max(a * 0.6f, s));
			}
		}
	}

	// ---- saving ----
	static json_t* patchToJson(const Patch& p) {
		json_t* o = json_object();
		json_object_set_new(o, "model", json_integer(p.model));
		json_object_set_new(o, "tune", json_real(p.tune));
		json_object_set_new(o, "color", json_real(p.color));
		json_object_set_new(o, "metal", json_real(p.metal));
		json_object_set_new(o, "feedback", json_real(p.feedback));
		json_object_set_new(o, "sweep", json_real(p.sweep));
		json_object_set_new(o, "decay", json_real(p.decay));
		json_object_set_new(o, "crush", json_real(p.crush));
		json_object_set_new(o, "drive", json_real(p.drive));
		json_object_set_new(o, "level", json_real(p.level));
		json_object_set_new(o, "cutoff", json_real(p.cutoff));
		json_object_set_new(o, "resonance", json_real(p.resonance));
		json_object_set_new(o, "filter_type", json_integer(p.filter_type));
		json_object_set_new(o, "ring_time", json_real(p.ring_time));
		json_object_set_new(o, "ring_feedback", json_real(p.ring_feedback));
		json_object_set_new(o, "ring_mix", json_real(p.ring_mix));
		json_object_set_new(o, "ring_tone", json_real(p.ring_tone));
		json_object_set_new(o, "metal_decay", json_real(p.metal_decay));
		json_object_set_new(o, "feedback_mode", json_integer(p.feedback_mode));
		json_object_set_new(o, "tail_mode", json_integer(p.tail_mode));
		return o;
	}

	static void patchFromJson(json_t* o, Patch& p) {
		auto num = [&](const char* k, float& f, float lo, float hi) {
			json_t* j = json_object_get(o, k);
			if (j) f = sophie::clampf((float)json_number_value(j), lo, hi);
		};
		auto inum = [&](const char* k, int& n, int lo, int hi) {
			json_t* j = json_object_get(o, k);
			if (j) n = std::max(lo, std::min(hi, (int)json_number_value(j)));
		};
		inum("model", p.model, 0, 5);
		num("tune", p.tune, -24, 24);
		num("color", p.color, 0, 100);
		num("metal", p.metal, 0, 100);
		num("feedback", p.feedback, 0, 100);
		num("sweep", p.sweep, -100, 100);
		num("decay", p.decay, 0.03f, 4);
		num("crush", p.crush, 0, 100);
		num("drive", p.drive, 0, 100);
		num("level", p.level, 0, 100);
		num("cutoff", p.cutoff, 0, 100);
		num("resonance", p.resonance, 0, 100);
		inum("filter_type", p.filter_type, 0, 4);
		num("ring_time", p.ring_time, 0.5f, 30);
		num("ring_feedback", p.ring_feedback, 0, 95);
		num("ring_mix", p.ring_mix, 0, 100);
		num("ring_tone", p.ring_tone, 0, 100);
		num("metal_decay", p.metal_decay, 0, 2000);
		inum("feedback_mode", p.feedback_mode, 0, 1);
		inum("tail_mode", p.tail_mode, 0, 1);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* arr = json_array();
		for (int i = 0; i < PADS; ++i) json_array_append_new(arr, patchToJson(base[i]));
		json_object_set_new(root, "pads", arr);
		json_object_set_new(root, "selected", json_integer(selected));
		json_object_set_new(root, "heightVelocity", json_boolean(heightVelocity));
		json_object_set_new(root, "selectOnHit", json_boolean(selectOnHit));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* arr = json_object_get(root, "pads");
		if (arr && json_is_array(arr)) {
			for (int i = 0; i < PADS && i < (int)json_array_size(arr); ++i) {
				base[i] = sophie::factoryPatch(i);
				patchFromJson(json_array_get(arr, i), base[i]);
			}
		}
		json_t* j;
		if ((j = json_object_get(root, "selected"))) selected = std::max(0, std::min(PADS - 1, (int)json_integer_value(j)));
		if ((j = json_object_get(root, "heightVelocity"))) heightVelocity = json_boolean_value(j);
		if ((j = json_object_get(root, "selectOnHit"))) selectOnHit = json_boolean_value(j);
		needSync = true;
	}
};

// ---- display ----
static const NVGcolor D_BG = nvgRGB(0x0C, 0x08, 0x10);
static const NVGcolor D_INK = nvgRGB(0xF4, 0xEE, 0xF7);
static const NVGcolor D_SOFT = nvgRGB(0x9A, 0x8C, 0xA8);
static const NVGcolor D_PINK = nvgRGB(0xFF, 0x4F, 0xA3);
static const NVGcolor D_BLUE = nvgRGB(0x7F, 0xC8, 0xFF);

struct SophieDisplay : Widget {
	Sophie* module = nullptr;

	void text(const DrawArgs& a, float x, float y, float size, NVGcolor c, int align, const std::string& s) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}

	float textWidth(const DrawArgs& a, float size, const std::string& s) {
		nvgFontSize(a.vg, size);
		float b[4];
		nvgTextBounds(a.vg, 0, 0, s.c_str(), NULL, b);
		return b[2] - b[0];
	}

	void chip(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, float& w) {
		nvgFontSize(a.vg, 9.f);
		float b[4];
		nvgTextBounds(a.vg, 0, 0, s.c_str(), NULL, b);
		w = b[2] - b[0] + 8.f;
		nvgBeginPath(a.vg);
		nvgRoundedRect(a.vg, x, y, w, 12.f, 3.f);
		nvgStrokeColor(a.vg, c);
		nvgStrokeWidth(a.vg, 1.f);
		nvgStroke(a.vg);
		text(a, x + 4.f, y + 9.f, 9.f, c, NVG_ALIGN_LEFT, s);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(1.2f));
		nvgFillColor(args.vg, D_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);

		int sel = module ? module->selected : 13;
		Patch p = module ? module->base[sel] : sophie::factoryPatch(sel);
		int voices = module ? module->engine.activeVoices() : 3;

		float x0 = 6 + textWidth(args, 14.f, string::f("PAD %d", sel + 1)) + 6;
		text(args, 6, 15, 14.f, D_INK, NVG_ALIGN_LEFT, string::f("PAD %d", sel + 1));
		text(args, x0, 15, 12.f, D_PINK, NVG_ALIGN_LEFT, sophie::padName(sel));
		text(args, W - 6, 13, 9.f, D_SOFT, NVG_ALIGN_RIGHT, string::f("voices %d/12", voices));

		int m = std::max(0, std::min(5, p.model));
		text(args, 6, 31, 11.f, D_INK, NVG_ALIGN_LEFT, MODEL_NAMES[m]);
		bool kick = sel == 0 && m < 4;  // pad 1 keeps a sine kick body under models 1-4
		text(args, 6 + textWidth(args, 11.f, MODEL_NAMES[m]) + 6, 31, 9.f, kick ? D_PINK : D_SOFT, NVG_ALIGN_LEFT,
			kick ? "kick body + texture" : MODEL_HINTS[m]);

		float x = 6, w;
		float fc = 40.f * std::pow(400.f, p.cutoff * 0.01f);
		std::string f = string::f("%s %s", FILTER_NAMES[std::max(0, std::min(4, p.filter_type))],
			fc >= 1000.f ? string::f("%.1fk", fc / 1000.f).c_str() : string::f("%.0f", fc).c_str());
		chip(args, x, 40, f, D_BLUE, w); x += w + 4;
		chip(args, x, 40, p.feedback_mode ? "FB Smooth" : "FB Raw", p.feedback_mode ? D_PINK : D_SOFT, w); x += w + 4;
		chip(args, x, 40, p.tail_mode ? "Tail Live" : "Tail Hit", p.tail_mode ? D_PINK : D_SOFT, w); x += w + 4;
		if (p.ring_mix > 0.5f) { chip(args, x, 40, string::f("Ring %.1fms", p.ring_time), D_BLUE, w); x += w + 4; }
		Widget::drawLayer(args, layer);
	}
};

// ---- context menu helpers ----
static Patch clipboard;
static bool clipboardFull = false;

struct SophieWidget : ModuleWidget {
	SophieWidget(Sophie* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Sophie.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		SophieDisplay* d = createWidget<SophieDisplay>(mm2px(Vec(5.0, 12.5)));
		d->box.size = mm2px(Vec(79.0, 21.0));
		d->module = module;
		addChild(d);

		for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
			int pad = padAt(r, c);
			addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(PAD_X[c], PAD_ROW_Y[r] + PAD_BTN_DY)),
				module, Sophie::PAD_PARAM + pad, Sophie::PAD_LIGHT + pad * 3));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PAD_X[c], PAD_ROW_Y[r] + PAD_JACK_DY)), module, Sophie::TRIG_INPUT + pad));
		}

		const int grid[4][5] = {
			{Sophie::MODEL_PARAM, Sophie::TUNE_PARAM, Sophie::SWEEP_PARAM, Sophie::DECAY_PARAM, Sophie::LEVEL_PARAM},
			{Sophie::COLOR_PARAM, Sophie::METAL_PARAM, Sophie::FEEDBACK_PARAM, Sophie::METAL_DECAY_PARAM, Sophie::FB_MODE_PARAM},
			{Sophie::CRUSH_PARAM, Sophie::DRIVE_PARAM, Sophie::CUTOFF_PARAM, Sophie::RESO_PARAM, Sophie::FILTER_PARAM},
			{Sophie::RING_TIME_PARAM, Sophie::RING_FB_PARAM, Sophie::RING_TONE_PARAM, Sophie::RING_MIX_PARAM, Sophie::TAIL_MODE_PARAM}};
		for (int r = 0; r < 4; ++r) for (int c = 0; c < 5; ++c) {
			int id = grid[r][c];
			Vec pos = mm2px(Vec(KX[c], KY[r]));
			if (id == Sophie::FB_MODE_PARAM || id == Sophie::TAIL_MODE_PARAM)
				addParam(createParamCentered<CKSS>(pos, module, id));
			else if (id == Sophie::MODEL_PARAM || id == Sophie::FILTER_PARAM) {
				RoundBlackSnapKnob* k = createParamCentered<RoundBlackSnapKnob>(pos, module, id);
				addParam(k);
			}
			else
				addParam(createParamCentered<RoundBlackKnob>(pos, module, id));
		}

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(NOTE_X[0], NOTE_Y)), module, Sophie::NOTE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(NOTE_X[1], NOTE_Y)), module, Sophie::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(NOTE_X[2], NOTE_Y)), module, Sophie::VEL_INPUT));

		for (int i = 0; i < 10; ++i) {
			int col = i % 2, row = i / 2;
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_JX[col], CV_Y[row])), module, Sophie::CV_INPUT + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_TX[col], CV_Y[row])), module, Sophie::CV_ATT_PARAM + i));
		}

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(OUT_X[0], OUT_Y)), module, Sophie::VOLUME_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[1], OUT_Y)), module, Sophie::MIX_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[2], OUT_Y)), module, Sophie::PADS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Sophie* m = dynamic_cast<Sophie*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Pad %d (%s)", m->selected + 1, sophie::padName(m->selected))));
		menu->addChild(createMenuItem("Copy pad", "", [=]() { clipboard = m->base[m->selected]; clipboardFull = true; }));
		menu->addChild(createMenuItem("Paste pad", "", [=]() {
			if (!clipboardFull) return;
			m->base[m->selected] = clipboard;
			m->needSync = true;
		}, !clipboardFull));
		menu->addChild(createMenuItem("Paste pad to all 16", "", [=]() {
			if (!clipboardFull) return;
			for (int i = 0; i < PADS; ++i) m->base[i] = clipboard;
			m->needSync = true;
		}, !clipboardFull));
		menu->addChild(createMenuItem("Reset pad to factory sound", "", [=]() {
			m->base[m->selected] = sophie::factoryPatch(m->selected);
			m->needSync = true;
		}));
		menu->addChild(createMenuItem("Mutate pad (small random nudge)", "", [=]() {
			Patch& p = m->base[m->selected];
			auto nudge = [](float v, float amt, float lo, float hi) { return sophie::clampf(v + (random::uniform() * 2.f - 1.f) * amt, lo, hi); };
			p.color = nudge(p.color, 12, 0, 100);
			p.metal = nudge(p.metal, 12, 0, 100);
			p.feedback = nudge(p.feedback, 12, 0, 100);
			p.sweep = nudge(p.sweep, 15, -100, 100);
			p.decay = sophie::clampf(p.decay * std::exp2((random::uniform() * 2.f - 1.f) * 0.5f), 0.03f, 4.f);
			p.cutoff = nudge(p.cutoff, 8, 0, 100);
			m->needSync = true;
		}));
		menu->addChild(createMenuItem("Reset whole kit to factory", "", [=]() {
			for (int i = 0; i < PADS; ++i) m->base[i] = sophie::factoryPatch(i);
			m->needSync = true;
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Options"));
		menu->addChild(createBoolPtrMenuItem("Trigger height sets velocity (10 V = full)", "", &m->heightVelocity));
		menu->addChild(createBoolPtrMenuItem("Incoming hits select their pad", "", &m->selectOnHit));
	}
};

Model* modelSophie = createModel<Sophie, SophieWidget>("Sophie");
