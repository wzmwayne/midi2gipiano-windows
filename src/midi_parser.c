#include "midi_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define INITIAL_CAPACITY 1024

/* ── helpers ──────────────────────────────────────────── */

static uint32_t read32be(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | p[3];
}

static uint16_t read16be(const unsigned char* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* variable-length value (MIDI convention) */
static uint64_t read_vlq(const unsigned char* buf, int* pos) {
    uint64_t val = 0;
    unsigned char c;
    do {
        c = buf[(*pos)++];
        val = (val << 7) | (c & 0x7F);
    } while (c & 0x80);
    return val;
}

/* ── note collection ─────────────────────────────────── */

typedef struct {
    long long startTick;
    int  noteNumber;
    int  velocity;
    int  channel;
    int  track;
} PendingOn;

static int add_note(MidiNote** notes, int* count, int* cap,
                     int note, int vel, int ch, int trk,
                     long long startTick, long long endTick)
{
    if (*count >= *cap) {
        *cap *= 2;
        MidiNote* tmp = realloc(*notes, (size_t)*cap * sizeof(MidiNote));
        if (!tmp) return 0;
        *notes = tmp;
    }
    MidiNote* n = &(*notes)[(*count)++];
    n->noteNumber  = note;
    n->velocity    = vel;
    n->channel     = ch;
    n->startMicros = startTick;
    n->endMicros   = endTick;
    return 1;
}

/* pending-on map: simple open-addressing hash table keyed by "note-ch-track" */
#define HT_SIZE 4096

typedef struct {
    long long key;   /* (note<<24)|(ch<<12)|track  packed */
    long long startTick;
    int       noteNumber;
    int       velocity;
    int       channel;
    int       track;
    int       used;
} HTEntry;

static long long pack_key(int note, int ch, int track) {
    return ((long long)note << 24) | ((long long)ch << 12) | track;
}

static HTEntry* ht_find(HTEntry* ht, long long key) {
    size_t idx = (size_t)(key % HT_SIZE);
    for (int i = 0; i < HT_SIZE; i++) {
        size_t j = (idx + i) % HT_SIZE;
        if (!ht[j].used || ht[j].key == key)
            return &ht[j];
    }
    return NULL; /* full – should never happen with reasonable music */
}

/* ── main parse ──────────────────────────────────────── */

int midi_parse(const char* filepath, ParsedMidi* out)
{
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "无法打开文件: %s\n", filepath);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    unsigned char* data = (unsigned char*)malloc((size_t)fsize);
    if (!data) { fclose(f); return 0; }
    if (fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(data); fclose(f); return 0;
    }
    fclose(f);

    /* header */
    if (fsize < 14 || memcmp(data, "MThd", 4) != 0) {
        free(data);
        fprintf(stderr, "不是有效的 MIDI 文件\n");
        return 0;
    }

    int pos = 8; /* skip "MThd"+length */
    int format   = read16be(data + pos); pos += 2;
    int ntracks  = read16be(data + pos); pos += 2;
    int division = read16be(data + pos); pos += 2; /* PPQ */
    int ppq = division;

    (void)format;

    /* tempo map */
    typedef struct { long long tick; int tempo; } TempoChange;
    int tcap = 16, tcount = 0;
    TempoChange* tempos = malloc((size_t)tcap * sizeof(TempoChange));
    if (!tempos) { free(data); fclose(f); return 0; }

    HTEntry pending[HT_SIZE];
    memset(pending, 0, sizeof(pending));

    MidiNote* notes = NULL;
    int ncount = 0, ncap = INITIAL_CAPACITY;
    notes = malloc((size_t)ncap * sizeof(MidiNote));
    if (!notes) { free(data); free(tempos); fclose(f); return 0; }

    long long maxTick = 0;

    /* parse tracks */
    for (int t = 0; t < ntracks && pos < fsize - 8; t++) {
        if (memcmp(data + pos, "MTrk", 4) != 0) break;
        pos += 4;
        long trkLen = read32be(data + pos); pos += 4;
        long trkEnd = pos + (long)trkLen;

        int runningStatus = 0;
        long long currentTick = 0;
        while (pos < trkEnd) {
            long long delta = read_vlq(data, &pos);
            currentTick += delta;
            int event = data[pos++];

            if (event == 0xFF) {
                /* meta event */
                int type = data[pos++];
                int len  = (int)read_vlq(data, &pos);

                if (type == 0x51 && len >= 3) {
                    /* set tempo */
                    int tempo = (data[pos] << 16) | (data[pos+1] << 8) | data[pos+2];
                    if (tcount >= tcap) {
                        tcap *= 2;
                        TempoChange* tmp = realloc(tempos, (size_t)tcap * sizeof(TempoChange));
                        if (!tmp) { free(data); free(tempos); free(notes); fclose(f); return 0; }
                        tempos = tmp;
                    }
                    tempos[tcount].tick  = currentTick;
                    tempos[tcount].tempo = tempo;
                    tcount++;
                }
                /* else skip */
                pos += len;

            } else if (event == 0xF0 || event == 0xF7) {
                /* sysex – skip */
                int len = (int)read_vlq(data, &pos);
                pos += len;

            } else {
                /* MIDI channel event */
                int status;
                if (event & 0x80) {
                    status = event;
                    runningStatus = status;
                } else {
                    status = runningStatus;
                    pos--; /* rewind: data byte belongs to this event */
                }

                int cmd = status & 0xF0;
                int ch  = status & 0x0F;

                if (cmd == 0x90 || cmd == 0x80) {
                    int note = data[pos++];
                    int vel  = data[pos++];
                    if (cmd == 0x90 && vel > 0) {
                        /* Note On */
                        long long key = pack_key(note, ch, t);
                        HTEntry* e = ht_find(pending, key);
                        if (e) {
                            e->used       = 1;
                            e->key        = key;
                            e->startTick  = currentTick;
                            e->noteNumber = note;
                            e->velocity   = vel;
                            e->channel    = ch;
                            e->track      = t;
                        }
                    } else {
                        /* Note Off (0x80 or Note On with vel=0) */
                        long long key = pack_key(note, ch, t);
                        HTEntry* e = ht_find(pending, key);
                        if (e && e->used) {
                            if (!add_note(&notes, &ncount, &ncap,
                                          e->noteNumber, e->velocity, e->channel, e->track,
                                          e->startTick, currentTick)) {
                                free(data); free(tempos); free(notes); return 0;
                            }
                            if (currentTick > maxTick) maxTick = currentTick;
                            e->used = 0;
                        }
                    }
                } else if (cmd == 0xC0 || cmd == 0xD0) {
                    /* 1-byte events: Program Change, Channel Aftertouch */
                    pos++;
                } else {
                    /* 2-byte events: Poly Aftertouch (0xA0), Control Change (0xB0),
                     * Pitch Bend (0xE0), and others */
                    pos += 2;
                }
            }
        }
    }

    /* flush pending notes (note-on without note-off) */
    for (int i = 0; i < HT_SIZE; i++) {
        if (pending[i].used) {
            if (!add_note(&notes, &ncount, &ncap,
                          pending[i].noteNumber, pending[i].velocity,
                          pending[i].channel, pending[i].track,
                          pending[i].startTick, pending[i].startTick + 1)) {
                free(data); free(tempos); free(notes); return 0;
            }
        }
    }

    /* build tempo segments and convert ticks → micros */
    /* if no tempo changes, default 120 BPM = 500000 µs/quarter */
    if (tcount == 0) {
        tcap = 1; tcount = 1;
        TempoChange* tmp = realloc(tempos, sizeof(TempoChange));
        if (!tmp) { free(data); free(notes); return 0; }
        tempos = tmp;
        tempos[0].tick = 0;
        tempos[0].tempo = 500000;
    }

    /* sort tempo changes by tick */
    for (int i = 0; i < tcount - 1; i++) {
        for (int j = 0; j < tcount - 1 - i; j++) {
            if (tempos[j].tick > tempos[j+1].tick) {
                TempoChange tmp = tempos[j];
                tempos[j] = tempos[j+1];
                tempos[j+1] = tmp;
            }
        }
    }

    /* build segments with accumulated micros */
    typedef struct { long long tickStart, tickEnd; int tempo; long long cumMicros; } Segment;
    int scount = 0;
    Segment* segs = malloc((size_t)tcount * sizeof(Segment));
    if (!segs) { free(data); free(tempos); free(notes); return 0; }
    long long prevTick = 0;
    int prevTempo = 500000;

    for (int i = 0; i < tcount; i++) {
        long long tick = tempos[i].tick;
        int tempo = tempos[i].tempo;
        if (tick > maxTick) break;
        if (tick > prevTick) {
            segs[scount].tickStart = prevTick;
            segs[scount].tickEnd   = tick;
            segs[scount].tempo     = prevTempo;
            segs[scount].cumMicros = 0;
            scount++;
        }
        prevTick = tick;
        prevTempo = tempo;
    }
    if (prevTick < maxTick) {
        segs[scount].tickStart = prevTick;
        segs[scount].tickEnd   = maxTick;
        segs[scount].tempo     = prevTempo;
        segs[scount].cumMicros = 0;
        scount++;
    }

    long long cum = 0;
    for (int i = 0; i < scount; i++) {
        segs[i].cumMicros = cum;
        cum += (segs[i].tickEnd - segs[i].tickStart) * (long long)segs[i].tempo / ppq;
    }

    out->totalDurationMicros = cum;

    /* convert ticks → micros for each note */
    for (int i = 0; i < ncount; i++) {
        long long st = notes[i].startMicros; /* currently holds startTick */
        long long et = notes[i].endMicros;   /* currently holds endTick   */

        long long sm = 0, em = 0;
        for (int s = 0; s < scount; s++) {
            if (st >= segs[s].tickStart && st < segs[s].tickEnd) {
                sm = segs[s].cumMicros + (st - segs[s].tickStart) * segs[s].tempo / ppq;
            }
            if (et >= segs[s].tickStart && et < segs[s].tickEnd) {
                em = segs[s].cumMicros + (et - segs[s].tickStart) * segs[s].tempo / ppq;
            }
        }
        /* handle ticks exactly at boundaries */
        if (scount > 0) {
            Segment* last = &segs[scount-1];
            if (st >= last->tickEnd)
                sm = last->cumMicros + (st - last->tickStart) * last->tempo / ppq;
            if (et >= last->tickEnd)
                em = last->cumMicros + (et - last->tickStart) * last->tempo / ppq;
        }

        notes[i].startMicros = sm;
        notes[i].endMicros   = em;
    }

    /* sort notes by startMicros (bubble sort – small set) */
    for (int i = 0; i < ncount - 1; i++) {
        for (int j = 0; j < ncount - 1 - i; j++) {
            if (notes[j].startMicros > notes[j+1].startMicros) {
                MidiNote tmp = notes[j];
                notes[j] = notes[j+1];
                notes[j+1] = tmp;
            }
        }
    }

    out->notes      = notes;
    out->noteCount  = ncount;
    out->noteCapacity = ncap;
    out->trackCount = ntracks;
    out->ppq        = ppq;

    free(data);
    free(tempos);
    free(segs);
    return 1;
}

void midi_free(ParsedMidi* pm)
{
    if (pm->notes) free(pm->notes);
    pm->notes    = NULL;
    pm->noteCount = 0;
    pm->noteCapacity = 0;
}
