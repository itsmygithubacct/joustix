/* Banked PCM sound with a procedural fallback and command-line sink mixer. */
#include "joustix.h"
#include "pcm_wav.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SAMPLE_RATE 44100
#define MIX_FRAMES 384
#define MAX_VOICES 16
#define MAX_SFX_VARIANTS 8

typedef struct { int16_t *data; int length; } Sample;
typedef struct { const int16_t *data; int length; float position, step, volume; bool active; } Voice;

static Sample samples[SFX_COUNT][MAX_SFX_VARIANTS];
static uint8_t sample_counts[SFX_COUNT];
static uint8_t last_variants[SFX_COUNT];
static Voice voices[MAX_VOICES];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mixer_thread;
static atomic_bool running = ATOMIC_VAR_INIT(false);
static bool enabled = true;
static int sink_fd = -1;
static pid_t sink_pid = -1;
static uint32_t audio_rng = 0x51a7c0deu;
static bool thread_started;

static uint32_t audio_random_u32(void)
{
    audio_rng ^= audio_rng << 13;
    audio_rng ^= audio_rng >> 17;
    audio_rng ^= audio_rng << 5;
    return audio_rng;
}

static float noise_sample(void)
{
    return ((audio_random_u32() >> 8) * (1.0f / 8388608.0f)) - 1.0f;
}

static void clear_sample_bank(int id)
{
    if (id < 0 || id >= SFX_COUNT) return;
    for (int variant = 0; variant < MAX_SFX_VARIANTS; variant++) {
        free(samples[id][variant].data);
        samples[id][variant] = (Sample){0};
    }
    sample_counts[id] = 0;
}

static void bake(int id, float *src, int n, float peak)
{
    float max_value = 1e-6f;
    for (int i = 0; i < n; i++) max_value = fmaxf(max_value, fabsf(src[i]));
    int16_t *out = malloc((size_t)n * sizeof *out);
    if (!out) return;
    float gain = peak / max_value;
    for (int i = 0; i < n; i++) {
        float fade = 1.0f;
        if (i < 48) fade *= i / 48.0f;
        if (n - i < 220) fade *= (n - i) / 220.0f;
        out[i] = (int16_t)(clampf(src[i] * gain * fade, -1, 1) * 32767.0f);
    }
    clear_sample_bank(id);
    samples[id][0] = (Sample){ out, n };
    sample_counts[id] = 1;
}

static void add_note(float *s, int n, float start, float duration, float frequency,
                     float amount)
{
    int first = (int)(start * SAMPLE_RATE);
    int last = (int)((start + duration) * SAMPLE_RATE);
    if (last > n) last = n;
    float phase = 0;
    for (int i = first; i < last; i++) {
        float t = (float)(i - first) / SAMPLE_RATE;
        float env = fminf(1, t / .018f) * expf(-t / (duration * .72f));
        phase += 6.2831853f * frequency / SAMPLE_RATE;
        s[i] += (sinf(phase) + .32f * sinf(phase * 2) + .12f * sinf(phase * 3)) *
                env * amount;
    }
}

static float triangle_wave(float phase)
{
    float p = phase * (1.0f / 6.2831853f);
    p -= floorf(p);
    return 1.0f - 4.0f * fabsf(p - .5f);
}

static void make_menu(void)
{
    int n = (int)(.14f * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (s) {
        add_note(s, n, .000f, .080f, 740.0f, .62f);
        add_note(s, n, .045f, .090f, 1110.0f, .76f);
        bake(SFX_MENU, s, n, .20f);
        free(s);
    }
}

static void make_flap(void)
{
    const float duration = .18f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float phase = 0, low_noise = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / duration;
        float noise = noise_sample();
        low_noise += .11f * (noise - low_noise);
        float high_noise = noise - low_noise;
        float frequency = 155.0f - 92.0f * u;
        phase += 6.2831853f * frequency / SAMPLE_RATE;
        float env = fminf(1.0f, t / .010f) * expf(-t / .092f);
        s[i] = (low_noise * .92f + high_noise * .16f +
                sinf(phase) * .30f) * env;
    }
    bake(SFX_FLAP, s, n, .30f);
    free(s);
}

static void make_step(void)
{
    const float duration = .095f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float phase = 0, grit = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / duration;
        float noise = noise_sample();
        grit += .19f * (noise - grit);
        phase += 6.2831853f * (92.0f - 28.0f * u) / SAMPLE_RATE;
        s[i] = sinf(phase) * .82f * expf(-t / .026f) +
               grit * .46f * expf(-t / .048f);
    }
    bake(SFX_STEP, s, n, .24f);
    free(s);
}

static void make_land(void)
{
    const float duration = .17f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float phase = 0, grit = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / duration;
        float noise = noise_sample();
        grit += .10f * (noise - grit);
        phase += 6.2831853f * (112.0f - 48.0f * u) / SAMPLE_RATE;
        float thump = (sinf(phase) + .22f * triangle_wave(phase * .5f)) *
                      expf(-t / .052f);
        s[i] = thump * .86f + grit * .48f * expf(-t / .038f);
    }
    bake(SFX_LAND, s, n, .32f);
    free(s);
}

static void make_joust(void)
{
    static const float frequencies[] = { 1493, 2210, 2967, 3743, 4638, 5512 };
    static const float decays[] = { .055f, .085f, .070f, .060f, .048f, .038f };
    static const float amounts[] = { .14f, .30f, .34f, .26f, .20f, .15f };
    const float duration = .36f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float value = noise_sample() * (.90f * expf(-t / .030f) +
                                        .52f * expf(-t / .0045f));
        for (int k = 0; k < 6; k++)
            value += sinf(6.2831853f * frequencies[k] * t + k * .73f) *
                     amounts[k] * expf(-t / decays[k]);
        value += sinf(6.2831853f * 168.0f * t) * .34f * expf(-t / .046f);
        s[i] = value;
    }
    bake(SFX_JOUST, s, n, .62f);
    free(s);
}

static void make_hurt(void)
{
    const float duration = .56f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float phase = 0, rubble = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float noise = noise_sample();
        rubble += .055f * (noise - rubble);
        float frequency = 320.0f * expf(-t * 5.2f) + 52.0f;
        phase += 6.2831853f * frequency / SAMPLE_RATE;
        float fall = (sinf(phase) + .24f * triangle_wave(phase * .51f)) *
                     expf(-t / .28f);
        float crash = (noise * .62f + rubble * .55f) * expf(-t / .070f);
        float thud = t > .07f ? sinf(6.2831853f * 69.0f * (t - .07f)) *
                                expf(-(t - .07f) / .13f) : 0;
        s[i] = fall * .68f + crash + thud * .62f;
    }
    bake(SFX_HURT, s, n, .56f);
    free(s);
}

static void make_egg(void)
{
    int n = (int)(.43f * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    add_note(s, n, .000f, .145f, 1046.50f, .62f);
    add_note(s, n, .085f, .165f, 1318.51f, .70f);
    add_note(s, n, .175f, .225f, 1760.00f, .82f);
    bake(SFX_EGG, s, n, .38f);
    free(s);
}

static void make_hatch(void)
{
    const float duration = .40f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float phase = 0, scratch = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float noise = noise_sample();
        scratch += .16f * (noise - scratch);
        float frequency = 980.0f * expf(-t * 6.7f) + 118.0f;
        phase += 6.2831853f * frequency / SAMPLE_RATE;
        float crack = noise * (1.10f * expf(-t / .008f) +
                               .34f * expf(-t / .090f));
        float creature = (triangle_wave(phase) * .52f + sinf(phase * .49f) * .22f) *
                         fminf(1.0f, t / .018f) * expf(-t / .18f);
        s[i] = crack + scratch * .28f * expf(-t / .15f) + creature;
    }
    bake(SFX_HATCH, s, n, .52f);
    free(s);
}

static void make_wave(void)
{
    int n = (int)(1.18f * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    add_note(s, n, .00f, .25f, 392.00f, .68f);
    add_note(s, n, .00f, .25f, 196.00f, .22f);
    add_note(s, n, .19f, .27f, 493.88f, .72f);
    add_note(s, n, .37f, .34f, 587.33f, .78f);
    add_note(s, n, .58f, .54f, 783.99f, .90f);
    add_note(s, n, .58f, .50f, 392.00f, .34f);
    bake(SFX_WAVE, s, n, .52f);
    free(s);
}

static void make_lava(void)
{
    const float duration = .62f;
    int n = (int)(duration * SAMPLE_RATE);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float phase = 0, rumble = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        rumble += .024f * (noise_sample() - rumble);
        phase += 6.2831853f * (51.0f + 4.0f * sinf(t * 17.0f)) / SAMPLE_RATE;
        float value = (sinf(phase) * .54f + rumble * 1.15f) * expf(-t / .31f);
        float b1 = clampf((t - .12f) / .16f, 0, 1);
        float b2 = clampf((t - .34f) / .18f, 0, 1);
        if (t >= .12f && t <= .28f)
            value += sinf(6.2831853f * (95.0f * b1 + 170.0f * b1 * b1)) *
                     sinf(3.1415927f * b1) * .42f;
        if (t >= .34f && t <= .52f)
            value += sinf(6.2831853f * (72.0f * b2 + 145.0f * b2 * b2)) *
                     sinf(3.1415927f * b2) * .32f;
        s[i] = value;
    }
    bake(SFX_LAVA, s, n, .44f);
    free(s);
}

static void synthesize(void)
{
    make_menu();
    make_flap();
    make_step();
    make_land();
    make_joust();
    make_hurt();
    make_egg();
    make_hatch();
    make_wave();
    make_lava();
}

static const char *const sfx_files[SFX_COUNT] = {
    [SFX_MENU] = "sfx/menu.wav",
    [SFX_FLAP] = "sfx/flap.wav",
    [SFX_STEP] = "sfx/step.wav",
    [SFX_LAND] = "sfx/land.wav",
    [SFX_JOUST] = "sfx/joust.wav",
    [SFX_HURT] = "sfx/hurt.wav",
    [SFX_EGG] = "sfx/egg.wav",
    [SFX_HATCH] = "sfx/hatch.wav",
    [SFX_WAVE] = "sfx/wave.wav",
    [SFX_LAVA] = "sfx/lava.wav",
};

static bool variant_filename(const char *base, int variant, char *out, size_t size)
{
    if (variant == 0)
        return snprintf(out, size, "%s", base) < (int)size;
    const char *extension = strrchr(base, '.');
    if (!extension) return false;
    int stem = (int)(extension - base);
    return snprintf(out, size, "%.*s_v%02d%s", stem, base,
                    variant + 1, extension) < (int)size;
}

static void load_external_sounds(void)
{
    for (int id = 0; id < SFX_COUNT; id++) {
        Sample loaded[MAX_SFX_VARIANTS] = {{0}};
        int count = 0;
        for (int variant = 0; variant < MAX_SFX_VARIANTS; variant++) {
            char relative[96];
            if (!variant_filename(sfx_files[id], variant, relative, sizeof relative))
                break;
            int16_t *data = NULL;
            int frames = 0;
            if (!pcm_wav_load_mono_44100(asset_path(relative), &data, &frames))
                break;
            loaded[count++] = (Sample){data, frames};
        }
        if (count == 0) continue;
        clear_sample_bank(id);
        for (int variant = 0; variant < count; variant++)
            samples[id][variant] = loaded[variant];
        sample_counts[id] = (uint8_t)count;
    }
}

static int choose_variant(int id)
{
    int count = sample_counts[id];
    if (count <= 1) return 0;
    int variant;
    do variant = (int)(audio_random_u32() % (uint32_t)count);
    while (variant == last_variants[id]);
    last_variants[id] = (uint8_t)variant;
    return variant;
}

static bool executable_on_path(const char *name)
{
    const char *path = getenv("PATH");
    if (!path) return false;
    char *copy = strdup(path);
    if (!copy) return false;
    bool found = false;
    for (char *save = NULL, *dir = strtok_r(copy, ":", &save); dir;
         dir = strtok_r(NULL, ":", &save)) {
        char full[1024];
        if (snprintf(full, sizeof full, "%s/%s", *dir ? dir : ".", name) < (int)sizeof full &&
            access(full, X_OK) == 0) { found = true; break; }
    }
    free(copy);
    return found;
}

static bool start_sink(void)
{
    enum { SINK_NONE, SINK_PACAT, SINK_PW, SINK_APLAY, SINK_SOX } kind = SINK_NONE;
    const char *program = NULL;
    if (executable_on_path("pacat")) { kind = SINK_PACAT; program = "pacat"; }
    else if (executable_on_path("pw-play")) { kind = SINK_PW; program = "pw-play"; }
    else if (executable_on_path("aplay")) { kind = SINK_APLAY; program = "aplay"; }
    else if (executable_on_path("play")) { kind = SINK_SOX; program = "play"; }
    if (!program) return false;

    int pipefd[2];
    if (pipe(pipefd) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return false; }
    if (pid == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) { dup2(nullfd, STDOUT_FILENO); dup2(nullfd, STDERR_FILENO); close(nullfd); }
        if (kind == SINK_PACAT)
            execlp(program, program, "--raw", "--latency-msec=20", "--rate=44100",
                   "--channels=1", "--format=s16le", (char *)NULL);
        else if (kind == SINK_PW)
            execlp(program, program, "--raw", "--rate=44100", "--channels=1", "--format=s16", "-", (char *)NULL);
        else if (kind == SINK_APLAY)
            execlp(program, program, "-q", "-f", "S16_LE", "-r", "44100", "-c", "1",
                   "-B", "30000", "-F", "10000", (char *)NULL);
        else
            execlp(program, program, "-q", "-t", "raw", "-e", "signed", "-b", "16", "-c", "1", "-r", "44100", "-", (char *)NULL);
        _exit(127);
    }
    close(pipefd[0]);
    sink_fd = pipefd[1]; sink_pid = pid;
    return true;
}

static bool write_samples(const int16_t *p, size_t bytes)
{
    const char *data = (const char *)p;
    while (bytes) {
        ssize_t n = write(sink_fd, data, bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n; bytes -= (size_t)n;
    }
    return true;
}

static void *mix_loop(void *unused)
{
    (void)unused;
    int16_t out[MIX_FRAMES];
    while (atomic_load(&running)) {
        pthread_mutex_lock(&lock);
        for (int i = 0; i < MIX_FRAMES; i++) {
            float sum = 0;
            for (int j = 0; j < MAX_VOICES; j++) {
                Voice *v = &voices[j];
                if (!v->active) continue;
                int at = (int)v->position;
                if (at >= v->length) { v->active = false; continue; }
                sum += v->data[at] * v->volume;
                v->position += v->step;
            }
            /* Soft limiting keeps overlapping impacts full without the brittle
             * square-edged distortion of hard clipping. */
            out[i] = (int16_t)(tanhf(sum * (1.0f / 32768.0f)) * 32767.0f);
        }
        pthread_mutex_unlock(&lock);
        if (!write_samples(out, sizeof out)) { atomic_store(&running, false); break; }
    }
    return NULL;
}

bool sound_init(void)
{
    synthesize();
    memset(last_variants, 0xff, sizeof last_variants);
    load_external_sounds();
    signal(SIGPIPE, SIG_IGN);
    if (!start_sink()) return false;
    atomic_store(&running, true);
    if (pthread_create(&mixer_thread, NULL, mix_loop, NULL) != 0) {
        atomic_store(&running, false);
        close(sink_fd); sink_fd = -1;
        kill(sink_pid, SIGTERM); waitpid(sink_pid, NULL, 0); sink_pid = -1;
        return false;
    }
    thread_started = true;
    return true;
}

void sound_shutdown(void)
{
    atomic_store(&running, false);
    if (sink_fd >= 0) close(sink_fd);
    sink_fd = -1;
    if (thread_started) pthread_join(mixer_thread, NULL);
    thread_started = false;
    if (sink_pid > 0) {
        int status;
        if (waitpid(sink_pid, &status, WNOHANG) == 0) {
            kill(sink_pid, SIGTERM);
            waitpid(sink_pid, &status, 0);
        }
    }
    sink_pid = -1;
    pthread_mutex_lock(&lock);
    memset(voices, 0, sizeof voices);
    pthread_mutex_unlock(&lock);
    for (int i = 0; i < SFX_COUNT; i++) clear_sample_bank(i);
}

void sound_set_enabled(bool on) { enabled = on; }
bool sound_is_enabled(void) { return enabled; }

void sound_play(int id, float volume, float pitch)
{
    if (!enabled || id < 0 || id >= SFX_COUNT || sample_counts[id] == 0 ||
        !atomic_load(&running)) return;
    Sample *sample = &samples[id][choose_variant(id)];
    pthread_mutex_lock(&lock);
    int slot = -1;
    float oldest = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { slot = i; break; }
        if (voices[i].position > oldest) { oldest = voices[i].position; slot = i; }
    }
    voices[slot] = (Voice){ sample->data, sample->length, 0,
                            clampf(pitch, .45f, 2.2f), clampf(volume, 0, 1), true };
    pthread_mutex_unlock(&lock);
}
