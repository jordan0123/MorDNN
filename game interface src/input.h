#pragma once

#include <variant>
#include <string>
#include <string_view>
#include <fstream>
#include <exception>
#include <vector>
#include <map>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <utility>
#include <array>
#include <initializer_list>
#include <algorithm>
#include <random>

using namespace std::literals;

// worlds ugliest forwarding declaration, consider this a gift from dwm
void Write(std::string_view s);
std::ostream& WriteEx();

short GetWheelDelta();
void ClearWheelDelta();
void MoveMouse(long, long);

class Input {
	struct ScrollInput {
		enum class Direction {
			Up, Down,
		};
		Direction direction;
	};
	struct VKeyInput {
		using KeyType = SHORT;
		KeyType key = -1;
	};
	using SensType = float;

	class StateVisitor {
	public:
		using OutType = bool;
		OutType operator()(const VKeyInput& key) {
			if (key.key == -1) return false;
			return HIWORD(GetAsyncKeyState(key.key)) != 0;
		}
		OutType operator()(const ScrollInput& scroll) {
			short wheel = GetWheelDelta();
			return scroll.direction == ScrollInput::Direction::Up ? (wheel > 0) : (wheel < 0);
		}
	};
	class PrintVisitor {
		constexpr static std::size_t bufsize = 256;
		using BufType = char;
		BufType buf[bufsize];
		std::string_view key_str;
	public:
		using OutType = std::string;
		OutType operator()(const VKeyInput& key) {
			if (key.key == -1) return "Unbound";
			ZeroMemory(buf, bufsize * sizeof(BufType));

			unsigned int sc = MapVirtualKeyA(key.key, MAPVK_VK_TO_VSC);
			sc <<= 16;
			sc |= 0x1 << 25;  // <- don't care

			// Convert to ANSI string
			if (GetKeyNameTextA(sc, buf, bufsize) > 0)
				return std::string(buf);

			switch (key.key) {
			case VK_LBUTTON:
				return "LMB";
			case VK_RBUTTON:
				return "RMB";
			case VK_MBUTTON:
				return "MMB";
			case VK_XBUTTON1:
				return "M3";
			case VK_XBUTTON2:
				return "M4";
			default:
				return "???";
			}
		}
		OutType operator()(const ScrollInput& scroll) {
			return (scroll.direction == ScrollInput::Direction::Up ? "ScrollUp" : "ScrollDown");
		}
	};

	using Bind_t = std::variant<VKeyInput, ScrollInput>;

	Bind_t Stab240;
	Bind_t StabLeft;
	Bind_t StabRight;

	Bind_t Slash240;
	Bind_t SlashUR;
	Bind_t SlashR;
	Bind_t SlashLR;
	Bind_t SlashLL;
	Bind_t SlashL;
	Bind_t SlashUL;

	Bind_t Kick;

	Bind_t Feint;
	Bind_t Parry;

	Bind_t Flip;

	Bind_t Forward;
	Bind_t Left;
	Bind_t Back;
	Bind_t Right;

	Bind_t Jump;
	Bind_t Crouch;
	Bind_t Sprint;

	SensType SensitivityX;
	SensType SensitivityY;

	Input(std::ifstream infile) {
		using MouseMap = std::map<std::string_view, SensType&>;
		using ActionMap = std::map<std::string_view, Bind_t&>;
		using MovePair = std::pair<std::string_view, int>;
		using MoveMap = std::map<MovePair, Bind_t&>;
		using VKMap = std::map<std::string_view, VKeyInput::KeyType>;

		static const VKMap KeyToVK = {
			{"F1", VK_F1},
			{"F2", VK_F2},
			{"F3", VK_F3},
			{"F4", VK_F4},
			{"F5", VK_F5},
			{"F6", VK_F6},
			{"F7", VK_F7},
			{"F8", VK_F8},
			{"F9", VK_F9},
			{"F10", VK_F10},

			{"LeftMouseButton", VK_LBUTTON},
			{"RightMouseButton", VK_RBUTTON},
			{"MiddleMouseButton", VK_MBUTTON},
			{"ThumbMouseButton", VK_XBUTTON1},
			{"ThumbMouseButton2", VK_XBUTTON2},

			{"LeftAlt", VK_MENU},
			{"RightAlt", VK_MENU},
			{"SpaceBar", VK_SPACE},
			{"Escape", VK_ESCAPE},
			{"Tab", VK_TAB},
			{"RightControl", VK_RCONTROL},
			{"LeftControl", VK_LCONTROL},
			{"RightShift", VK_RSHIFT},
			{"LeftShift", VK_LSHIFT},

			{"Zero", '0'},
			{"One", '1'},
			{"Two", '2'},
			{"Three", '3'},
			{"Four", '4'},
			{"Five", '5'},
			{"Six", '6'},
			{"Seven", '7'},
			{"Eight", '8'},
			{"Nine", '9'},
		};

		// Read from ActionMapping
		const ActionMap action_map = {
			{"Stab"sv, Stab240},
			{"Left Stab"sv, StabLeft},
			{"Right Stab"sv, StabRight},
			{"Strike"sv, Slash240},
			{"Right Upper Strike"sv, SlashUR},
			{"Right Strike"sv, SlashR},
			{"Right Lower Strike"sv, SlashLR},
			{"Left Lower Strike"sv, SlashLL},
			{"Left Strike"sv, SlashL},
			{"Left Upper Strike"sv, SlashUL},
			{"Kick"sv, Kick},
			{"Feint"sv, Feint},
			{"Parry"sv, Parry},
			{"Flip Attack Side"sv, Flip},
			{"Jump"sv, Jump},
			{"Crouch"sv, Crouch},
			{"Sprint"sv, Sprint},
		};

		// Read sensitivity from AxisConfig
		const MouseMap mouse_map = {
			{"MouseX"sv, SensitivityX},
			{"MouseY"sv, SensitivityY},
		};

		// Read value and scale from AxisMappings
		const MoveMap move_map = {
			{MovePair("Move Forward"sv, 1), Forward},
			{MovePair("Move Forward"sv, -1), Back},
			{MovePair("Move Right"sv, 1), Right},
			{MovePair("Move Right"sv, -1), Left},
		};

		// Don't have str.starts_with until C++20
		auto starts_with = [](std::string_view str, std::string_view with) -> bool {
			return str.find(with.data(), 0, with.length()) == 0;
		};

		// val_of("...Key=Val...","Key") -> "Val"
		auto val_of = [](std::string_view str, std::string_view key) -> std::string_view {
			std::size_t pos, pos2;
			if ((pos = str.find(key)) == std::string_view::npos) return std::string_view(str.data(), 0);
			if ((pos = str.find("="sv, pos + key.length())) == std::string_view::npos) return std::string_view(str.data(), 0);
			if ((pos2 = str.find_first_of(",)"sv, pos + 1)) == std::string_view::npos) return std::string_view(str.data(), 0);
			return str.substr(pos + 1, pos2 - (pos + 1));
		};

		// Thin out string by amt, meant for trimming quote
		auto trim = [](std::string_view str, std::size_t amt = 1) {
			if (str.length() == 0) return std::string_view(str.data(), 0);
			return str.substr(amt, str.length() - (2 * amt));
		};

		auto bind_key = [&val_of, &starts_with](Bind_t& bind, std::string_view key) -> bool {
			// First look for None, discard if found
			if (key == "None"sv) return false;
			// Then look for Gamepad_, discard if found
			if (starts_with(key, "Gamepad_"sv)) return false;

			// Then check for A-Z, use if found
			if (key.length() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
				bind = VKeyInput{ static_cast<VKeyInput::KeyType>(key[0]) };
				return true;
			}
			// Then check if within map, use if found
			else {
				auto vk = KeyToVK.find(key);
				if (vk != KeyToVK.end()) {
					// Use key from map
					bind = VKeyInput{ vk->second };
					return true;
				}
				else {
					// Then check if scroll, use if found
					if (starts_with(key, "MouseScroll"sv)) {
						if (key.find("Up"sv, "MouseScroll"sv.length()) != std::string_view::npos) {
							bind = ScrollInput{ ScrollInput::Direction::Up };
							return true;
						}
						else {
							bind = ScrollInput{ ScrollInput::Direction::Down };
							return true;
						}
					}
					else {
						// Skip otherwise
						return false;
					}
				}
			}
			return false;
		};

		std::string line;
		while (std::getline(infile, line) && line != "") {
			if (starts_with(line, "AxisConfig"sv)) {
				auto name = trim(val_of(line, "AxisKeyName"sv));
				if (name.length() == 0) continue;

				// Check for presence in mouse map
				auto it = mouse_map.find(name);
				if (it == mouse_map.end()) continue;

				// Get sensitivity
				auto sens = val_of(line, "Sensitivity"sv);
				if (sens.length() == 0) continue;

				// Put into bind (multiply by 15 to get real value -- mordhau makes in-game 1.0 be ini 0.0666
				auto& bind = it->second;
				bind = 15.0f * static_cast<float>(std::atof(sens.data()));
			}
			else if (starts_with(line, "ActionMappings"sv)) {
				auto name = trim(val_of(line, "ActionName"sv));
				if (name.length() == 0) continue;

				// Check for presence in action map
				auto it = action_map.find(name);
				if (it == action_map.end()) continue;

				auto key = val_of(line, "Key"sv);
				auto& bind = it->second;
				bind_key(bind, key);
			}
			else if (starts_with(line, "AxisMappings"sv)) {
				auto name = trim(val_of(line, "AxisName"sv));
				if (name.length() == 0) continue;

				auto scale_s = val_of(line, "Scale"sv);
				if (scale_s.length() == 0) continue;
				int scale = static_cast<int>(std::round(std::atof(scale_s.data())));

				auto it = move_map.find(MovePair(name, scale));
				if (it == move_map.end()) {
					continue;
				}

				auto key = val_of(line, "Key"sv);
				auto& bind = it->second;
				bind_key(bind, key);
			}
		}
	}

	static const std::size_t NumInputs = 21;
	INPUT inputbuf[NumInputs] = { 0 };
	bool parryState;

	std::uniform_real_distribution<float> real_distribution;
	using EngineType = std::default_random_engine;
	EngineType gen = EngineType(std::random_device()());

	float random_real() {
		return real_distribution(gen);
	}
public:
	using StateType = std::array<StateVisitor::OutType, NumInputs>;

	StateType GetState() {
		const auto binds = {
			Stab240, StabLeft, StabRight,
			Slash240, SlashUR, SlashR, SlashLR, SlashLL, SlashL, SlashUL,
			Kick, Feint, Parry, Flip,
			Forward, Left, Back, Right,
			Jump, Crouch, Sprint
		};
		StateType ret;
		std::size_t index = 0;
		std::for_each(binds.begin(), binds.end(), [&index, &ret](const Bind_t& bind) {
			ret[index++] = std::visit(StateVisitor(), bind);
		});
		// UGLY!!!
		ClearWheelDelta();
		return ret;
	}

	void PrintKeys() {
		using Pair = std::pair<std::string_view, Bind_t&>;
		const std::array<Pair, NumInputs> binds = {
			Pair("Stab 240", Stab240), Pair("Stab Left", StabLeft), Pair("Stab Right", StabRight),
			Pair("Slash 240", Slash240), Pair("Slash Upper-right", SlashUR), Pair("Slash Right", SlashR),
			Pair("Slash Lower-right", SlashLR), Pair("Slash Lower-left", SlashLL), Pair("Slash Left", SlashL), Pair("Slash Upper-left", SlashUL),
			Pair("Kick", Kick), Pair("Feint", Feint), Pair("Parry", Parry), Pair("Flip", Flip),
			Pair("Forward", Forward), Pair("Left", Left), Pair("Back", Back), Pair("Right", Right),
			Pair("Jump", Jump), Pair("Crouch", Crouch), Pair("Sprint", Sprint)
		};
		std::for_each(binds.begin(), binds.end(), [](const Pair& pair) {
			WriteEx() << pair.first << ": " << std::visit(PrintVisitor(), pair.second) << '\n';
		});
	}

	template<class FloatType>
	void ProduceInputs(const std::vector<FloatType>& model_inputs) {
		/*
		Stab240 -- Insert
		StabLeft -- Home
		StabRight -- NumPadDec
		Slash240 -- NumPadZero
		SlashUR -- NumPadOne
		SlashR -- NumPadTwo
		SlashLR -- NumPadThree
		SlashLL -- NumPadFour
		SlashL -- NumPadFive
		SlashUL -- NumPadSix
		Kick -- NumPadSeven
		Feint -- NumPadEight
		Parry -- NumPadNine
		Flip -- NumpadSub
		Forward -- ArrowUp
		Left -- ArrowLeft
		Back -- ArrowDown
		Right -- ArrowRight
		Jump -- NumPadAdd
		Crouch -- NumPadMul
		Sprint -- NumPadDiv
		*/
		std::array<VKeyInput::KeyType, NumInputs> keys{
			// Stab 240, Left, Right
			VK_INSERT, VK_HOME, VK_DECIMAL,

			// Slash 240, Upper-right, Right
			VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2,
			// Slash Lower-right, Lower-left, Left
			VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5,
			// Slash upper-left
			VK_NUMPAD6,

			// Kick, Feint
			VK_NUMPAD7, VK_NUMPAD8,
			// Parry, Flip-attack
			VK_NUMPAD9, VK_SUBTRACT,
			// Forward, Left, Back, Right
			VK_UP, VK_LEFT, VK_DOWN, VK_RIGHT,

			// Jump, Crouch, Sprint
			VK_ADD, VK_MULTIPLY, VK_DIVIDE,
		};

		auto pushInput = [this](VKeyInput::KeyType key, std::size_t index, bool down) {
			auto& input = inputbuf[index];
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = key;
			input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
		};

		ZeroMemory(inputbuf, NumInputs * sizeof(INPUT));

		for (std::size_t i = 0; i < NumInputs; ++i) {
			const auto& input = model_inputs[i];
			const auto& key = keys[i];

			bool active = random_real() < input;

			if (key == VK_NUMPAD9) {
				auto& current = parryState;

				if (current != active) pushInput(key, i, active);

				if (active) current = true;
				if (input < 0.1) current = false;
			}
			else {
				pushInput(key, i, active);
			}
		}

		SendInput(NumInputs, inputbuf, sizeof(INPUT));
	}

	// Send ups for all keys
	void CeaseInputs() {
		std::array<VKeyInput::KeyType, NumInputs> keys{
			// Stab 240, Left, Right
			VK_INSERT, VK_HOME, VK_DECIMAL,

			// Slash 240, Upper-right, Right
			VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2,
			// Slash Lower-right, Lower-left, Left
			VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5,
			// Slash upper-left
			VK_NUMPAD6,

			// Kick, Feint
			VK_NUMPAD7, VK_NUMPAD8,
			// Parry, Flip-attack
			VK_NUMPAD9, VK_SUBTRACT,
			// Forward, Left, Back, Right
			VK_UP, VK_LEFT, VK_DOWN, VK_RIGHT,

			// Jump, Crouch, Sprint
			VK_ADD, VK_MULTIPLY, VK_DIVIDE,
		};
		static INPUT inputbuf[NumInputs] = {};
		for (std::size_t index = 0; index < NumInputs; ++index) {
			auto& input = inputbuf[index];
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = keys[index];
			input.ki.dwFlags = KEYEVENTF_KEYUP;
		}
		SendInput(NumInputs, inputbuf, sizeof(INPUT));
		parryState = false;
	}

	void SendMouse(double x, double y) {
		x /= SensitivityX;
		y /= SensitivityY;
		MoveMouse(static_cast<long>(x), static_cast<long>(y));
	}

	static Input FromInputIni(std::ifstream&& infile) {
		// Extract and discard lines until "[/Script/Mordhau.MordhauInput]" is reached
		// then process subsequent lines until "" is encountered

		if (!infile) throw std::runtime_error("File not found.");

		std::string line;

		while (std::getline(infile, line) && line != "[/Script/Mordhau.MordhauInput]");

		if (!infile) throw std::runtime_error("Input section not found within file.");

		return Input(std::move(infile));
	}

	Input() = default;
};