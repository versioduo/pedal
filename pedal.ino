// © Kay Sievers <kay@vrfy.org>, 2020-2021
// SPDX-License-Identifier: Apache-2.0

#include <V2Device.h>
#include <V2LED.h>
#include <V2MIDI.h>
#include <V2Potentiometer.h>

V2DEVICE_METADATA("com.versioduo.pedal", 18, "versioduo:samd:itsybitsy");

enum {
  PIN_POTENTIOMETER = A0,
};

static class Device : public V2Device {
public:
  Device() : V2Device() {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "pedal";
    metadata.description = "Expression Pedal";
    metadata.home        = "https://versioduo.com/#pedal";

    system.download       = "https://versioduo.com/download";
    system.configure      = "https://versioduo.com/configure";
    system.ports.announce = 0;

    // https://github.com/versioduo/arduino-board-package/blob/main/boards.txt
    usb.pid = 0xe930;

    configuration = {.magic{0x9e1e0000 | usb.pid}, .size{sizeof(config)}, .data{&config}};
  }

  // Config, written to EEPROM
  struct {
    uint8_t channel;
    uint8_t controller;
    struct {
      uint8_t from;
      uint8_t to;
    } range;
  } config{.channel{}, .controller{V2MIDI::CC::ModulationWheel}, .range{.from{}, .to{127}}};

private:
  const struct V2Potentiometer::Config _config { .n_steps{128}, .min{0.6}, .max{0.95}, .alpha{0.3}, .lag{0.007}, };

  V2Potentiometer _poti{&_config};
  uint8_t _step{};
  unsigned long _measure_usec{};
  unsigned long _events_usec{};
  V2MIDI::Packet _midi{};

  void handleReset() override {
    digitalWrite(PIN_LED_ONBOARD, LOW);

    _poti.reset();
    _step         = 0;
    _measure_usec = 0;
    _events_usec  = micros();
    _midi         = {};
  }

  void allNotesOff() {
    sendEvents(true);
  }

  void handleLoop() override {
    if ((unsigned long)(micros() - _measure_usec) > 10 * 1000) {
      _poti.measure(analogRead(PIN_POTENTIOMETER) / 1023.f);
      _measure_usec = micros();
    }

    if ((unsigned long)(micros() - _events_usec) > 20 * 1000) {
      sendEvents();
      _events_usec = micros();
    }
  }

  bool handleSend(V2MIDI::Packet *midi) override {
    return usb.midi.send(midi);
  }

  void sendEvents(bool force = false) {
    const float range   = (int8_t)config.range.to - (int8_t)config.range.from;
    const uint8_t value = config.range.from + (range * _poti.getFraction());

    if (!force && _step == value)
      return;

    _step = value;
    send(_midi.setControlChange(config.channel, config.controller, value));
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    switch (controller) {
      case V2MIDI::CC::AllSoundOff:
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;
    }
  }

  void handleSystemReset() override {
    reset();
  }

  void exportSettings(JsonArray json) override {
    {
      JsonObject json_midi = json.createNestedObject();
      json_midi["type"]    = "midi";
      json_midi["channel"] = "midi.channel";

      // The object in the configuration record.
      JsonObject json_configuration = json_midi.createNestedObject("configuration");
      json_configuration["path"]    = "midi";
      json_configuration["field"]   = "channel";
    }

    {
      JsonObject json_controller = json.createNestedObject();
      json_controller["type"]    = "controller";
      json_controller["title"]   = "Controller";

      // The object in the configuration record.
      JsonObject json_configuration = json_controller.createNestedObject("configuration");
      json_configuration["path"]    = "controller";
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonObject json_midi = json["midi"];
    if (json_midi) {
      if (!json_midi["channel"].isNull()) {
        uint8_t channel = json_midi["channel"];

        if (channel < 1)
          config.channel = 0;
        else if (channel > 16)
          config.channel = 15;
        else
          config.channel = channel - 1;
      }
    }

    if (!json["controller"].isNull()) {
      uint8_t controller = json["controller"];
      if (controller > 127)
        config.controller = 127;

      else
        config.controller = controller;
    }

    JsonObject json_range = json["range"];
    if (json_range) {
      if (!json_range["from"].isNull()) {
        uint8_t value = json_range["from"];
        if (value > 127)
          config.range.from = 127;

        else
          config.range.from = value;
      }

      if (!json_range["to"].isNull()) {
        uint8_t value = json_range["to"];
        if (value > 127)
          config.range.to = 127;

        else
          config.range.to = value;
      }
    }
  }

  void exportConfiguration(JsonObject json) override {
    {
      json["#midi"]         = "The MIDI settings";
      JsonObject json_midi  = json.createNestedObject("midi");
      json_midi["#channel"] = "The channel to send notes and control values to";
      json_midi["channel"]  = config.channel + 1;
    }

    json["#controller"] = "The MIDI The MIDI controller number";
    json["controller"]  = config.controller;

    json["#range"]        = "The range of controller values to send";
    JsonObject json_range = json.createNestedObject("range");
    json_range["from"]    = config.range.from;
    json_range["to"]      = config.range.to;
  }

  void exportOutput(JsonObject json) override {
    json["channel"] = config.channel;

    JsonArray json_potis = json.createNestedArray("controllers");
    JsonObject json_poti = json_potis.createNestedObject();
    json_poti["name"]    = "Controller";
    json_poti["number"]  = config.controller;
  }
} Device;

// Dispatch MIDI packets
static class MIDI {
public:
  void loop() {
    if (!Device.usb.midi.receive(&_midi))
      return;

    if (_midi.getPort() != 0)
      return;

    Device.dispatch(&Device.usb.midi, &_midi);
  }

private:
  V2MIDI::Packet _midi{};
} MIDI;

void setup() {
  Serial.begin(9600);

  Device.begin();
  Device.reset();
}

void loop() {
  MIDI.loop();
  Device.loop();

  if (Device.idle())
    Device.sleep();
}
