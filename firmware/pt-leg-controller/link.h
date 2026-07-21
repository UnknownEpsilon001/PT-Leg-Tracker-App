#pragma once
#include <Arduino.h>

struct LinkState {
  bool running;
  int phase;
  uint32_t elapsed;
  uint16_t reps;
  bool wifi;
  bool server;
};

struct LinkSettings {
  String ssid, pass, server, code;
  uint16_t maxTravel, hold, rest;
};

class Link {
 public:
  void begin(Stream* io) { _io = io; }
  void poll();
  void sendCmd(const char* v);
  void sendSettings(const LinkSettings& s);
  void sendHello();
  void sendState(const LinkState& st);
  void sendPing();
  uint32_t lastRxMs() const { return _lastRx; }

  void (*onCmd)(const char* v) = nullptr;
  void (*onSettings)(const LinkSettings&) = nullptr;   // controller side
  void (*onHello)() = nullptr;                         // controller side
  void (*onState)(const LinkState&) = nullptr;         // display side
  void (*onSettingsReply)(const LinkSettings&) = nullptr; // display side

 private:
  void dispatch(const String& line);
  Stream* _io = nullptr;
  String _buf;
  uint32_t _lastRx = 0;
};
