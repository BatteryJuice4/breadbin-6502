#include "audio/audio.h"

#include "sokol_audio.h"
#include "sokol_log.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_MAX_CHUNK 1024

static struct {
    bool ok;
    float gain;         // volume, or 0 when muted
    int latency_ms;
} as = { .gain = 0.8f, .latency_ms = 50 };

static int frames_for_latency(int ms) {
    int frames = AUDIO_SAMPLE_RATE * ms / 1000;
    // power of two keeps every backend happy
    int p = 256;
    while (p < frames && p < 16384) p <<= 1;
    return p;
}

bool audio_init(int latency_ms) {
    as.latency_ms = latency_ms;
    saudio_setup(&(saudio_desc){
        .sample_rate = AUDIO_SAMPLE_RATE,
        .num_channels = 1,
        .buffer_frames = frames_for_latency(latency_ms),
        .logger.func = slog_func,
    });
    as.ok = saudio_isvalid();
    return as.ok;
}

void audio_shutdown(void) {
    saudio_shutdown();
    as.ok = false;
}

bool audio_ok(void) {
    return as.ok;
}

int audio_sample_rate(void) {
    return AUDIO_SAMPLE_RATE;   // requested rate; the core is created with this
}

void audio_set_settings(const audio_settings_t* s) {
    float v = s->volume < 0.0f ? 0.0f : (s->volume > 1.0f ? 1.0f : s->volume);
    as.gain = s->mute ? 0.0f : v;
    if (s->latency_ms != as.latency_ms) {
        audio_shutdown();
        audio_init(s->latency_ms);
    }
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
            buf[i] = samples[i] * as.gain;
        }
        saudio_push(buf, n);    // drops samples if the device buffer is full
        samples += n;
        num_samples -= n;
    }
}
