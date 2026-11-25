// Platform socket headers must come before rack.hpp so winsock2.h precedes any windows.h include
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SocketHandle;
#define TSTICK_INVALID_SOCKET INVALID_SOCKET
#define tstickCloseSocket closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SocketHandle;
#define TSTICK_INVALID_SOCKET (-1)
#define tstickCloseSocket close
#endif

#include "plugin.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// Minimal OSC 1.0 parsing: messages and (nested) bundles, numeric arguments only.
struct OscMessage {
	std::string address;
	std::vector<float> args;
};

uint32_t readBigEndian32(const uint8_t* p) {
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t readBigEndian64(const uint8_t* p) {
	return (uint64_t(readBigEndian32(p)) << 32) | uint64_t(readBigEndian32(p + 4));
}

// Reads a null-terminated, 4-byte padded OSC string. Returns false if malformed.
bool readOscString(const uint8_t* data, size_t size, size_t& pos, std::string& out) {
	size_t start = pos;
	while (pos < size && data[pos] != 0) {
		pos++;
	}
	if (pos >= size) {
		return false;
	}
	out.assign(reinterpret_cast<const char*>(data + start), pos - start);
	pos = (pos + 4) & ~size_t(3);
	return pos <= size;
}

bool parseOscMessage(const uint8_t* data, size_t size, OscMessage& msg) {
	size_t pos = 0;
	if (!readOscString(data, size, pos, msg.address)) {
		return false;
	}
	msg.args.clear();
	std::string tags;
	if (pos >= size || !readOscString(data, size, pos, tags) || tags.empty() || tags[0] != ',') {
		// Messages without a type tag string have no arguments we can interpret
		return true;
	}
	for (size_t t = 1; t < tags.size(); t++) {
		switch (tags[t]) {
			case 'f': {
				if (pos + 4 > size) return false;
				uint32_t bits = readBigEndian32(data + pos);
				float f;
				std::memcpy(&f, &bits, 4);
				msg.args.push_back(f);
				pos += 4;
			} break;
			case 'i': {
				if (pos + 4 > size) return false;
				msg.args.push_back(float(int32_t(readBigEndian32(data + pos))));
				pos += 4;
			} break;
			case 'd': {
				if (pos + 8 > size) return false;
				uint64_t bits = readBigEndian64(data + pos);
				double d;
				std::memcpy(&d, &bits, 8);
				msg.args.push_back(float(d));
				pos += 8;
			} break;
			case 'h': {
				if (pos + 8 > size) return false;
				msg.args.push_back(float(int64_t(readBigEndian64(data + pos))));
				pos += 8;
			} break;
			case 't': {
				if (pos + 8 > size) return false;
				pos += 8;
			} break;
			case 'c':
			case 'r':
			case 'm': {
				if (pos + 4 > size) return false;
				pos += 4;
			} break;
			case 's':
			case 'S': {
				std::string ignored;
				if (!readOscString(data, size, pos, ignored)) return false;
			} break;
			case 'b': {
				if (pos + 4 > size) return false;
				size_t blobSize = readBigEndian32(data + pos);
				pos += 4 + ((blobSize + 3) & ~size_t(3));
				if (pos > size) return false;
			} break;
			case 'T': msg.args.push_back(1.f); break;
			case 'F': msg.args.push_back(0.f); break;
			case 'N':
			case 'I':
			case '[':
			case ']':
				break;
			default:
				// Unknown type: we can't know its size, so stop here
				return true;
		}
	}
	return true;
}

template <typename Callback>
void parseOscPacket(const uint8_t* data, size_t size, Callback&& callback, int depth = 0) {
	if (size < 4 || depth > 8) {
		return;
	}
	if (size >= 16 && std::memcmp(data, "#bundle", 8) == 0) {
		size_t pos = 16; // "#bundle\0" + 8 byte time tag
		while (pos + 4 <= size) {
			size_t elementSize = readBigEndian32(data + pos);
			pos += 4;
			if (elementSize > size - pos) {
				return;
			}
			parseOscPacket(data + pos, elementSize, callback, depth + 1);
			pos += elementSize;
		}
		return;
	}
	if (data[0] != '/') {
		return;
	}
	OscMessage msg;
	if (parseOscMessage(data, size, msg)) {
		callback(msg);
	}
}

bool endsWith(const std::string& s, const char* suffix) {
	size_t n = std::strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

float wrapAngle(float a) {
	// Wrap into [-pi, pi)
	return a - 2.f * M_PI * std::floor((a + M_PI) / (2.f * M_PI));
}

const std::array<std::vector<int>, 5> SCALES = {{
	{0, 2, 4, 5, 7, 9, 11},                     // major
	{0, 2, 3, 5, 7, 8, 10},                     // natural minor
	{0, 2, 4, 7, 9},                            // major pentatonic
	{0, 3, 5, 7, 10},                           // minor pentatonic
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},     // chromatic
}};

float indexToVoltage(int index, int scale) {
	const std::vector<int>& degrees = SCALES[scale];
	int n = degrees.size();
	return float(index / n) + degrees[index % n] / 12.f;
}

// Moves linearly to each new target, arriving after a fixed time
struct LinearRamp {
	float value = 0.f;
	float target = 0.f;
	float step = 0.f;

	float process(float newTarget, float rampTime, float sampleTime) {
		if (newTarget != target) {
			target = newTarget;
			step = (target - value) * sampleTime / rampTime;
		}
		if (std::abs(target - value) <= std::abs(step)) {
			value = target;
			step = 0.f;
		}
		else {
			value += step;
		}
		return value;
	}
};

} // namespace

struct TStick : Module {
	enum ParamId {
		LISTEN_PARAM,
		SCALE_PARAM,
		GROUP_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		SQUEEZE_OUTPUT,
		TOUCH_GATE_OUTPUT,
		TOUCH_LOW_OUTPUT,
		TOUCH_HIGH_OUTPUT,
		PITCH_OUTPUT,
		YAW_OUTPUT,
		ROLL_OUTPUT,
		TOTAL_OUTPUT,
		ACCEL_OUTPUT,
		NOTE_GATES_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LISTEN_LIGHT,
		RECEIVE_LIGHT,
		LIGHTS_LEN
	};

	enum Angle {
		YAW,
		PITCH,
		ROLL,
		ANGLES_LEN
	};

	// Values shared between the network thread (writer) and the audio thread (reader)
	std::atomic<float> squeeze{0.f};
	std::atomic<bool> touched{false};
	std::atomic<int> touchLow{-1};
	std::atomic<int> touchHigh{-1};
	// Bit i set while pad i is touched, for the first 64 pads
	std::atomic<uint64_t> touchMask{0};
	std::atomic<int> padCount{0};
	std::array<std::atomic<float>, ANGLES_LEN> angleChange;
	std::atomic<float> totalRotation{0.f};
	std::atomic<float> acceleration{0.f};
	std::atomic<uint32_t> messageCount{0};

	// Network thread control
	std::atomic<bool> listening{false};
	std::atomic<bool> running{true};
	std::atomic<int> configVersion{0};
	std::thread networkThread;

	std::mutex configMutex;
	std::string listenIp = "0.0.0.0";
	int listenPort = 8000;
	std::string status = "Not listening";

	// Angle tracking state, only touched on the network thread
	bool haveYpr = false;
	bool wasTouched = false;
	std::array<float, ANGLES_LEN> lastRaw{};
	std::array<float, ANGLES_LEN> unwrapped{};
	std::array<float, ANGLES_LEN> reference{};
	bool haveOrientation = false;
	std::array<float, 4> orientation{};
	std::array<float, 4> referenceOrientation{};

	// Audio thread state
	uint32_t lastMessageCount = 0;
	float receivePulse = 0.f;
	float lowVoltage = 0.f;
	float highVoltage = 0.f;
	std::array<LinearRamp, OUTPUTS_LEN> ramps;
	const float rampTime = 0.015f;

	TStick() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configSwitch(LISTEN_PARAM, 0.f, 1.f, 1.f, "Listen for OSC", {"Off", "On"});
		configParam(GROUP_PARAM, 1.f, 5.f, 1.f, "Touch pads per note")->snapEnabled = true;
		configSwitch(SCALE_PARAM, 0.f, 4.f, 0.f, "Touch scale",
			{"Major", "Minor", "Major pentatonic", "Minor pentatonic", "Chromatic"});

		configOutput(SQUEEZE_OUTPUT, "Squeeze");
		configOutput(TOUCH_GATE_OUTPUT, "Touch gate");
		configOutput(TOUCH_LOW_OUTPUT, "Lowest touch (1V/oct)");
		configOutput(TOUCH_HIGH_OUTPUT, "Highest touch (1V/oct)");
		configOutput(PITCH_OUTPUT, "Pitch change since touch (1V = pi/2 rad)");
		configOutput(YAW_OUTPUT, "Yaw change since touch (1V = pi/2 rad)");
		configOutput(ROLL_OUTPUT, "Roll change since touch (1V = pi/2 rad)");
		configOutput(TOTAL_OUTPUT, "Rotation away from orientation at touch (0V at start, 10V when turned all the way around)");
		configOutput(ACCEL_OUTPUT, "Total acceleration (1V = 1g, minus 1V so about 0V at rest)");
		configOutput(NOTE_GATES_OUTPUT, "Note gates (one channel per note, up to 16)");

		for (auto& a : angleChange) {
			a = 0.f;
		}

		networkThread = std::thread([this] { networkLoop(); });
	}

	~TStick() override {
		running = false;
		if (networkThread.joinable()) {
			networkThread.join();
		}
	}

	void setAddress(const std::string& ip, int port) {
		{
			std::lock_guard<std::mutex> lock(configMutex);
			listenIp = ip;
			listenPort = port;
		}
		configVersion++;
	}

	std::string getStatus() {
		std::lock_guard<std::mutex> lock(configMutex);
		return status;
	}

	void setStatus(const std::string& s) {
		std::lock_guard<std::mutex> lock(configMutex);
		status = s;
	}

	SocketHandle openSocket() {
		std::string ip;
		int port;
		{
			std::lock_guard<std::mutex> lock(configMutex);
			ip = listenIp;
			port = listenPort;
		}

		SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == TSTICK_INVALID_SOCKET) {
			setStatus("Could not create socket");
			return TSTICK_INVALID_SOCKET;
		}

		int reuse = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(uint16_t(port));
		if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
			tstickCloseSocket(sock);
			setStatus("Invalid IP address " + ip);
			return TSTICK_INVALID_SOCKET;
		}
		if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
			tstickCloseSocket(sock);
			setStatus(string::f("Could not bind %s:%d", ip.c_str(), port));
			return TSTICK_INVALID_SOCKET;
		}

		setStatus(string::f("Listening on %s:%d", ip.c_str(), port));
		return sock;
	}

	void networkLoop() {
#if defined(_WIN32)
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			setStatus("Could not start Winsock");
			return;
		}
#endif
		SocketHandle sock = TSTICK_INVALID_SOCKET;
		int openedVersion = -1;
		bool failed = false;
		std::vector<uint8_t> buffer(65536);

		while (running) {
			bool wantOpen = listening;
			int version = configVersion;

			if (sock != TSTICK_INVALID_SOCKET && (!wantOpen || version != openedVersion)) {
				tstickCloseSocket(sock);
				sock = TSTICK_INVALID_SOCKET;
			}
			if (!wantOpen) {
				setStatus("Not listening");
				failed = false;
				openedVersion = -1;
			}
			else if (sock == TSTICK_INVALID_SOCKET && (!failed || version != openedVersion)) {
				sock = openSocket();
				failed = sock == TSTICK_INVALID_SOCKET;
				openedVersion = version;
			}

			if (sock == TSTICK_INVALID_SOCKET) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}

			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(sock, &readSet);
			timeval timeout{0, 50000};
			int ready = select(int(sock + 1), &readSet, nullptr, nullptr, &timeout);
			if (ready <= 0) {
				continue;
			}
			int received = recv(sock, reinterpret_cast<char*>(buffer.data()), int(buffer.size()), 0);
			if (received > 0) {
				parseOscPacket(buffer.data(), size_t(received), [this](const OscMessage& msg) { handleMessage(msg); });
			}
		}

		if (sock != TSTICK_INVALID_SOCKET) {
			tstickCloseSocket(sock);
		}
#if defined(_WIN32)
		WSACleanup();
#endif
	}

	// Match on suffix so the module works with any T-Stick ID
	void handleMessage(const OscMessage& msg) {
		const std::string& a = msg.address;
		if (endsWith(a, "/instrument/squeeze")) {
			if (!msg.args.empty()) {
				squeeze = msg.args[0];
			}
		}
		else if (endsWith(a, "/instrument/touch/all")) {
			if (!msg.args.empty()) {
				bool isTouched = msg.args[0] > 0.f;
				if (isTouched && !wasTouched) {
					resetRotationReference();
				}
				wasTouched = isTouched;
				touched = isTouched;
			}
		}
		else if (endsWith(a, "/instrument/touch/discrete")) {
			int low = -1;
			int high = -1;
			uint64_t mask = 0;
			for (int i = 0; i < int(msg.args.size()); i++) {
				if (msg.args[i] != 0.f) {
					if (low < 0) {
						low = i;
					}
					high = i;
					if (i < 64) {
						mask |= uint64_t(1) << i;
					}
				}
			}
			touchLow = low;
			touchHigh = high;
			touchMask = mask;
			padCount = std::min(int(msg.args.size()), 64);
		}
		else if (endsWith(a, "/ypr")) {
			if (msg.args.size() >= 3) {
				updateRotation(msg.args[0], msg.args[1], msg.args[2]);
			}
		}
		else if (endsWith(a, "/raw/accl")) {
			if (msg.args.size() >= 3) {
				const std::vector<float>& v = msg.args;
				acceleration = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			}
		}
		else if (endsWith(a, "/orientation")) {
			if (msg.args.size() >= 4) {
				updateOrientation({msg.args[0], msg.args[1], msg.args[2], msg.args[3]});
			}
		}
		else {
			return;
		}
		messageCount++;
	}

	void resetRotationReference() {
		reference = unwrapped;
		referenceOrientation = orientation;
		publishRotation();
	}

	void updateOrientation(std::array<float, 4> q) {
		float norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
		if (!(norm > 0.f)) {
			return;
		}
		for (float& c : q) {
			c /= norm;
		}
		orientation = q;
		if (!haveOrientation) {
			referenceOrientation = q;
			haveOrientation = true;
		}
		publishRotation();
	}

	void updateRotation(float yaw, float pitch, float roll) {
		std::array<float, ANGLES_LEN> raw = {yaw, pitch, roll};
		if (!haveYpr) {
			lastRaw = raw;
			unwrapped = raw;
			reference = raw;
			haveYpr = true;
			publishRotation();
			return;
		}
		for (int i = 0; i < ANGLES_LEN; i++) {
			// Take the shortest way around the circle, so +pi -> -pi is a tiny step, not a jump
			unwrapped[i] += wrapAngle(raw[i] - lastRaw[i]);
			lastRaw[i] = raw[i];
		}
		publishRotation();
	}

	void publishRotation() {
		for (int i = 0; i < ANGLES_LEN; i++) {
			angleChange[i] = unwrapped[i] - reference[i];
		}
		// Angle of the rotation taking the reference orientation to the current one (0 to pi).
		// abs() because q and -q are the same orientation.
		float dot = 0.f;
		for (int i = 0; i < 4; i++) {
			dot += orientation[i] * referenceOrientation[i];
		}
		totalRotation = 2.f * std::acos(std::min(std::abs(dot), 1.f));
	}

	void process(const ProcessArgs& args) override {
		listening = params[LISTEN_PARAM].getValue() > 0.5f;

		const float radiansToVolts = 2.f / M_PI;
		int scale = clamp(int(params[SCALE_PARAM].getValue() + 0.5f), 0, 4);
		// Neighbouring pads share a note, so with 2 per note pads 0-1 are the root, 2-3 the second degree...
		int group = clamp(int(params[GROUP_PARAM].getValue() + 0.5f), 1, 5);

		// Hold the last touched notes when the stick is released
		int low = touchLow;
		int high = touchHigh;
		if (low >= 0) {
			lowVoltage = indexToVoltage(low / group, scale);
		}
		if (high >= 0) {
			highVoltage = indexToVoltage(high / group, scale);
		}

		// Gate and 1V/oct outputs switch instantly, everything else ramps to smooth out the OSC rate
		auto setSmoothed = [&](int output, float voltage) {
			outputs[output].setVoltage(ramps[output].process(voltage, rampTime, args.sampleTime));
		};

		setSmoothed(SQUEEZE_OUTPUT, clamp(squeeze.load(), 0.f, 1.f) * 10.f);
		outputs[TOUCH_GATE_OUTPUT].setVoltage(touched ? 10.f : 0.f);
		outputs[TOUCH_LOW_OUTPUT].setVoltage(lowVoltage);
		outputs[TOUCH_HIGH_OUTPUT].setVoltage(highVoltage);

		// One gate per note, high while any pad in that note's group is touched
		uint64_t mask = touchMask;
		int noteCount = clamp((padCount + group - 1) / group, 1, 16);
		uint64_t groupBits = (uint64_t(1) << group) - 1;
		outputs[NOTE_GATES_OUTPUT].setChannels(noteCount);
		for (int c = 0; c < noteCount; c++) {
			bool noteTouched = (mask >> (c * group)) & groupBits;
			outputs[NOTE_GATES_OUTPUT].setVoltage(noteTouched ? 10.f : 0.f, c);
		}
		setSmoothed(PITCH_OUTPUT, clamp(angleChange[PITCH].load() * radiansToVolts, -10.f, 10.f));
		setSmoothed(YAW_OUTPUT, clamp(angleChange[YAW].load() * radiansToVolts, -10.f, 10.f));
		setSmoothed(ROLL_OUTPUT, clamp(angleChange[ROLL].load() * radiansToVolts, -10.f, 10.f));
		// Largest possible distance is pi, so scale that to 10V
		setSmoothed(TOTAL_OUTPUT, clamp(totalRotation.load() * (10.f / float(M_PI)), 0.f, 10.f));
		// Accelerometer reports m/s^2 with gravity included, so offset by 1g to sit near 0V at rest
		setSmoothed(ACCEL_OUTPUT, clamp(acceleration.load() / 9.81f - 1.f, -1.f, 10.f));

		uint32_t count = messageCount;
		if (count != lastMessageCount) {
			lastMessageCount = count;
			receivePulse = 0.05f;
		}
		receivePulse = std::max(receivePulse - args.sampleTime, 0.f);
		lights[LISTEN_LIGHT].setBrightness(listening ? 1.f : 0.f);
		lights[RECEIVE_LIGHT].setBrightnessSmooth(receivePulse > 0.f ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		std::lock_guard<std::mutex> lock(configMutex);
		json_object_set_new(rootJ, "ip", json_string(listenIp.c_str()));
		json_object_set_new(rootJ, "port", json_integer(listenPort));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		std::string ip = listenIp;
		int port = listenPort;
		json_t* ipJ = json_object_get(rootJ, "ip");
		if (json_is_string(ipJ)) {
			ip = json_string_value(ipJ);
		}
		json_t* portJ = json_object_get(rootJ, "port");
		if (json_is_integer(portJ)) {
			port = json_integer_value(portJ);
		}
		setAddress(ip, port);
	}
};


struct TStickAddressField : ui::TextField {
	TStick* module;
	bool isPort;

	TStickAddressField(TStick* module, bool isPort) : module(module), isPort(isPort) {
		box.size.x = 120;
		std::lock_guard<std::mutex> lock(module->configMutex);
		text = isPort ? std::to_string(module->listenPort) : module->listenIp;
		placeholder = isPort ? "8000" : "0.0.0.0";
		selectAll();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			std::string ip;
			int port;
			{
				std::lock_guard<std::mutex> lock(module->configMutex);
				ip = module->listenIp;
				port = module->listenPort;
			}
			if (isPort) {
				int newPort = std::atoi(text.c_str());
				if (newPort > 0 && newPort < 65536) {
					port = newPort;
				}
			}
			else {
				ip = string::trim(text);
			}
			module->setAddress(ip, port);

			ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
			if (overlay) {
				overlay->requestDelete();
			}
			e.consume(this);
		}
		if (!e.getTarget()) {
			TextField::onSelectKey(e);
		}
	}
};


struct TStickLabels : TransparentWidget {
	struct Label {
		Vec pos;
		std::string text;
		float size;
	};
	std::vector<Label> labels;

	void add(Vec posMm, const std::string& text, float size = 8.f) {
		labels.push_back({mm2px(posMm), text, size});
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || !font->handle) {
			return;
		}
		nvgFontFaceId(args.vg, font->handle);
		nvgFillColor(args.vg, nvgRGB(0x30, 0x30, 0x30));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (const Label& l : labels) {
			nvgFontSize(args.vg, l.size);
			nvgText(args.vg, l.pos.x, l.pos.y, l.text.c_str(), nullptr);
		}
	}
};


struct TStickWidget : ModuleWidget {
	TStickWidget(TStick* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TStick.svg")));

		const float left = 10.16;
		const float right = 30.48;

		TStickLabels* labels = new TStickLabels;
		labels->box.size = box.size;
		labels->add(Vec(20.32, 7.0), "T-STICK", 12.f);
		labels->add(Vec(left, 14.5), "LISTEN");
		labels->add(Vec(right, 14.5), "RX");
		labels->add(Vec(left, 29.5), "SCALE");
		labels->add(Vec(right, 29.5), "GROUP");
		labels->add(Vec(left, 46.0), "SQUEEZE");
		labels->add(Vec(right, 46.0), "GATE");
		labels->add(Vec(left, 62.0), "TOUCH LO");
		labels->add(Vec(right, 62.0), "TOUCH HI");
		labels->add(Vec(20.32, 78.0), "ROTATION", 9.f);
		labels->add(Vec(left, 82.0), "PITCH");
		labels->add(Vec(right, 82.0), "YAW");
		labels->add(Vec(left, 96.0), "ROLL");
		labels->add(Vec(right, 96.0), "TOTAL");
		labels->add(Vec(left, 113.0), "ACCEL");
		labels->add(Vec(right, 113.0), "NOTES");
		addChild(labels);

		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(mm2px(Vec(left, 20.0)), module, TStick::LISTEN_PARAM, TStick::LISTEN_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(right, 20.0)), module, TStick::RECEIVE_LIGHT));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(left, 34.0)), module, TStick::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(right, 34.0)), module, TStick::GROUP_PARAM));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(left, 52.0)), module, TStick::SQUEEZE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(right, 52.0)), module, TStick::TOUCH_GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(left, 68.0)), module, TStick::TOUCH_LOW_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(right, 68.0)), module, TStick::TOUCH_HIGH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(left, 88.0)), module, TStick::PITCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(right, 88.0)), module, TStick::YAW_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(left, 102.0)), module, TStick::ROLL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(right, 102.0)), module, TStick::TOTAL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(left, 119.0)), module, TStick::ACCEL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(right, 119.0)), module, TStick::NOTE_GATES_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		TStick* module = getModule<TStick>();

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("OSC (press Enter to apply)"));
		menu->addChild(createMenuLabel(module->getStatus()));

		std::string ip;
		int port;
		{
			std::lock_guard<std::mutex> lock(module->configMutex);
			ip = module->listenIp;
			port = module->listenPort;
		}
		menu->addChild(createSubmenuItem("Listen IP", ip, [=](Menu* menu) {
			menu->addChild(new TStickAddressField(module, false));
		}));
		menu->addChild(createSubmenuItem("Listen port", std::to_string(port), [=](Menu* menu) {
			menu->addChild(new TStickAddressField(module, true));
		}));
	}
};


Model* modelTStick = createModel<TStick, TStickWidget>("Mytstick");
