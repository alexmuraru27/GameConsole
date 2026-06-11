# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""Square-wave synthesis and playback via pygame.mixer. No Qt dependencies.

The synthesized audio mimics the buzzer: a plain square wave per note,
silence for PAUSE (frequency 0) entries, with a ~1 ms fade at note edges to
avoid clicks that the real hardware would not produce anyway.
"""

import os
import time

import numpy as np

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
import pygame

SAMPLE_RATE = 44100
AMPLITUDE = 6000
PREVIEW_DURATION_MS = 200
_MIXER_BUFFER = 512


def init():
    pygame.mixer.init(SAMPLE_RATE, -16, 1, _MIXER_BUFFER)


def shutdown():
    pygame.mixer.quit()


def synth_note(frequency_hz, duration_ms):
    """Render one square-wave note (or silence for frequency 0) as int16 mono."""
    sample_count = max(1, SAMPLE_RATE * duration_ms // 1000)
    if frequency_hz == 0:
        return np.zeros(sample_count, dtype=np.int16)
    t = np.arange(sample_count)
    wave = ((t * frequency_hz * 2 // SAMPLE_RATE) % 2) * 2 - 1
    samples = wave * AMPLITUDE
    fade = min(sample_count // 4, SAMPLE_RATE // 1000)
    if fade > 0:
        ramp = np.linspace(0.0, 1.0, fade)
        samples[:fade] = samples[:fade] * ramp
        samples[-fade:] = samples[-fade:] * ramp[::-1]
    return samples.astype(np.int16)


def synth_sequence(sequence):
    buffers = [synth_note(freq, dur) for freq, dur in sequence]
    return np.concatenate(buffers) if buffers else np.zeros(1, dtype=np.int16)


def _make_sound(samples, volume):
    sound = pygame.sndarray.make_sound(np.ascontiguousarray(samples))
    sound.set_volume(volume)
    return sound


class Player:
    """Plays one sequence at a time and tracks its position for the playhead.

    The position clock is wall time based: pygame gives no sample-accurate
    position, so pause() remembers when it stopped and resume() shifts the
    start reference by the time spent paused.
    """

    def __init__(self, volume=0.6):
        self.volume = volume
        self.total_ms = 0
        self._sound = None
        self._channel = None
        self._paused = False
        self._started_at = 0.0
        self._paused_at = 0.0

    @property
    def is_active(self):
        return self._channel is not None

    @property
    def is_paused(self):
        return self._paused

    def set_volume(self, volume):
        self.volume = volume
        if self._sound is not None:
            self._sound.set_volume(volume)

    def preview(self, frequency_hz):
        """Play a short standalone note; independent of sequence playback."""
        if frequency_hz == 0:
            return
        _make_sound(synth_note(frequency_hz, PREVIEW_DURATION_MS), self.volume).play()

    def play(self, sequence):
        self.stop()
        self._sound = _make_sound(synth_sequence(sequence), self.volume)
        self._channel = self._sound.play()
        self._started_at = time.monotonic()
        self.total_ms = sum(dur for _freq, dur in sequence)

    def pause(self):
        if self._channel is not None and not self._paused:
            self._channel.pause()
            self._paused_at = time.monotonic()
            self._paused = True

    def resume(self):
        if self._channel is not None and self._paused:
            self._channel.unpause()
            self._started_at += time.monotonic() - self._paused_at
            self._paused = False

    def stop(self):
        if self._channel is not None:
            self._channel.stop()
        self._channel = None
        self._sound = None
        self._paused = False

    def elapsed_ms(self):
        return (time.monotonic() - self._started_at) * 1000.0
