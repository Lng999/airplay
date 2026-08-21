// audio_mute.h - silence one process through the Windows audio session API.
//
// A sleeping iPhone stops sending video but keeps sending audio, so hiding the video window
// is only half of what the user asked for. Muting the child's audio session leaves the
// GStreamer pipeline completely untouched - no pause, no restart, no risk of the sink
// deciding its window went away - and is undone by a single call.
#pragma once

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

// Mutes or unmutes every audio session owned by pid on the default render device.
// Best-effort: returns false when the process has no session yet (it only appears once it
// has actually played something) or when the audio service refuses. Callers treat a false
// as "nothing to do", never as a fatal error.
bool setProcessMuted(unsigned long pid, bool muted);

} // namespace ui
