// Text-to-speech with the operating system's own voice (worker totdpacks, 2026-11-09): The Typing of the Dead 0's
// Speak the Words (accessibility, off by default; docs/typing.md). No voice files are bundled or made: Windows speaks
// through SAPI (COM, the system's default voice), macOS through `say`, Linux / other Unix through the first of
// `spd-say`, `espeak-ng`, `espeak` found on PATH; none: not available. A worker thread speaks one text after another;
// a backlog drops the oldest (the screen has moved on).
#pragma once

#include <string>

namespace hotd::util::tts {

bool available();             // a voice can speak here (checked once)
std::string backend();        // "Windows speech (SAPI)", "say", "espeak-ng" ... ("" when none)
void speak(const std::string& text);   // queued, never blocks
void shutdown();              // the worker ends (the queue dropped)

} // namespace hotd::util::tts
