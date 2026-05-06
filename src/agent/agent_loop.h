#pragma once
/*
 * T113Claw Agent Loop
 *
 * Core ReAct loop: consume inbound messages, build context,
 * call LLM with tools, execute tool calls, iterate.
 */

/* Initialize the agent loop */
int agent_loop_init(void);

/* Start the agent loop thread (blocks on inbound queue) */
int agent_loop_start(void);

/* Stop the agent loop gracefully */
void agent_loop_stop(void);
