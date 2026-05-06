#pragma once
/*
 * T113Claw Voice Manager
 *
 * Orchestrates:  Button → Record → STT → Agent → TTS → Playback
 *
 * State machine: IDLE → LISTENING → RECOGNIZING → THINKING → SPEAKING → IDLE
 */

/* Initialize voice subsystem (STT, TTS, GPIO) */
int voice_manager_init(void);

/* Start voice manager thread + button listener */
int voice_manager_start(void);

/* Stop voice manager */
void voice_manager_stop(void);

/*
 * Called by main loop's dispatch_outbound() when channel == "voice".
 * Delivers the agent response text to the voice manager for TTS playback.
 */
void voice_manager_on_response(const char *text);
