/*
 * Save system using INI format
 * Uses inih library by Ben Hoyt (https://github.com/benhoyt/inih)
 * 
 * Converts the binary save_t structure to human-readable INI format
 * for easier debugging and cross-platform compatibility.
 */

#ifndef SAVE_INI_H
#define SAVE_INI_H

#include "wipeout/game.h"

// Load save data from INI format
bool save_load_ini(save_t *save, const char *filename);

// Save data to INI format  
bool save_write_ini(const save_t *save, const char *filename);

// Get default save filename
const char *save_get_ini_filename(void);

#endif