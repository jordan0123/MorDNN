#include "pch.h"

#include "SDK.h"
#include "SDK/CoreUObject_functions.cpp"
#include "SDK/BP_MordhauCharacter_functions.cpp"
#include "SDK/Engine_functions.cpp"
#include "SDK/Mordhau_functions.cpp"
#include "SDK/Basic.cpp"

#include "fdeep.hpp"

#include "globals.h"
#include "exceptions.h"
#include "mymath.h"
#include "bones.h"
#include "mouse.h"

#define NOMINMAX
#include "process.h"

#include "input.h"

#include <cstddef>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <ctime>
#include <exception>
#include <mutex>
#include <future>
#include <fstream>
#include <variant>
#include <sstream>

#pragma warning(disable : 4996) //_CRT_SECURE_NO_WARNINGS

// =============================== Macros

//#define NullCheck(x) if(x == nullptr) { Write(#x " is null"); return; }
//#define NullCheckVal(x, v) if(x == nullptr) { Write(#x " is null"); return v; }
//#define NullCheckFalse(x) NullCheckVal(x, false);
//#define NullCheckCont(x) if(x == nullptr) { continue; }

#define NullCheck(x) if(x == nullptr) { return; }
#define NullCheckVal(x, v) if(x == nullptr) { return v; }
#define NullCheckFalse(x) NullCheckVal(x, false);
#define NullCheckCont(x) if(x == nullptr) { continue; }

#define KP(k) (GetAsyncKeyState(k) & 0x8000)

// ===============================

using namespace SDK;
using namespace std::chrono_literals;

FILE* fDummy;
SDK::UWorld* pUWorld = NULL;

std::ofstream dataFile;

bool ai_enabled = false;
bool record_enabled = false;
bool recording = false;
bool auto_target = false;

// begin chaq mode
constexpr std::size_t bufsize = 256;
char userprofile[bufsize];
char localappdata[bufsize];

Input inputs;
// end chaq mode

// pointer to current enemy
AMordhauCharacter* target = nullptr;

std::ostream& WriteEx() {
	using Clock = std::chrono::system_clock;
	auto now = Clock::to_time_t(Clock::now());

	constexpr auto len = 32;
	char buf[len];

	auto localtime = std::localtime(&now);

	// Time is YYYYMMDD HH:MM:SS
	auto format = std::strftime(buf, len, "%Y%m%d %H:%M:%S", localtime);
	auto str = std::string_view(buf, format);

	std::cout << "[MordhAI][" << str << "]: ";
	return std::cout;
}

void Write(std::string_view s) {
	WriteEx() << s << '\n';
}

// Function which binds a timeout to a unique procedure. Not reentrant.
template<class Duration, class Procedure, class ...Types>
bool Timeout(Duration timeout, Procedure&& procedure) {
	using Clock = std::chrono::system_clock;

	static auto time = Clock::now();
	auto now = Clock::now();
	if (now >= time) {
		std::invoke(procedure);

		now = Clock::now();
		time = now + timeout;

		return true;
	}
	else {
		return false;
	}
}

// Function which binds a key and toggle mechanism to a unique procedure. Not reentrant.
// If you want to execute code on the rising and falling edges of the toggle, pass in a lambda
// which takes a boolean for the EdgeFunc parameter, otherwise pass a string or value to print
template<bool StartingValue = true, class KeyCode, class Procedure, class EdgeType>
void Toggle(KeyCode key, Procedure&& procedure, EdgeType&& edge) {
	static bool toggled = StartingValue;
	static bool keyPressed = false;

	if (keyPressed) {
		if (!KP(key)) keyPressed = false;
	}
	else if (KP(key)) {
		keyPressed = true;
		toggled = !toggled;

		if constexpr (std::is_invocable_v<EdgeType, bool>) {
			std::invoke(edge, toggled);
		}
		else {
			WriteEx() << edge << (toggled ? " enabled\n" : " disabled\n");
		}
	}

	if (toggled) {
		std::invoke(procedure);
	}
}

// Function which binds a key and latch mechanism to a unique procedure. Not reentrant.
// Procedure only executed when key is pressed down or up based on templated parameter.
template<bool ExecuteOnDown, class KeyCode, class Procedure>
void Pulse(KeyCode key, Procedure&& procedure) {
	static bool latched = false;

	if (latched) {
		if (!KP(key)) {
			latched = false;
			if constexpr (!ExecuteOnDown) std::invoke(procedure);
		}
	}
	else if (KP(key)) {
		latched = true;
		if constexpr (ExecuteOnDown) std::invoke(procedure);
	}
}

std::string HumanSize(std::size_t bytes) {
	std::stringstream ss;
	if (bytes < 1024) {
		ss << bytes << " B\n";
		return ss.str();
	}
	bytes >>= 10;

	auto prefixes = "KMGTPE";
	std::size_t pos = 0;
	while (bytes > (1 << 10)) {
		bytes >>= 10;
		++pos;
	}

	ss << bytes << prefixes[pos] << "iB";
	return ss.str();
}


void SetupConsole() {
	AllocConsole();
	freopen_s(&fDummy, "CONOUT$", "w", stdout);
}

void DestroyConsole() {
	fclose(fDummy);
	FreeConsole();
}

// initialize the SDK
bool InitMordhauSDK() {
	using namespace SDK;

	Write("Initializing SDK...");

	GetProcessInfo();

	DWORD64 uworld_addr = FindPattern(
		GameImageBase,
		GameModuleInfo.SizeOfImage,
		"\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x3B\x41\xB0\x01",
		"xxx????xxxxxxxx"
	);
	if (uworld_addr == 0) {
		Write("Failed to find UWorld!");
		return false;
	}
	while (true) {
		pUWorld = *reinterpret_cast<UWorld**>(uworld_addr + 7 + *reinterpret_cast<uint32_t*> (uworld_addr + 3));

		if (pUWorld != nullptr) break;

		std::this_thread::sleep_for(200ms);
	}

	// TODO: see if any of this stuff ever fails
	const DWORD64 gname_addr = FindPattern(
		GameImageBase,
		GameModuleInfo.SizeOfImage,
		"\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x75\x5F",
		"xxx????xxxxx"
	);
	if (gname_addr == 0) {
		Write("Failed to find GNames!");
		return false;
	}
	uint32_t offset = *(uint32_t*)(gname_addr + 3);
	FName::GNames = (TNameEntryArray*)(*(uintptr_t*)(gname_addr + 7 + offset));
	void* g_names = (void*)FName::GNames;
	NullCheckFalse(g_names);

	const DWORD64 objects_addr = FindPattern(
		GameImageBase,
		GameModuleInfo.SizeOfImage,
		"\x4C\x8B\x15\x00\x00\x00\x00\x8D\x43\x01",
		"xxx????xxx"
	);
	if (objects_addr == 0) {
		Write("Failed to find GObjects!");
		return false;
	}
	offset = *(int32_t*)(objects_addr + 3);
	TUObjectArray::g_objects = (uintptr_t)(objects_addr + 7 + offset) - (uintptr_t)GameImageBase;
	void* g_objects = (void*)TUObjectArray::g_objects;
	NullCheckFalse(g_objects);

	const DWORD64 total_objects_addr = FindPattern(
		GameImageBase,
		GameModuleInfo.SizeOfImage,
		"\x44\x8B\x15\x00\x00\x00\x00\x45\x33\xC9\x45\x85\xD2",
		"xxx????xxxxxx"
	);
	if (total_objects_addr == 0) {
		Write("Failed to find GTotalObjects!");
		return false;
	}
	offset = *(int32_t*)(total_objects_addr + 3);
	TUObjectArray::g_total_objects = (uintptr_t)(total_objects_addr + 7 + offset) - (uintptr_t)GameImageBase;
	void* g_total_objects = (void*)TUObjectArray::g_total_objects;
	NullCheckFalse(g_total_objects);

	Write("SDK initialized.");
	return true;
}

// returns a pointer to my character
AMordhauCharacter* GetLocalCharacter() {
	auto game_instance = pUWorld->OwningGameInstance;
	NullCheckVal(game_instance, nullptr);

	auto local_player = game_instance->LocalPlayers[0];
	NullCheckVal(local_player, nullptr);

	auto local_player_controller = static_cast<AMordhauPlayerController*>(local_player->PlayerController);
	NullCheckVal(local_player_controller, nullptr);

	AMordhauCharacter* local_mordhau_character = static_cast<AMordhauCharacter*>(local_player_controller->AcknowledgedPawn);
	NullCheckVal(local_mordhau_character, nullptr);

	return local_mordhau_character;
}

// returns pointer to nearest enemy
AMordhauCharacter* GetNearestPlayer() {
	AMordhauCharacter* nearest_player = nullptr;

	auto persistent_level = pUWorld->PersistentLevel;
	NullCheckVal(persistent_level, nearest_player);

	auto game_instance = pUWorld->OwningGameInstance;
	NullCheckVal(game_instance, nearest_player);

	auto local_player = game_instance->LocalPlayers[0];
	NullCheckVal(local_player, nearest_player);

	auto local_player_controller = static_cast<AMordhauPlayerController*>(local_player->PlayerController);
	NullCheckVal(local_player_controller, nearest_player);

	auto local_mordhau_character = static_cast<AMordhauCharacter*>(local_player_controller->AcknowledgedPawn);
	NullCheckVal(local_mordhau_character, nearest_player);

	auto local_mordhau_player_state = static_cast<AMordhauPlayerState*>(local_mordhau_character->PlayerState);
	NullCheckVal(local_mordhau_player_state, nearest_player);

	auto local_root_comp = local_mordhau_character->RootComponent;
	NullCheckVal(local_root_comp, nearest_player);

	float nearest_player_distance = 0;

	// for each actor in the level
	for (int i = 0; i < persistent_level->GetActors().Num(); i++) {
		auto curr_actor = persistent_level->GetActors()[i];
		NullCheckCont(curr_actor);

		// skip if me
		if (curr_actor == local_mordhau_character) continue;

		// players
		if (curr_actor->IsA(AMordhauCharacter::StaticClass())) {
			auto curr_aadvancedcharacter = static_cast<AAdvancedCharacter*>(curr_actor);
			NullCheckCont(curr_aadvancedcharacter);

			auto curr_mordhau_character = static_cast<AMordhauCharacter*>(curr_aadvancedcharacter);
			NullCheckCont(curr_mordhau_character);

			auto curr_actor_root_comp = curr_actor->RootComponent;
			NullCheckCont(curr_actor_root_comp);

			if (curr_aadvancedcharacter->bIsDead) continue;

			float distance = dist(local_root_comp->RelativeLocation, curr_actor_root_comp->RelativeLocation);

			if (nearest_player_distance == 0 || distance < nearest_player_distance) {
				nearest_player_distance = distance;
				nearest_player = curr_mordhau_character;
			}
		}
	}

	return nearest_player;
}

// sets my relative yaw to the target
void setRelativeYaw(FVector enemy_pos, FVector my_pos, float ang) // relative to nearest player, 0 = facing them
{
	auto persistent_level = pUWorld->PersistentLevel;
	NullCheck(persistent_level);

	auto game_instance = pUWorld->OwningGameInstance;
	NullCheck(game_instance);

	auto local_player = game_instance->LocalPlayers[0];
	NullCheck(local_player);

	auto local_player_controller = static_cast<AMordhauPlayerController*>(local_player->PlayerController);
	NullCheck(local_player_controller);

	float yaw = atan2(enemy_pos.Y - my_pos.Y, enemy_pos.X - my_pos.X);

	auto rot = FRotator{ 0, yaw / 3.1416f * 180 + ang, 0 };
	local_player_controller->ClientSetRotation(rot, false);
}

// returns my relative yaw between my position and the target
float getRelativeYaw(FVector to, FVector from, FVector forward) // relative to nearest player, 0 = facing them
{

	float yaw_to_target = atan2(to.Y - from.Y, to.X - from.X);
	float yaw_look = atan2(forward.Y, forward.X);

	return normalizeAngle((yaw_look - yaw_to_target) / 3.1416f * 180);
}

// returns a vector of the game state
std::vector<float> GetState(AMordhauCharacter* target_player, AMordhauCharacter* me) {
	std::vector<float> state;

	NullCheckVal(target_player, state);
	NullCheckVal(me, state);

	FVector my_pos = me->RootComponent->RelativeLocation;
	FVector my_forward = me->RootComponent->GetForwardVector();
	FVector my_vel = me->RootComponent->ComponentVelocity;

	FVector enemy_pos = target_player->RootComponent->RelativeLocation;
	FVector enemy_forward = target_player->RootComponent->GetForwardVector();
	FVector enemy_vel = target_player->RootComponent->ComponentVelocity;

	AMordhauWeapon* my_weapon = me->RightHandEquipment == nullptr ? nullptr : static_cast<AMordhauWeapon*>(me->RightHandEquipment);
	AMordhauWeapon* enemy_weapon = target_player->RightHandEquipment == nullptr ? nullptr : static_cast<AMordhauWeapon*>(target_player->RightHandEquipment);

	FVector positions[] = {
		GetBonePos(target_player->Mesh, BONE_IDS::Hips),		 // 0
		GetBonePos(target_player->Mesh, BONE_IDS::head),
		GetBonePos(target_player->Mesh, BONE_IDS::LeftHand),
		GetBonePos(target_player->Mesh, BONE_IDS::LeftForearm),
		GetBonePos(target_player->Mesh, BONE_IDS::LeftArm),
		GetBonePos(target_player->Mesh, BONE_IDS::RightHand),
		GetBonePos(target_player->Mesh, BONE_IDS::RightForearm),
		GetBonePos(target_player->Mesh, BONE_IDS::RightArm),
		GetBonePos(target_player->Mesh, BONE_IDS::RightWeapon),
		GetBonePos(me->Mesh, BONE_IDS::Hips),					// 9
		GetBonePos(me->Mesh, BONE_IDS::head),
		GetBonePos(me->Mesh, BONE_IDS::LeftHand),
		GetBonePos(me->Mesh, BONE_IDS::LeftForearm),
		GetBonePos(me->Mesh, BONE_IDS::LeftArm),
		GetBonePos(me->Mesh, BONE_IDS::RightHand),
		GetBonePos(me->Mesh, BONE_IDS::RightForearm),
		GetBonePos(me->Mesh, BONE_IDS::RightArm),
		GetBonePos(me->Mesh, BONE_IDS::RightWeapon),			// 17

		(my_weapon != nullptr) && me->Motion->IsA(UAttackMotion::StaticClass()) ? my_weapon->CurrentTraceEnd : my_pos,
		(enemy_weapon != nullptr) && target_player->Motion->IsA(UAttackMotion::StaticClass()) ? enemy_weapon->CurrentTraceEnd : my_pos // 19
	};

	// add positions to the game state
	for (int i = 0; i < 20; i++) {
		// transform each vector
		float position_scaling_factor = 1 / 10.f; // usually 300
		positions[i] = toLocal_MeToEnemy(my_pos, enemy_pos, positions[i]) * position_scaling_factor;

		state.push_back(positions[i].X);
		state.push_back(positions[i].Y);
		state.push_back(positions[i].Z);
	}

	FVector velocities[] =
	{
		enemy_vel + my_pos,
		my_vel + my_pos
	};


	// add velocities to the game state
	for (int i = 0; i < 2; i++) {
		// transform each vector
		float vel_scaling_factor = 1 / 10.f; // max of about 500, usually 200-300
		velocities[i] = toLocal_MeToEnemy(my_pos, enemy_pos, velocities[i]) * vel_scaling_factor;

		state.push_back(velocities[i].X);
		state.push_back(velocities[i].Y);
		state.push_back(velocities[i].Z);
	}

	// get the attack and parry stage from me and the enemy
	float attack_stage_scaling_factor = 10.f;
	float parry_stage_scaling_factor = 10.f;

	EAttackStage attack_stage = static_cast<EAttackStage>(-1);
	EParryStage parry_stage = static_cast<EParryStage>(-1);

	for (int i = 0; i < 2; i++)
	{
		auto motion = i == 0 ? target_player->Motion : me->Motion;

		if (motion != nullptr) {
			if (motion->IsA(UAttackMotion::StaticClass())) {
				attack_stage = static_cast<UAttackMotion*>(motion)->Stage;
			}
			else {
				attack_stage = static_cast<EAttackStage>(-1);
			}

			if (motion->IsA(UParryMotion::StaticClass())) {
				parry_stage = static_cast<UParryMotion*>(motion)->Stage;
			}
			else {
				parry_stage = static_cast<EParryStage>(-1);
			}
		}

		state.push_back((attack_stage == EAttackStage::EAttackStage__Windup) * attack_stage_scaling_factor);
		state.push_back((attack_stage == EAttackStage::EAttackStage__Release) * attack_stage_scaling_factor);
		state.push_back((attack_stage == EAttackStage::EAttackStage__Recovery) * attack_stage_scaling_factor);

		state.push_back((parry_stage == EParryStage::EParryStage__Parry) * parry_stage_scaling_factor);
		state.push_back((parry_stage == EParryStage::EParryStage__Recovery) * parry_stage_scaling_factor);
	}

	float stamina_scaling_factor = 1 / 10.f;
	state.push_back(me->Stamina * stamina_scaling_factor);

	float ang_scaling_factor = 1 / 10.f;
	state.push_back(getRelativeYaw(my_pos, enemy_pos, enemy_forward) * ang_scaling_factor);
	state.push_back(target_player->LookUpValue * ang_scaling_factor);
	//state.push_back(getRelativeYaw(enemy_pos, my_pos, my_forward) * ang_scaling_factor);
	//state.push_back(me->LookUpValue * ang_scaling_factor);

	//float cur_rel_yaw = getRelativeYaw(target_player->RootComponent->RelativeLocation, me->RootComponent->RelativeLocation, me->RootComponent->GetForwardVector());
	//float cur_pitch = me->LookUpValue;

	//state.push_back(cur_rel_yaw / 10.f);
	//state.push_back(cur_pitch / 10.f);

	// static const float feature_scalings[] = { 0.10340312399036221, 11.308342282749448, 6.056444444893202, 0.10808723571185938, 2.573297045494285, 0.46671917928084744, 0.11232211518991674, 1.0517522672232957, 0.7950540857931376, 0.1092012534114704, 1.272149450345089, 0.7709887285729515, 0.1069181227838103, 1.948537084095561, 0.572824985325233, 0.1132948190241344, 0.9793992928108622, 0.6962353139715866, 0.10900557210751796, 1.1375494455158264, 0.7870280418534624, 0.10726449050548179, 1.7505040162336427, 0.5645277208166568, 0.11312782731663094, 0.9728520160374646, 0.6812732320077297, 8.176935013356871, 12.796265390490257, 13.269140827163165, 2.013475204823333, 3.5709430082809805, 0.42269439782464147, 0.9230384466319836, 1.1422958453423546, 0.5592809861415196, 1.297280807721419, 1.3138081463949551, 0.5542543084293862, 1.8533591799483715, 1.68142485112555, 0.4623346924783219, 0.8939838737538963, 1.0523209744398183, 0.5361924088125248, 1.413011752192775, 1.324257614932814, 0.5851095496129459, 1.576211436672897, 2.053637709013365, 0.4820770532611439, 0.8927769488370992, 1.0333487910721602, 0.5328456638331119, 0.18305818322599857, 0.237325403520102, 1.0, 0.16738861297915467, 0.19489196559664473, 1.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 0.1 };

	// for (int i = 0; i < 71; i++) state[i] *= feature_scalings[i];

	return state;
}

// Record game state and outputs
void RecordData() {
	static std::queue<std::string> buffer;

	auto me = GetLocalCharacter();
	auto enemy = target;

	auto stop_recording = []() {
		if (recording) {
			recording = false;
			WriteEx() << "Data file written -- " << HumanSize(dataFile.tellp()) << '\n';
			dataFile.close();
		}
	};

	auto discard_buffer = []() {
		Write("Discarded buffer containing " + std::to_string(buffer.size()) + " samples");
		while (!buffer.empty()) buffer.pop();
	};

	auto flush_buffer = []() {
		// empty out the buffer to the file
		Write("Flushing buffer to data file");
		while (!buffer.empty()) {
			dataFile << buffer.front();
			buffer.pop();
		}
	};

	if (me == nullptr || me->bIsDead) {
		Timeout(500ms, []() { Write("Recording stopped: You are dead"); });
		target = nullptr; // reset the target to null

		if (recording) discard_buffer();
		stop_recording();
		return;
	}

	if (enemy == nullptr || enemy->bIsDead) {
		Timeout(500ms, []() { Write("Recording stopped: No target"); });
		target = nullptr; // reset the target to null

		if (recording) flush_buffer();
		stop_recording();
		return;
	}

	/*
	static unsigned char last_hp = 100;
	unsigned char hp = me->Health;
	if (hp < last_hp) {
		Write("Received damage");
		if (recording) discard_buffer();
		stop_recording();
	}
	last_hp = hp;
	*/

	// if recording is enabled but not actively recording yet
	if (!recording) {
		// ensure the buffer is clear before starting a new file
		if (!buffer.empty()) discard_buffer();

		time_t now = time(0);
		// create new data file
		dataFile = std::ofstream(userprofile + std::string("\\Desktop\\MorDNN\\MorDNN-") + std::to_string(now) + ".txt");

		Write("Created new data file");
		recording = true;
	}

	std::string cur_data;

	// get the game state
	std::vector<float> state = GetState(enemy, me);
	assert(state.size() > 0);

	// write it into file
	for (int i = 0; i < state.size(); i++) {
		cur_data += std::to_string(state[i]) + " ";
	}
	cur_data += '\n';

	// binary output -- input state
	/*
		Stab240, StabLeft, StabRight,
		Slash240, SlashUR, SlashR, SlashLR, SlashLL, SlashL, SlashUL,
		Kick, Feint, Parry, Flip,
		Forward, Left, Back, Right,
		Jump, Crouch, Sprint
	*/
	auto input_state = inputs.GetState();
	for (const auto& state : input_state) {
		cur_data += std::to_string(state) + " ";
	}
	cur_data += '\n';

	// angle output
	cur_data += std::to_string( getRelativeYaw(enemy->RootComponent->RelativeLocation, me->RootComponent->RelativeLocation, me->RootComponent->GetForwardVector()) ) + " "
		+ std::to_string(me->LookUpValue) + "\n";

	buffer.push(cur_data);

	if (buffer.size() > 150)
	{
		dataFile << buffer.front();
		buffer.pop();
	}

	// Tell filesize every 3 seconds
	Timeout(3s, []() {
		WriteEx() << HumanSize(dataFile.tellp()) << " written...\n";
	});
}

// Run the AI
void AI() {
	// load neural net from file
	static auto model = fdeep::load_model(std::string(userprofile) + "\\Desktop\\frugally-deep-master\\mordhai.json");

	auto me = GetLocalCharacter();
	NullCheck(me);

	auto enemy = target;

	if (target == nullptr || target->bIsDead) {
		Write("AI has no target");
		return;
	}

	// current state
	std::vector<float>in = GetState(enemy, me);

	// send input tensor to the neural network and get result
	auto result = model.predict_stateful({ fdeep::tensor(fdeep::tensor_shape(1, in.size()), in) });

	// display the output of the neural network
	std::cout << fdeep::show_tensors(result) << std::endl;

	// output for t+1 tensor contains two vectors for probabilistic and regression outputs
	auto next_out_buttons = result[0].to_vector(); // control outputs (probabilistic, 0 to 1.0)
	auto next_out_angles = result[1].to_vector(); // player orientation output (regression, unbounded)

	static auto cur_out_buttons = next_out_buttons;
	static auto cur_out_angles = next_out_angles;

	inputs.ProduceInputs(cur_out_buttons);

	// move the mouse to reach the goal angle
	float goal_rel_yaw = cur_out_angles[0];
	float goal_pitch = cur_out_angles[1];

	float cur_rel_yaw = getRelativeYaw(enemy->RootComponent->RelativeLocation, me->RootComponent->RelativeLocation, me->RootComponent->GetForwardVector());
	float cur_pitch = me->LookUpValue;

	constexpr double speed = 4;
	inputs.SendMouse((goal_rel_yaw - cur_rel_yaw) * speed, (cur_pitch - goal_pitch) * speed);

	cur_out_buttons = next_out_buttons;
	cur_out_angles = next_out_angles;
}

void MainLoop() {
	// translator to convert Win32 exceptions to C++ exceptions
	Scoped_SE_Translator translator{ SETranslator };

	// launch thread to handle windows mouse messages
	std::thread mouse_hook(UpdateMouseState);

	while (!quit) {
		auto loop_start_time = std::chrono::system_clock::now();

		// terminate the program
		if (GetAsyncKeyState(VK_SCROLL)) {
			quit = true;
			break;
		}

		if (GetAsyncKeyState(VK_F7))
		{
			auto_target = true;
		}

		Pulse<true>(VK_F2, []() {
			Write("Recording started");
			record_enabled = true;
		});
		Pulse<true>(VK_F3, []() {
			Write("Recording stopped");
			record_enabled = false;
			if (recording) {
				recording = false;
				WriteEx() << "Data file written -- " << HumanSize(dataFile.tellp()) << '\n';
				dataFile.close();
			}
		});

		Toggle<false>(VK_F4, []() {
			auto is = inputs.GetState();
			for (auto& i : is) {
				std::cout << i << ' ';
			}
			std::cout << std::endl;
		}, "State output");

		Pulse<true>(VK_F5, []() {
			inputs.PrintKeys();
		});

		// functions in here can potentially result in segfaults
		try {
			Pulse<true>(VK_F1, []() {
				InitMordhauSDK();
			});

			if (auto_target) target = GetNearestPlayer();

			Pulse<true>(VK_F8, []() {
				target = GetNearestPlayer();
				if (target == nullptr) {
					Write("No target found.");
				}
				else {
					auto target_player_state = static_cast<AMordhauPlayerState*>(target->PlayerState);
					auto name = target_player_state->PlayerNamePrivate.ToString();
					Write("Target updated: " + name);
				}
			});

			if (record_enabled) RecordData();

			Toggle<false>(VK_F9, []() {
				AI();
			},
				[](bool rising) {
				ai_enabled = rising;
				if (!rising) auto_target = false;
				WriteEx() << "AI " << (rising ? "enabled.\n" : "disabled.\n");
				inputs.CeaseInputs();
			});

			if (GetAsyncKeyState(VK_F6))
			{
				/*
				static float my_last_damage_time = 0;
				static float my_new_damage_time = 0;

				auto damage_history = GetLocalCharacter()->DamageHistory;

				if (damage_history.Num() > 0) my_new_damage_time = damage_history[0].Time;
				if (my_last_damage_time != my_new_damage_time || GetLocalCharacter()->bIsDead) std::cout << "damaged" << std::endl;

				my_last_damage_time = my_new_damage_time;
				*/
				auto me = GetLocalCharacter();

				static unsigned char last_hp = 100;
				auto hp = me->Health;
				if (hp < last_hp) Write("Damaged");
				last_hp = hp;
			}
		}
		catch (const SE_Exception & e) {
			Write("Caught SE Exception " + std::to_string(e.getSeNumber()));
			Write("Reinitializing SDK.");
			InitMordhauSDK();
		}

		// ensure constant loop time
		auto loop_time = std::chrono::system_clock::now() - loop_start_time;
		if (loop_time < 20ms) 
			std::this_thread::sleep_for(20ms - loop_time);
		else 
			Write("WARNING: Loop time exceeded required interval");
	}

	mouse_hook.join();
}

void GetEnvironmentVariables() {
	DWORD upr = GetEnvironmentVariableA("USERPROFILE", userprofile, bufsize);
	if (!upr) Write("Userprofile not found.");
	DWORD lar = GetEnvironmentVariableA("LOCALAPPDATA", localappdata, bufsize);
	if (!lar) Write("Local appdata not found.");

	inputs = Input::FromInputIni(std::ifstream(std::string(localappdata, lar) + std::string("\\Mordhau\\Saved\\Config\\WindowsClient\\Input.ini")));
}

DWORD APIENTRY Main(LPVOID) {
	SetupConsole();

	GetEnvironmentVariables();

	if (!InitMordhauSDK()) {
		Write("Mordhau SDK failed to init!");
	}

	Write("Main loop");
	MainLoop();
	Write("Post loop in main");

	Write("Finishing up...");
	std::this_thread::sleep_for(1s);

	DestroyConsole();

	FreeLibraryAndExitThread(Module, 0);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);

		Module = hModule;
		CloseHandle(CreateThread(NULL, NULL, Main, NULL, NULL, NULL));
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}