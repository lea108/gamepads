
#include <map>
#include <wtypes.h>

#include <windows.h>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <optional>
#include <GameInput.h>

struct GamepadData {
  std::string id;
  std::string name;
  bool stop_thead;
  bool alive;
};

struct Event {
  int time;
  std::string type;
  std::string key;
  double value;
};

class Gamepads {
 private:
  std::list<GamepadData*> gamepads;

  GameInput::v3::GameInputCallbackToken* deviceCallbackToken;
  GameInput::v3::GameInputCallbackToken* readingCallbackToken;
  void read_gamepad(GamepadData* gamepad, GameInput::v3::IGameInputDevice* device);

  void on_gamepad_connected(GameInput::v3::IGameInputDevice* device);
  void on_gamepad_disconnected(GameInput::v3::IGameInputDevice* device);

 public:
  std::optional<std::function<void(GamepadData* gamepad, const Event& event)>>
      event_emitter;
  void init();
  void stop();
  std::list<GamepadData*> get_gamepads();
};

extern Gamepads gamepads;
