#define _GNU_SOURCE

// aloop_txgen.c
//
// Controlled SVXLink replacement for FM TX diagnostics.  It drives the same
// snd-aloop TX cable and PTY that SVXLink normally uses, so the multimode FM
// bridge can be tested without SVXLink:
//
//   aloop_txgen -> plughw:Loopback,0,0 (16 kHz S16_LE mono)
//   aloop_txgen -> /run/svxlink/ptt (PTY master, writes 'T'/'R')
//
// Build (on the target or on an arm64 dev machine):
//   gcc -O2 -Wall -o aloop_txgen aloop_txgen.c -lasound
//
// Run with svxlink stopped and the bridge running:
//   sudo ./aloop_txgen --freq 1000 --dur 2000 --pre 500 --tail 300
//
// From 'T' to 'R' the device is fed continuously: <pre> ms of silence,
// <dur> ms of a sine tone, then <tail> ms of silence — like SVXLink's Tx1.

#include <alsa/asoundlib.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int g_pttFd = -1;
static unsigned int g_rate = 16000;

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

static snd_pcm_t* openPlayback(const char* dev)
{
    snd_pcm_t* pcm = NULL;
    int err = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "open %s: %s\n", dev, snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_t* p = NULL;
    snd_pcm_hw_params_alloca(&p);
    snd_pcm_hw_params_any(pcm, p);
    snd_pcm_hw_params_set_access(pcm, p, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, p, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, p, 1);
    unsigned int rate = g_rate;
    snd_pcm_hw_params_set_rate_near(pcm, p, &rate, 0);
    snd_pcm_uframes_t period = 512;
    snd_pcm_hw_params_set_period_size_near(pcm, p, &period, 0);
    snd_pcm_uframes_t buffer = period * 8;
    snd_pcm_hw_params_set_buffer_size_near(pcm, p, &buffer);
    err = snd_pcm_hw_params(pcm, p);
    if (err < 0) {
        fprintf(stderr, "hw_params %s: %s\n", dev, snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }
    snd_pcm_prepare(pcm);
    return pcm;
}

static int writeAll(snd_pcm_t* pcm, const short* buf, snd_pcm_uframes_t frames)
{
    snd_pcm_uframes_t off = 0;
    while (off < frames) {
        const snd_pcm_sframes_t w =
            snd_pcm_writei(pcm, buf + off, frames - off);
        if (w == -EPIPE) {
            snd_pcm_prepare(pcm);
            continue;
        }
        if (w < 0) {
            fprintf(stderr, "writei: %s\n", snd_strerror((int)w));
            return -1;
        }
        off += (snd_pcm_uframes_t)w;
    }
    return 0;
}

static int createPtt(const char* path)
{
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        perror("posix_openpt");
        return -1;
    }
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        perror("grantpt/unlockpt");
        close(master);
        return -1;
    }

    const char* slave = ptsname(master);
    if (!slave) {
        perror("ptsname");
        close(master);
        return -1;
    }

    // The bridge reads the slave; we write to the master, like SVXLink.
    struct termios t;
    if (tcgetattr(master, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(master, TCSANOW, &t);
    }

    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0775);
    }
    unlink(path);
    if (symlink(slave, path) != 0) {
        perror("symlink ptt");
        close(master);
        return -1;
    }
    g_pttFd = master;
    return 0;
}

static void ptt(char c)
{
    if (g_pttFd >= 0)
        (void)write(g_pttFd, &c, 1);
}

int main(int argc, char** argv)
{
    const char* dev  = "plughw:Loopback,0,0";
    const char* path = "/run/svxlink/ptt";
    double freq      = 1000.0;
    int durMs        = 2000;
    int preMs        = 500;
    int tailMs       = 300;
    int amp          = 12000;
    int block        = 512;
    int loops        = 1;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (v && strcmp(a, "--dev") == 0)       { dev = v; ++i; }
        else if (v && strcmp(a, "--ptt") == 0)  { path = v; ++i; }
        else if (v && strcmp(a, "--freq") == 0) { freq = atof(v); ++i; }
        else if (v && strcmp(a, "--dur") == 0)  { durMs = atoi(v); ++i; }
        else if (v && strcmp(a, "--pre") == 0)  { preMs = atoi(v); ++i; }
        else if (v && strcmp(a, "--tail") == 0) { tailMs = atoi(v); ++i; }
        else if (v && strcmp(a, "--amp") == 0)  { amp = atoi(v); ++i; }
        else if (v && strcmp(a, "--block") == 0) { block = atoi(v); ++i; }
        else if (v && strcmp(a, "--loops") == 0){ loops = atoi(v); ++i; }
        else {
            fprintf(stderr, "unknown arg: %s\n", a);
            return 2;
        }
    }

    if (createPtt(path) != 0)
        return 3;

    snd_pcm_t* pcm = openPlayback(dev);
    if (!pcm)
        return 4;

    // Give the bridge PTT reader time to open the freshly created slave.
    sleep_ms(400);

    const unsigned int durFrames  = (unsigned int)((double)durMs  * g_rate / 1000.0);
    const unsigned int preFrames  = (unsigned int)((double)preMs  * g_rate / 1000.0);
    const unsigned int tailFrames = (unsigned int)((double)tailMs * g_rate / 1000.0);

    short* chunk = (short*)malloc((size_t)block * sizeof(short));
    if (!chunk)
        return 5;

    for (int loop = 0; loop < loops; ++loop) {
        ptt('T');

        // SVXLink keeps writing while keyed: lead with silence, not a gap.
        memset(chunk, 0, (size_t)block * sizeof(short));
        for (unsigned int n = 0; n < preFrames; n += (unsigned int)block) {
            const unsigned int k = preFrames - n < (unsigned int)block
                ? preFrames - n : (unsigned int)block;
            if (writeAll(pcm, chunk, k) != 0)
                goto done;
        }

        double phase = 0.0;
        const double step = 2.0 * M_PI * freq / (double)g_rate;
        for (unsigned int n = 0; n < durFrames; n += (unsigned int)block) {
            const unsigned int k = durFrames - n < (unsigned int)block
                ? durFrames - n : (unsigned int)block;
            for (unsigned int i = 0; i < k; ++i) {
                chunk[i] = (short)(sin(phase) * (double)amp);
                phase += step;
            }
            if (writeAll(pcm, chunk, k) != 0)
                goto done;
        }

        memset(chunk, 0, (size_t)block * sizeof(short));
        for (unsigned int n = 0; n < tailFrames; n += (unsigned int)block) {
            const unsigned int k = tailFrames - n < (unsigned int)block
                ? tailFrames - n : (unsigned int)block;
            if (writeAll(pcm, chunk, k) != 0)
                goto done;
        }

        ptt('R');
        if (loop + 1 < loops)
            sleep_ms(500);
    }

done:
    free(chunk);
    snd_pcm_close(pcm);
    if (g_pttFd >= 0) {
        ptt('R');
        close(g_pttFd);
    }
    unlink(path);
    return 0;
}
