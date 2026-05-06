#pragma once
/*
 * T113Claw Audio — Codec Initialization
 *
 * Configures the T113-S3 internal audio codec via amixer commands.
 * Sets up MIC3 input, headphone output, and volume levels.
 */

/* Initialize the audio codec (amixer settings for MIC3, HP output, etc.) */
int audio_codec_init(void);

/* Set playback volume (0-100, mapped to codec range) */
int audio_codec_set_volume(int volume_pct);

/* Get current playback volume (0-100) */
int audio_codec_get_volume(void);

/* Enable/disable the external amplifier (GPIO34 / PB2) */
int audio_amp_enable(int enable);
