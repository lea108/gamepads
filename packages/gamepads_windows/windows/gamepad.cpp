#include <algorithm>
#include <ppl.h>
#include <vector>
#include <concrt.h>
#include <winerror.h>

#include "gamepad.h"
#include "utils.h"
#include <optional>
#include <GameInput.h>
#include <iomanip>
#include <sstream>
//#pragma comment(lib, "GameInput.lib")

Gamepads gamepads;

using namespace GameInput::v3;


static IGameInput* g_gameInput = nullptr;
static IGameInputDevice* g_gamepad = nullptr;

std::string get_button_name(uint32_t button) {
  switch (button) {
    case GameInputGamepadMenu: return "menu";
    case GameInputGamepadView: return "view";
    case GameInputGamepadA: return "a";
    case GameInputGamepadB: return "b";
    case GameInputGamepadX: return "x";
    case GameInputGamepadY: return "y";
    case GameInputGamepadDPadUp: return "dpadUp";
    case GameInputGamepadDPadDown: return "dpadDown";
    case GameInputGamepadDPadLeft: return "dpadLeft";
    case GameInputGamepadDPadRight: return "dpadRight";
    case GameInputGamepadLeftShoulder: return "leftShoulder";
    case GameInputGamepadRightShoulder: return "rightShoulder";
    case GameInputGamepadLeftThumbstick: return "leftThumbstick";
    case GameInputGamepadRightThumbstick: return "rightThumbstick";
  }
  return "button-" + std::to_string(button);
}

std::string AppLocalDeviceIdToString(const APP_LOCAL_DEVICE_ID& id) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < APP_LOCAL_DEVICE_ID_SIZE; ++i) {
        oss << std::setw(2) << static_cast<int>(id.value[i]);
    }
    return oss.str();
}

std::list<Event> diff_states(const GameInputDeviceInfo& device_info,
                                       const GameInputGamepadState& old,
                                       const GameInputGamepadState& current) {
  std::time_t now = std::time(nullptr);
  int time = static_cast<int>(now);

  std::list<Event> events;
  if (old.leftThumbstickX != current.leftThumbstickX) {
    events.push_back(
        {time, "analog", "leftThumbstickX", current.leftThumbstickX});
  }
  if (old.leftThumbstickY != current.leftThumbstickY) {
    events.push_back(
        {time, "analog", "leftThumbstickY", current.leftThumbstickY});
  }
  if (old.rightThumbstickX != current.rightThumbstickX) {
    events.push_back(
        {time, "analog", "rightThumbstickX", current.rightThumbstickX});
  }
  if (old.rightThumbstickY != current.rightThumbstickY) {
    events.push_back(
        {time, "analog", "rightThumbstickY", current.rightThumbstickY});
  }
  if (old.leftTrigger != current.leftTrigger) {
    events.push_back(
        {time, "analog", "leftTrigger", current.leftTrigger});
  }
  if (old.rightTrigger != current.rightTrigger) {
    events.push_back(
        {time, "analog", "rightTrigger", current.rightTrigger});
  }
  if (old.buttons != current.buttons) {
    for (uint32_t i = 0; i < 14; ++i) {
      bool was_pressed = old.buttons & (1 << i);
      bool is_pressed = current.buttons & (1 << i);
      if (was_pressed != is_pressed) {
        double value = is_pressed ? 1.0 : 0.0;
        auto key = get_button_name(1 << i);
        events.push_back(
            {time, "button", key, value});
      }
    }
  }
  return events;
}

bool are_states_different(const GameInputGamepadState& a, const GameInputGamepadState& b) {
  return a.leftThumbstickX != b.leftThumbstickX ||
    a.leftThumbstickY != b.leftThumbstickY ||
    a.leftTrigger != b.leftTrigger ||
    a.rightThumbstickX != b.rightThumbstickX ||
    a.rightThumbstickY != b.rightThumbstickY ||
    a.rightTrigger != b.rightTrigger ||
    a.buttons != b.buttons;
}

void Gamepads::init()
{
  auto hr = GameInputCreate(&g_gameInput);
  std::cout << "GameInputCreate " <<  std::hex << std::setw(8) << std::setfill('0') << hr << std::dec << std::endl;
  std::cout << "GameInput " << (g_gameInput == nullptr ? "null" : "not null")  << std::endl;

  if (g_gameInput != nullptr) {
    // Register listener for gamepad events
    hr = g_gameInput->RegisterDeviceCallback(
      nullptr, // All devices
      GameInputKindGamepad,
      GameInputDeviceConnected,
      GameInputAsyncEnumeration,
      static_cast<void*>(this),
      [](
        _In_ GameInputCallbackToken callbackToken,
        _In_ void * context,
        _In_ IGameInputDevice * device,
        _In_ uint64_t timestamp,
        _In_ GameInputDeviceStatus currentStatus,
        _In_ GameInputDeviceStatus previousStatus
      ) {
        auto* self = static_cast<Gamepads*>(context);
        if (currentStatus & GameInputDeviceConnected) {
          self->on_gamepad_connected(device);
        } else {
          self->on_gamepad_disconnected(device);
        }
      },
      this->deviceCallbackToken
    );
    std::cout << "RegisterDeviceCallback " <<  std::hex << std::setw(8) << std::setfill('0') << hr << std::dec << std::endl;

    hr = g_gameInput->RegisterReadingCallback(
        nullptr, // Any device,
        GameInputKindGamepad,
        static_cast<void*>(this),
        [] (
          GameInputCallbackToken callbackToken,
          void* context,
          IGameInputReading* reading
        ) {
          // auto* self = static_cast<Gamepads*>(context);
          std::cout << "Gamepad event" << std::endl;
        },
        this->readingCallbackToken
    );
    std::cout << "RegisterReadingCallback " <<  std::hex << std::setw(8) << std::setfill('0') << hr << std::dec << std::endl;
  }
}

void Gamepads::stop()
{
  if (g_gamepad) g_gamepad->Release();
  if (g_gameInput) {
    g_gameInput->UnregisterCallback(*this->deviceCallbackToken);
    g_gameInput->UnregisterCallback(*this->readingCallbackToken);
    g_gameInput->Release();
  }

  // Stop/cleanup threads
  for (auto gp : this->gamepads) {
    if (!gp->stop_thead) {
      if (gp->alive) {
        gp->stop_thead = true;
      } else {
        // Cleanup data of threads that exited due to error state.
        delete gp;
      }
    }
  }
  this->gamepads.clear();
}

std::list<GamepadData*> Gamepads::get_gamepads() {
  return this->gamepads;
}

void Gamepads::on_gamepad_connected(IGameInputDevice * device)
{
  const GameInputDeviceInfo* info;
  device->GetDeviceInfo(&info);
  if (info == nullptr) {
    std::cerr << "Gamepad connected but failed to read info" << std::endl;
    return;
  }
  auto gp = new GamepadData();
  gp->id = AppLocalDeviceIdToString(info->deviceId);
  gp->name = info->displayName != nullptr ? info->displayName : "";
  gp->stop_thead = false;
  gp->alive = true;
  this->gamepads.push_back(gp);

  std::cout << "Gamepad connected: " << gp->id << " : " << gp->name << std::endl;

//  std::thread read_thread(
//      [this, gp, device]() { this->read_gamepad(gp, device); });
//  read_thread.detach();
}

void Gamepads::on_gamepad_disconnected(IGameInputDevice * device)
{
  const GameInputDeviceInfo* info;
  device->GetDeviceInfo(&info);
  if (info == nullptr) {
    std::cerr << "Gamepad disconnected but failed to read info" << std::endl;
    return;
  }
  std::string removeId = AppLocalDeviceIdToString(info->deviceId);
  std::cout << "Gamepad disconnected: " << removeId << std::endl;
  GamepadData* removeGp = nullptr;
  for (auto gp : this->gamepads) {
    if (gp->id == removeId) {
      gp->stop_thead = true;
      removeGp = gp;
      break;
    }
  }
  // Remove the gamepad from list. The thread will free up memory.
  if (removeGp != nullptr) {
    this->gamepads.remove(removeGp);
  }
}


void Gamepads::read_gamepad(GamepadData* gamepad, IGameInputDevice* device) {
  /*
  GameInputDeviceInfo* info;
  auto info = device->GetDeviceInfo();
  device->GetDeviceInfo(&info);

  GameInputGamepadState previous_state;
  while (info != nullptr && !gamepad->stop_thead && g_gameInput != nullptr) {
    IGameInputReading* reading;
    GameInputGamepadState state;
    g_gameInput->GetCurrentReading(GameInputKindGamepad, device, &reading);
    if (reading != nullptr) {
      if(reading->GetGamepadState(&state)) {
        if (are_states_different(previous_state, state)) {
          auto events = diff_states(*info, state, previous_state);
          for (auto event : events) {
            if (event_emitter.has_value()) {
              (*event_emitter)(gamepad, event);
            }
          }
        }
        previous_state = state;
        reading->Release();
      }
    }

    Sleep(1);
  }

  if (gamepad->stop_thead) {
    std::cout << "Gamepad thread exit (via signal) " << gamepad->id << std::endl;
    delete gamepad;
  } else {
    std::cout << "Gamepad thread exit (due to error state) " << gamepad->id << std::endl;
    gamepad->alive = false;
  }
    */
}
