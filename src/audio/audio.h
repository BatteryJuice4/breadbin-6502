#pragma once
// SID output -> sokol_audio (push model, mono float).
#include <stdbool.h>

#include "settings/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

bool audio_init(int latency_ms);    // false if no audio device (the app still runs, silently)
void audio_shutdown(void);
bool audio_ok(void);
int audio_sample_rate(void);
// Volume/mute apply immediately; a latency change reopens the device.
void audio_set_settings(const audio_settings_t* as);
// Matches core_audio_fn; pass as core_desc_t.audio_fn.
void audio_push(const float* samples, int num_samples, void* user_data);

#ifdef __cplusplus
}
#endif
