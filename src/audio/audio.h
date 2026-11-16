#pragma once
// SID output -> sokol_audio (push model, mono float).
#include <stdbool.h>

bool audio_init(void);      // false if no audio device (the app still runs, silently)
void audio_shutdown(void);
int audio_sample_rate(void);
void audio_set_volume(float volume);    // 0..1
// Matches core_audio_fn; pass as core_desc_t.audio_fn.
void audio_push(const float* samples, int num_samples, void* user_data);
