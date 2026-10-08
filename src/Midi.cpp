#include "Midi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

/* Event while parsing: ticks, plus track and order so the merge is stable. */
struct RawEvent {
	uint32_t tick;
	uint32_t order;
	uint8_t status, data1, data2;
};

struct TempoChange {
	uint32_t tick;
	uint32_t usPerQuarter;
	uint32_t order;
};

static uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t read_be16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

/* MIDI variable-length quantity: 7 bits per byte, high bit = more follows. Max 4 bytes. */
static bool read_vlq(const uint8_t **p, const uint8_t *end, uint32_t *value)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		if (*p >= end)
			return false;
		uint8_t b = *(*p)++;
		v = (v << 7) | (b & 0x7F);
		if (!(b & 0x80)) {
			*value = v;
			return true;
		}
	}
	return false;
}

/* Number of data bytes after a channel status byte. */
static int channel_data_bytes(uint8_t status)
{
	uint8_t kind = status & 0xF0;
	return (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
}

static bool parse_track(const uint8_t *p, const uint8_t *end, std::vector<RawEvent> *events,
	std::vector<TempoChange> *tempos, uint32_t *endTick, uint32_t *order, const char *path)
{
	uint32_t tick = 0;
	uint8_t running = 0;

	while (p < end) {
		uint32_t delta;
		if (!read_vlq(&p, end, &delta))
			goto truncated;
		tick += delta;
		if (p >= end)
			goto truncated;

		uint8_t status = *p;
		if (status & 0x80) {
			p++;
		} else {
			/* Running status: reuse the previous channel status, this byte is data. */
			if (!running) {
				fprintf(stderr, "%s: data byte without a status\n", path);
				return false;
			}
			status = running;
		}

		if (status == 0xFF) {
			/* Meta event: type, length, payload. */
			if (p >= end)
				goto truncated;
			uint8_t type = *p++;
			uint32_t len;
			if (!read_vlq(&p, end, &len) || len > (uint32_t)(end - p))
				goto truncated;
			if (type == 0x51 && len == 3) {
				TempoChange t;
				t.tick = tick;
				t.usPerQuarter = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
				t.order = (*order)++;
				tempos->push_back(t);
			} else if (type == 0x2F) {
				if (tick > *endTick)
					*endTick = tick;
				return true;
			}
			p += len;
			running = 0;
		} else if (status == 0xF0 || status == 0xF7) {
			/* Sysex: length-prefixed, ignored. */
			uint32_t len;
			if (!read_vlq(&p, end, &len) || len > (uint32_t)(end - p))
				goto truncated;
			p += len;
			running = 0;
		} else if (status >= 0x80 && status < 0xF0) {
			int n = channel_data_bytes(status);
			if (end - p < n)
				goto truncated;
			RawEvent e;
			e.tick = tick;
			e.order = (*order)++;
			e.status = status;
			e.data1 = p[0] & 0x7F;
			e.data2 = n == 2 ? (p[1] & 0x7F) : 0;
			p += n;
			running = status;
			events->push_back(e);
		} else {
			fprintf(stderr, "%s: unsupported status byte 0x%02x\n", path, status);
			return false;
		}
	}
	/* Track without an end-of-track event: its last event ends it. */
	if (tick > *endTick)
		*endTick = tick;
	return true;

truncated:
	fprintf(stderr, "%s: truncated track\n", path);
	return false;
}

static bool is_note_off(const RawEvent &e)
{
	uint8_t kind = e.status & 0xF0;
	return kind == 0x80 || (kind == 0x90 && e.data2 == 0);
}

bool midi_load(Song *song, const char *path, int sampleRate)
{
	memset(song, 0, sizeof(*song));

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open %s\n", path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> file((size_t)(size > 0 ? size : 0));
	if (size <= 0 || fread(file.data(), 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "Cannot read %s\n", path);
		fclose(f);
		return false;
	}
	fclose(f);

	const uint8_t *data = file.data();
	if (size < 14 || memcmp(data, "MThd", 4) != 0 || read_be32(data + 4) < 6) {
		fprintf(stderr, "%s: not a Standard MIDI File\n", path);
		return false;
	}
	uint16_t format = read_be16(data + 8);
	uint16_t tracks = read_be16(data + 10);
	uint16_t division = read_be16(data + 12);
	if (format > 1) {
		fprintf(stderr, "%s: MIDI format %u, only 0 and 1 are supported\n", path, format);
		return false;
	}
	if (division & 0x8000) {
		fprintf(stderr, "%s: SMPTE time division is not supported\n", path);
		return false;
	}
	if (division == 0) {
		fprintf(stderr, "%s: zero ticks per quarter note\n", path);
		return false;
	}

	std::vector<RawEvent> events;
	std::vector<TempoChange> tempos;
	uint32_t endTick = 0;
	uint32_t order = 0;

	long pos = 8 + (long)read_be32(data + 4);
	int found = 0;
	while (pos + 8 <= size && found < tracks) {
		uint32_t len = read_be32(data + pos + 4);
		const uint8_t *body = data + pos + 8;
		if ((long)len > size - pos - 8) {
			fprintf(stderr, "%s: track runs past the end of the file\n", path);
			return false;
		}
		if (memcmp(data + pos, "MTrk", 4) == 0) {
			if (!parse_track(body, body + len, &events, &tempos, &endTick, &order, path))
				return false;
			found++;
		}
		pos += 8 + (long)len;	/* unknown chunk types are skipped */
	}
	if (found == 0) {
		fprintf(stderr, "%s: no tracks\n", path);
		return false;
	}

	/* Merge tracks by time. At equal ticks note-offs go first so a re-struck note is not cut. */
	std::stable_sort(events.begin(), events.end(), [](const RawEvent &a, const RawEvent &b) {
		if (a.tick != b.tick)
			return a.tick < b.tick;
		bool offA = is_note_off(a), offB = is_note_off(b);
		if (offA != offB)
			return offA;
		return a.order < b.order;
	});
	std::stable_sort(tempos.begin(), tempos.end(), [](const TempoChange &a, const TempoChange &b) {
		return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
	});

	/*
	 * Ticks to samples, walking the tempo map. Seconds per tick = tempo / (1e6 * division).
	 * Accumulate in double so long songs do not drift.
	 */
	auto to_sample = [&](uint32_t tick, size_t *tempoIndex, uint32_t *segTick, double *segSeconds,
		uint32_t *usPerQuarter) -> uint32_t {
		while (*tempoIndex < tempos.size() && tempos[*tempoIndex].tick <= tick) {
			const TempoChange &t = tempos[*tempoIndex];
			*segSeconds += (double)(t.tick - *segTick) * (double)*usPerQuarter / (1e6 * division);
			*segTick = t.tick;
			*usPerQuarter = t.usPerQuarter;
			(*tempoIndex)++;
		}
		double seconds = *segSeconds + (double)(tick - *segTick) * (double)*usPerQuarter / (1e6 * division);
		return (uint32_t)(seconds * sampleRate + 0.5);
	};

	song->count = (int)events.size();
	song->events = (MidiEvent *)malloc(sizeof(MidiEvent) * (events.empty() ? 1 : events.size()));
	size_t tempoIndex = 0;
	uint32_t segTick = 0;
	double segSeconds = 0.0;
	uint32_t usPerQuarter = 500000;	/* 120 BPM until told otherwise */
	for (size_t i = 0; i < events.size(); i++) {
		MidiEvent *e = &song->events[i];
		e->sample = to_sample(events[i].tick, &tempoIndex, &segTick, &segSeconds, &usPerQuarter);
		e->status = events[i].status;
		e->data1 = events[i].data1;
		e->data2 = events[i].data2;
	}
	song->length = to_sample(endTick, &tempoIndex, &segTick, &segSeconds, &usPerQuarter);
	if (song->count > 0 && song->length < song->events[song->count - 1].sample)
		song->length = song->events[song->count - 1].sample;
	if (song->length == 0) {
		fprintf(stderr, "%s: song has no length\n", path);
		midi_free(song);
		return false;
	}
	return true;
}

void midi_free(Song *song)
{
	free(song->events);
	memset(song, 0, sizeof(*song));
}

/* ------------------------------------------------------------------------- */
/* Player                                                                    */
/* ------------------------------------------------------------------------- */

bool music_open(MusicPlayer *player, const char *path, int sampleRate)
{
	memset(player, 0, sizeof(*player));
	if (!midi_load(&player->song, path, sampleRate))
		return false;
	synth_init(&player->synth, sampleRate);
	return true;
}

void music_close(MusicPlayer *player)
{
	midi_free(&player->song);
}

void music_render(MusicPlayer *player, int16_t *out, int frames)
{
	const Song *song = &player->song;
	while (frames > 0) {
		/* Send everything due now. */
		while (player->next < song->count && song->events[player->next].sample <= player->position) {
			const MidiEvent *e = &song->events[player->next++];
			synth_message(&player->synth, e->status, e->data1, e->data2);
		}

		/* Loop: release what is still sounding and start over. */
		if (player->position >= song->length) {
			synth_all_notes_off(&player->synth);
			player->position = 0;
			player->next = 0;
			continue;
		}

		/* Render up to the next event, the loop point or the end of the request. */
		uint32_t until = song->length;
		if (player->next < song->count && song->events[player->next].sample < until)
			until = song->events[player->next].sample;
		uint32_t n = until - player->position;
		if (n > (uint32_t)frames)
			n = (uint32_t)frames;

		synth_render(&player->synth, out, (int)n);
		out += n * 2;
		frames -= (int)n;
		player->position += n;
	}
}
