#include "audio/audio.h"

#include "sokol_audio.h"
#include "sokol_log.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_MAX_CHUNK 1024

static struct {
    bool ok;
    float volume;
} as = { .volume = 1.0f };

bool audio_init(void) {
    saudio_setup(&(saudio_desc){
        .sample_rate = AUDIO_SAMPLE_RATE,
        .num_channels = 1,
        .buffer_frames = 2048,      // ~46 ms; latency becomes a setting in Phase 3
        .logger.func = slog_func,
    });
    as.ok = saudio_isvalid();
    return as.ok;
}

void audio_shutdown(void) {
    saudio_shutdown();
    as.ok = false;
}

int audio_sample_rate(void) {
    return as.ok ? saudio_sample_rate() : AUDIO_SAMPLE_RATE;
}

void audio_set_volume(float volume) {
    as.volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

void audio_push(const float* samples, int num_samples, void* user_data) {
    (void)user_data;
    if (!as.ok) {
        return;
    }
    float buf[AUDIO_MAX_CHUNK];
    while (num_samples > 0) {
        int n = num_samples < AUDIO_MAX_CHUNK ? num_samples : AUDIO_MAX_CHUNK;
        for (int i = 0; i < n; i++) {
            buf[i] = samples[i] * as.volume;
        }
        saudio_push(buf, n);    // drops samples if the device buffer is full
        samples += n;
        num_samples -= n;
    }
}
