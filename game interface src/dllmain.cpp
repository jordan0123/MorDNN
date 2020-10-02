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

#include <cstddef>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>
#include <exception>
#include <mutex>
#include <future>
#include <fstream>

// =============================== Macros

#define NullCheck(x) if(x == nullptr) { Write(#x " is null"); return; }
#define NullCheckRetVal(x, v) if(x == nullptr) { Write(#x " is null"); return v; }
#define NullCheckRetFalse(x) NullCheckRetVal(x, false);
#define NullCheckCont(x) if(x == nullptr) { continue; }

// ===============================

using namespace std::chrono_literals;

FILE* fDummy;
SDK::UWorld* pUWorld = NULL;

std::ofstream dataFile;

bool ai_enabled = false;
bool record_enabled = false;
bool recording = false;

// pointer to current enemy
AMordhauCharacter* target = NULL;

void Write(std::string && s) {
    std::cout << "[MordhAI]: " << s << std::endl;
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
    NullCheckRetFalse(g_names);

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
    NullCheckRetFalse(g_objects);

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
    NullCheckRetFalse(g_total_objects);

    Write("SDK initialized.");
    return true;
}

// returns a pointer to my character
SDK::AMordhauCharacter* GetLocalCharacter()
{
    using namespace SDK;

    auto game_instance = pUWorld->OwningGameInstance;
    NullCheckRetVal(game_instance, nullptr)

    auto local_player = game_instance->LocalPlayers[0];
    NullCheckRetVal(local_player, nullptr)

    auto local_player_controller = static_cast<AMordhauPlayerController*>(local_player->PlayerController);
    NullCheckRetVal(local_player_controller, nullptr)

    AMordhauCharacter* local_mordhau_character = static_cast<AMordhauCharacter*>(local_player_controller->AcknowledgedPawn);
    NullCheckRetVal(local_mordhau_character, nullptr)

    return local_mordhau_character;
}

// returns pointer to nearest enemy
AMordhauCharacter* GetNearestPlayer()
{
    AMordhauCharacter* nearest_player = NULL;

    auto persistent_level = pUWorld->PersistentLevel;
    NullCheckRetVal(persistent_level, nearest_player);

    auto game_instance = pUWorld->OwningGameInstance;
    NullCheckRetVal(game_instance, nearest_player);

    auto local_player = game_instance->LocalPlayers[0];
    NullCheckRetVal(local_player, nearest_player);

    auto local_player_controller = static_cast<AMordhauPlayerController*>(local_player->PlayerController);
    NullCheckRetVal(local_player_controller, nearest_player);

    auto local_mordhau_character = static_cast<AMordhauCharacter*>(local_player_controller->AcknowledgedPawn);
    NullCheckRetVal(local_mordhau_character, nearest_player);

    auto local_mordhau_player_state = static_cast<AMordhauPlayerState*>(local_mordhau_character->PlayerState);
    NullCheckRetVal(local_mordhau_player_state, nearest_player);

    auto local_root_comp = local_mordhau_character->RootComponent;
    NullCheckRetVal(local_root_comp, nearest_player);

    float nearest_player_distance = 0;

    // for each actor in the level
    for (int i = 0; i < persistent_level->GetActors().Num(); i++)
    {
        auto curr_actor = persistent_level->GetActors()[i];
        NullCheckCont(curr_actor);

        // skip if me
        if (curr_actor == local_mordhau_character) continue;

        // players
        if (curr_actor->IsA(AMordhauCharacter::StaticClass()))
        {
            auto curr_aadvancedcharacter = static_cast<AAdvancedCharacter*>(curr_actor);
            NullCheckCont(curr_aadvancedcharacter);

            auto curr_mordhau_character = static_cast<AMordhauCharacter*>(curr_aadvancedcharacter);
            NullCheckCont(curr_mordhau_character);

            auto curr_actor_root_comp = curr_actor->RootComponent;
            NullCheckCont(curr_actor_root_comp);

            if (curr_aadvancedcharacter->bIsDead) continue;

            float distance = dist(local_root_comp->RelativeLocation, curr_actor_root_comp->RelativeLocation);

            if (nearest_player_distance == 0 || distance < nearest_player_distance)
            {
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
float getRelativeYaw(FVector enemy_pos, FVector my_pos, FVector forward) // relative to nearest player, 0 = facing them
{

    float yaw_to_target = atan2(enemy_pos.Y - my_pos.Y, enemy_pos.X - my_pos.X);
    float yaw_look = atan2(forward.Y, forward.X);

    return normalizeAngle((yaw_look - yaw_to_target) / 3.1416f * 180);
}

// returns a vector of the game state
std::vector<float> GetState(AMordhauCharacter* target_player, AMordhauCharacter* me)
{
    std::vector<float> state;

    if (target_player != NULL)
    {
        FVector my_pos = me->RootComponent->RelativeLocation;
        FVector forward = me->RootComponent->GetForwardVector();
        FVector my_vel = me->RootComponent->ComponentVelocity;

        FVector enemy_pos = target_player->RootComponent->RelativeLocation;
        FVector enemy_vel = target_player->RootComponent->ComponentVelocity;

        // joint positions of the enemy
        FVector bone_positions[9];

        bone_positions[0] = GetBonePos(target_player->Mesh, BONE_IDS::Hips);
        bone_positions[1] = GetBonePos(target_player->Mesh, BONE_IDS::head);
        bone_positions[2] = GetBonePos(target_player->Mesh, BONE_IDS::LeftHand);
        bone_positions[3] = GetBonePos(target_player->Mesh, BONE_IDS::LeftForearm);
        bone_positions[4] = GetBonePos(target_player->Mesh, BONE_IDS::LeftArm);
        bone_positions[5] = GetBonePos(target_player->Mesh, BONE_IDS::RightHand);
        bone_positions[6] = GetBonePos(target_player->Mesh, BONE_IDS::RightForearm);
        bone_positions[7] = GetBonePos(target_player->Mesh, BONE_IDS::RightArm);
        bone_positions[8] = GetBonePos(target_player->Mesh, BONE_IDS::RightWeapon);

        float position_scaling_factor = 1 / 10.f; // usually 300

        // add bone positions to the game state vector
        for (int i = 0; i < 9; i++)
        {
            // transform each vector
            FVector p = toLocal_MeToEnemy(my_pos, enemy_pos, bone_positions[i]);

            state.push_back(p.X * position_scaling_factor);
            state.push_back(p.Y * position_scaling_factor);
            state.push_back(p.Z * position_scaling_factor);
        }

        float vel_scaling_factor = 1 / 10.f; // max of about 500, usually 200-300

        // their velocity relative to the local coordinate plane between me and the enemy
        FVector enemy_vel_local = toLocal_MeToEnemy(my_pos, enemy_pos, enemy_vel + my_pos);

        state.push_back(enemy_vel_local.X * vel_scaling_factor);
        state.push_back(enemy_vel_local.Y * vel_scaling_factor);
        state.push_back(enemy_vel_local.Z * vel_scaling_factor);

        // stage of an attack
        float attack_stage_scaling_factor = 10.f;
        EAttackStage stage = static_cast<EAttackStage>(-1);

        auto motion = target_player->Motion;
        if (motion != nullptr)
        {
            if (motion->IsA(UAttackMotion::StaticClass()))
            {
                stage = static_cast<UAttackMotion*>(motion)->Stage;
            }
            else
            {
                stage = static_cast<EAttackStage>(-1);
            }
        }

        state.push_back((stage == EAttackStage::EAttackStage__Windup) * attack_stage_scaling_factor);
        state.push_back((stage == EAttackStage::EAttackStage__Release) * attack_stage_scaling_factor);
        state.push_back((stage == EAttackStage::EAttackStage__Recovery) * attack_stage_scaling_factor);

        motion = me->Motion;
        stage = static_cast<EAttackStage>(-1);

        if (motion != nullptr)
        {
            if (motion->IsA(UAttackMotion::StaticClass()))
            {
                stage = static_cast<UAttackMotion*>(motion)->Stage;
            }
            else
            {
                stage = static_cast<EAttackStage>(-1);
            }
        }

        state.push_back((stage == EAttackStage::EAttackStage__Release) * attack_stage_scaling_factor);
        state.push_back((stage == EAttackStage::EAttackStage__Recovery) * attack_stage_scaling_factor);
    }

    float stamina_scaling_factor = 1 / 10.f;

    state.push_back(me->Stamina * stamina_scaling_factor);

    return state;
}

// Record game state and outputs
void RecordData()
{
    auto me = GetLocalCharacter();

    auto enemy = target;

    if (me == nullptr || me->bIsDead)
    {
        Write("Recording paused: You are dead");
        target = nullptr; // reset the target to null
        return;
    }

    if (enemy == nullptr || enemy->bIsDead)
    {
        Write("Recording paused: No target");
        target = nullptr; // reset the target to null
        return;
    }

    // if recording is enabled but not actively recording yet
    if (!recording)
    {
        time_t now = time(0);

        // create new data file
        dataFile = std::ofstream("C:\\Users\\Jordan\\Desktop\\data\\" + std::to_string(now) + ".txt");

        Write("Created new data file");
        recording = true;
    }

    // inputs
    std::vector<float> state = GetState(enemy, me);
    assert(state.size() > 0);

    // write the state to the file
    for (int i = 0; i < state.size(); i++)
    {
        dataFile << state[i] << " ";
    }

    // outputs
    dataFile << "\n";

    int wheel_delta = GetAndClearWheelDelta();

    // TO DO: rewrite this part for control recording to read control configuration from config file

    // LMB, RMB, MWUP, MWDOWN, Thumb1, Thumb2, WASD, Q, F, Space, Shift, Alt, CTRL
    dataFile <<
        ((GetKeyState(VK_LBUTTON) & 0x80) != 0) << " " << ((GetKeyState(VK_RBUTTON) & 0x80) != 0) << " " // LMB, RMB
        << (wheel_delta > 0) << " " << (wheel_delta < 0) << " " // MWUP, MWDOWN
        << (GetKeyState(VK_XBUTTON1) < 0) << " " << (GetKeyState(VK_XBUTTON2) < 0) << " " // Thumb1, Thumb2
        << (GetAsyncKeyState(0x57) != 0) << " " << (GetAsyncKeyState(0x41) != 0) << " " // W, A
        << (GetAsyncKeyState(0x53) != 0) << " " << (GetAsyncKeyState(0x44) != 0) << " " // S, D
        << (GetAsyncKeyState(0x51) != 0) << " " << (GetAsyncKeyState(0x46) != 0) << " " // Q, F
        << (GetAsyncKeyState(VK_SPACE) != 0) << " " << (GetAsyncKeyState(VK_SHIFT) != 0) << " " // Space, Shift
        << (GetAsyncKeyState(VK_MENU) != 0) << " " << (GetAsyncKeyState(VK_CONTROL) != 0) << " " // Alt, CTRL
        << "\n"
        << getRelativeYaw(enemy->RootComponent->RelativeLocation, me->RootComponent->RelativeLocation, me->RootComponent->GetForwardVector()) << " " << me->LookUpValue
        << "\n";
}

// Run the AI
void AI()
{
    // load neural net from file
    static auto model = fdeep::load_model("C:\\Users\\Jordan\\Desktop\\frugally-deep-master\\mordhai.json");

    auto me = GetLocalCharacter();
    NullCheck(me);

    auto enemy = target;

    if (target == nullptr || target->bIsDead)
    {
        Write("AI has no target");
        return;
    }

    // current state
    std::vector<float>in = GetState(enemy, me);
    // ensure the number of features in the state is correct
    if (in.size() < 36) return;

    // send input tensor to the neural network and get result
    auto result = model.predict_stateful( { fdeep::tensor(fdeep::tensor_shape(1, 36), in) } );

    // display the output of the neural network
    std::cout << fdeep::show_tensors(result) << std::endl;

    // output tensor contains two vectors for probabilistic and regression outputs
    auto out1 = result[0].to_vector(); // control outputs (probabilistic, 0 to 1.0)
    auto out2 = result[1].to_vector(); // player orientation output (regression, unbounded)

    // out1 tensor order: LMB, RMB, MWUP, MWDOWN, Thumb1, Thumb2, WASD, Q, F, Space, Shift, Alt, CTRL

    // prioritize attacking over blocking
    if (out1[0] > 0.2) // if output is greater than threshold,
    {
        LeftClick();
    }
    else if (out1[1] > 0.2) { // block and dont unblock until the activation dies down to below 0.05
        SetMouseClickState(0, 1);
    }
    else if(out1[1] < 0.05) {
        SetMouseClickState(0, 0);
    }

    // perform overhead and stab attacks
    if (out1[2] > 0.2) ScrollMouse(120);
    if (out1[3] > 0.2) ScrollMouse(-120);

    // keystroke emulation

    INPUT ip;
    ZeroMemory(&ip, sizeof(ip));
    // Set up a generic keyboard event.
    ip.type = INPUT_KEYBOARD;
    ip.ki.wScan = 0; // hardware scan code for key
    ip.ki.time = 0;
    ip.ki.dwExtraInfo = 0;

    // Thumb1, Thumb2, WASD, Q, F, Space, Shift, Alt, CTRL (sprint and thumb are rebound in game)
    static const unsigned keys[] = { 0x4A, 0x4C, 0x57, 0x41, 0x53, 0x44, 0x51, 0x46, VK_SPACE, 0x48, VK_MENU, VK_CONTROL };

    for (int keys_ix = 0; keys_ix < 12; keys_ix++)
    {
        float threshold = 0.5;

        // specific threshold values for certain outputs
        if (keys_ix == 10) threshold = 0.1;
        if (keys_ix == 3) threshold = 0.1;
        if (keys_ix == 0) threshold = 0.1;

        ip.ki.wVk = keys[keys_ix];
        ip.ki.dwFlags = out1[keys_ix + 4] > threshold ? 0 : KEYEVENTF_KEYUP;
        SendInput(1, &ip, sizeof(INPUT));
    }

    float goal_rel_yaw = out2[0];
    float goal_pitch = out2[1];

    // move the mouse to reach the goal angle
    
    float cur_rel_yaw = getRelativeYaw(enemy->RootComponent->RelativeLocation, me->RootComponent->RelativeLocation, me->RootComponent->GetForwardVector());
    float cur_pitch = me->LookUpValue;
    
    MoveMouse((cur_rel_yaw - goal_rel_yaw) * 5, (cur_pitch - goal_pitch) * 5);

}

void MainLoop()
{
    // translator to convert Win32 exceptions to C++ exceptions
    Scoped_SE_Translator translator{ SETranslator };

    // launch thread to handle windows mouse messages
    std::thread mouse_hook(UpdateMouseState);

    while (!quit)
    {
        auto loop_start_time = std::chrono::system_clock::now();

        // TO DO: use toggles rather than separate on and off keys

        // terminate the program
        if (GetAsyncKeyState(VK_SCROLL)) {
            quit = true;
            break;
        }
        // record data to file
        if (GetAsyncKeyState(VK_F2))
        {
            Write("Recording started");
            record_enabled = true;
        }
        // stop recording and save file
        if (GetAsyncKeyState(VK_F3))
        {
            Write("Recording stopped");
            record_enabled = false;
            if (recording)
            {
                recording = false;
                dataFile.close();
                Write("Data file written");
            }
        }

        if (GetAsyncKeyState(VK_F9))
        {
            ai_enabled = true;
            Write("AI enabled");
        }
        if (GetAsyncKeyState(VK_F10))
        {
            ai_enabled = false;
            Write("AI disabled");
        }

        // functions in here can potentially result in segfaults
        try {
            if (GetAsyncKeyState(VK_F1)) {
                Write("Reinitializing SDK.");
                InitMordhauSDK();
            }

            if (GetAsyncKeyState(VK_F8)) {
                Write("Target updated");
                target = GetNearestPlayer();
            }

            if (record_enabled) RecordData();
            if (ai_enabled) AI();
        }
        catch (const SE_Exception & e)
        {
            Write("Caught SE Exception " + std::to_string(e.getSeNumber()));
            Write("Reinitializing SDK.");
            std::this_thread::sleep_for(250ms);
            InitMordhauSDK();
        }

        // ensure constant loop time
        auto loop_time = std::chrono::system_clock::now() - loop_start_time;
        if (loop_time < 20ms) std::this_thread::sleep_for(20ms - loop_time); 
    }

    mouse_hook.join();
}

DWORD APIENTRY Main(LPVOID) {
    SetupConsole();

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
