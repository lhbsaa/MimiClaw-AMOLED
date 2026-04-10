#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the heartbeat service (logs ready state).
 */
esp_err_t heartbeat_init(void);

/**
 * Start the heartbeat timer. Checks HEARTBEAT.md periodically
 * and sends a prompt to the agent if actionable tasks are found.
 */
esp_err_t heartbeat_start(void);

/**
 * Stop and delete the heartbeat timer.
 */
void heartbeat_stop(void);

/**
 * Manually trigger a heartbeat check (for CLI testing).
 * Returns true if the agent was prompted, false if no tasks found.
 */
bool heartbeat_trigger(void);

/**
 * Poll for pending heartbeat work. Call this from a task context
 * (e.g. cron task loop) to execute deferred SPIFFS I/O that the
 * timer callback cannot safely perform.
 */
void heartbeat_poll(void);
