#include <stdio.h>
#include <string.h>

#include <sys/audioio.h>

#include <userland/modules/include/syscalls.h>

#include <userland/applications/games/DOOM/linuxdoom-1.10/z_zone.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/i_system.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/i_sound.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/m_misc.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/w_wad.h>

#define SAMPLECOUNT 512
#define NUM_CHANNELS 8
#define BUFMUL 4
#define MIXBUFFERSIZE (SAMPLECOUNT * BUFMUL)
#define SAMPLERATE 11025

static int g_lengths[NUMSFX];
static signed short g_mixbuffer[MIXBUFFERSIZE];
static unsigned int g_channelstep[NUM_CHANNELS];
static unsigned int g_channelstepremainder[NUM_CHANNELS];
static unsigned char *g_channels[NUM_CHANNELS];
static unsigned char *g_channelsend[NUM_CHANNELS];
static int g_channelstart[NUM_CHANNELS];
static int g_channelhandles[NUM_CHANNELS];
static int g_channelids[NUM_CHANNELS];
static int g_steptable[256];
static int g_vol_lookup[128 * 256];
static int *g_channelleftvol_lookup[NUM_CHANNELS];
static int *g_channelrightvol_lookup[NUM_CHANNELS];
static int g_sound_ready = 0;

static void *doom_getsfx(char *sfxname, int *len) {
    unsigned char *sfx;
    unsigned char *paddedsfx;
    int i;
    int size;
    int paddedsize;
    char name[20];
    int sfxlump;

    sprintf(name, "ds%s", sfxname);
    if (W_CheckNumForName(name) == -1) {
        sfxlump = W_GetNumForName("dspistol");
    } else {
        sfxlump = W_GetNumForName(name);
    }

    size = W_LumpLength(sfxlump);
    sfx = (unsigned char *)W_CacheLumpNum(sfxlump, PU_STATIC);
    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;
    paddedsfx = (unsigned char *)Z_Malloc((size_t)paddedsize + 8u, PU_STATIC, 0);
    memcpy(paddedsfx, sfx, (size_t)size);
    for (i = size; i < paddedsize + 8; ++i) {
        paddedsfx[i] = 128;
    }
    Z_Free(sfx);
    *len = paddedsize;
    return (void *)(paddedsfx + 8);
}

static int doom_addsfx(int sfxid, int volume, int step, int separation) {
    static unsigned short handlenums = 0;
    int i;
    int rc = -1;
    int oldest = gametic;
    int oldestnum = 0;
    int slot;
    int rightvol;
    int leftvol;

    if (sfxid == sfx_sawup ||
        sfxid == sfx_sawidl ||
        sfxid == sfx_sawful ||
        sfxid == sfx_sawhit ||
        sfxid == sfx_stnmov ||
        sfxid == sfx_pistol) {
        for (i = 0; i < NUM_CHANNELS; ++i) {
            if (g_channels[i] && g_channelids[i] == sfxid) {
                g_channels[i] = 0;
                break;
            }
        }
    }

    for (i = 0; (i < NUM_CHANNELS) && g_channels[i]; ++i) {
        if (g_channelstart[i] < oldest) {
            oldestnum = i;
            oldest = g_channelstart[i];
        }
    }

    slot = (i == NUM_CHANNELS) ? oldestnum : i;
    g_channels[slot] = (unsigned char *)S_sfx[sfxid].data;
    g_channelsend[slot] = g_channels[slot] + g_lengths[sfxid];

    if (!handlenums) {
        handlenums = 100;
    }

    g_channelhandles[slot] = rc = handlenums++;
    g_channelstep[slot] = (unsigned int)step;
    g_channelstepremainder[slot] = 0u;
    g_channelstart[slot] = gametic;

    separation += 1;
    leftvol = volume - ((volume * separation * separation) >> 16);
    separation = separation - 257;
    rightvol = volume - ((volume * separation * separation) >> 16);

    if (rightvol < 0 || rightvol > 127) {
        I_Error("rightvol out of bounds");
    }
    if (leftvol < 0 || leftvol > 127) {
        I_Error("leftvol out of bounds");
    }

    g_channelleftvol_lookup[slot] = &g_vol_lookup[leftvol * 256];
    g_channelrightvol_lookup[slot] = &g_vol_lookup[rightvol * 256];
    g_channelids[slot] = sfxid;
    return rc;
}

void I_SetChannels(void) {
    int i;
    int j;
    int *steptablemid = g_steptable + 128;

    for (i = -128; i < 128; ++i) {
        int step = 65536 + (i * 384);

        if (step < 16384) {
            step = 16384;
        }
        steptablemid[i] = step;
    }
    for (i = 0; i < 128; ++i) {
        for (j = 0; j < 256; ++j) {
            g_vol_lookup[(i * 256) + j] = (i * (j - 128) * 256) / 127;
        }
    }
}

void I_SetSfxVolume(int volume) {
    snd_SfxVolume = volume;
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo) {
    char namebuf[9];

    sprintf(namebuf, "ds%s", sfxinfo->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
    (void)priority;
    if (!g_sound_ready) {
        return -1;
    }
    return doom_addsfx(id, vol, g_steptable[pitch], sep);
}

void I_StopSound(int handle) {
    int i;

    for (i = 0; i < NUM_CHANNELS; ++i) {
        if (g_channelhandles[i] == handle) {
            g_channels[i] = 0;
            return;
        }
    }
}

int I_SoundIsPlaying(int handle) {
    int i;

    for (i = 0; i < NUM_CHANNELS; ++i) {
        if (g_channels[i] && g_channelhandles[i] == handle) {
            return 1;
        }
    }
    return 0;
}

void I_UpdateSound(void) {
    register unsigned int sample;
    register int dl;
    register int dr;
    signed short *leftout;
    signed short *rightout;
    signed short *leftend;
    int step;
    int chan;

    if (!g_sound_ready) {
        return;
    }

    leftout = g_mixbuffer;
    rightout = g_mixbuffer + 1;
    step = 2;
    leftend = g_mixbuffer + (SAMPLECOUNT * step);

    while (leftout != leftend) {
        dl = 0;
        dr = 0;

        for (chan = 0; chan < NUM_CHANNELS; ++chan) {
            if (g_channels[chan]) {
                sample = *g_channels[chan];
                dl += g_channelleftvol_lookup[chan][sample];
                dr += g_channelrightvol_lookup[chan][sample];
                g_channelstepremainder[chan] += g_channelstep[chan];
                g_channels[chan] += g_channelstepremainder[chan] >> 16;
                g_channelstepremainder[chan] &= 65536 - 1;

                if (g_channels[chan] >= g_channelsend[chan]) {
                    g_channels[chan] = 0;
                }
            }
        }

        if (dl > 0x7fff) {
            *leftout = 0x7fff;
        } else if (dl < -0x8000) {
            *leftout = -0x8000;
        } else {
            *leftout = (signed short)dl;
        }

        if (dr > 0x7fff) {
            *rightout = 0x7fff;
        } else if (dr < -0x8000) {
            *rightout = -0x8000;
        } else {
            *rightout = (signed short)dr;
        }

        leftout += step;
        rightout += step;
    }
}

void I_SubmitSound(void) {
    int total_size;
    int written;

    if (!g_sound_ready) {
        return;
    }

    total_size = (int)sizeof(g_mixbuffer);
    written = sys_audio_write(g_mixbuffer, (uint32_t)total_size);
    if (written <= 0) {
        g_sound_ready = 0;
        (void)sys_audio_stop();
    }
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
    int slot;
    int separation;
    int rightvol;
    int leftvol;

    (void)pitch;

    for (slot = 0; slot < NUM_CHANNELS; ++slot) {
        if (g_channels[slot] && g_channelhandles[slot] == handle) {
            separation = sep + 1;
            leftvol = vol - ((vol * separation * separation) >> 16);
            separation = separation - 257;
            rightvol = vol - ((vol * separation * separation) >> 16);

            if (rightvol < 0) {
                rightvol = 0;
            } else if (rightvol > 127) {
                rightvol = 127;
            }
            if (leftvol < 0) {
                leftvol = 0;
            } else if (leftvol > 127) {
                leftvol = 127;
            }

            g_channelleftvol_lookup[slot] = &g_vol_lookup[leftvol * 256];
            g_channelrightvol_lookup[slot] = &g_vol_lookup[rightvol * 256];
            return;
        }
    }
}

void I_ShutdownSound(void) {
    if (!g_sound_ready) {
        return;
    }

    g_sound_ready = 0;
    (void)sys_audio_stop();
}

void I_InitSound(void) {
    struct audio_swpar params;
    int i;

    AUDIO_INITPAR(&params);
    params.sig = 1u;
    params.le = 1u;
    params.bits = 16u;
    params.bps = 2u;
    params.rate = SAMPLERATE;
    params.pchan = 2u;
    params.rchan = 2u;
    params.nblks = 4u;
    params.round = 512u;

    memset(g_lengths, 0, sizeof(g_lengths));
    memset(g_mixbuffer, 0, sizeof(g_mixbuffer));
    memset(g_channelstep, 0, sizeof(g_channelstep));
    memset(g_channelstepremainder, 0, sizeof(g_channelstepremainder));
    memset(g_channels, 0, sizeof(g_channels));
    memset(g_channelsend, 0, sizeof(g_channelsend));
    memset(g_channelstart, 0, sizeof(g_channelstart));
    memset(g_channelhandles, 0, sizeof(g_channelhandles));
    memset(g_channelids, 0, sizeof(g_channelids));
    I_SetChannels();

    if (sys_audio_set_params(&params) != 0 || sys_audio_start() != 0) {
        fprintf(stderr, "I_InitSound: backend de audio indisponivel\n");
        g_sound_ready = 0;
        return;
    }

    for (i = 1; i < NUMSFX; ++i) {
        if (!S_sfx[i].link) {
            S_sfx[i].data = doom_getsfx(S_sfx[i].name, &g_lengths[i]);
        } else {
            int link_index = (int)(S_sfx[i].link - S_sfx);

            S_sfx[i].data = S_sfx[i].link->data;
            if (link_index >= 0 && link_index < NUMSFX) {
                g_lengths[i] = g_lengths[link_index];
            }
        }
    }

    g_sound_ready = 1;
    fprintf(stderr, "I_InitSound: sound module ready\n");
}

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) {
    snd_MusicVolume = volume;
}
void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }
int I_RegisterSong(void *data) { (void)data; return 1; }
void I_PlaySong(int handle, int looping) { (void)handle; (void)looping; }
void I_StopSong(int handle) { (void)handle; }
void I_UnRegisterSong(int handle) { (void)handle; }
