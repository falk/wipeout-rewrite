/*
 * Save system using INI format
 * Uses inih library by Ben Hoyt (https://github.com/benhoyt/inih)
 * 
 * Converts the binary save_t structure to human-readable INI format
 * for easier debugging and cross-platform compatibility.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "save_ini.h"
#include "libs/ini.h"
#include "platform.h"
#include "mem.h"
#include "wipeout/game.h"

// Save context for parsing
typedef struct {
    save_t *save;
    bool success;
} save_parse_context_t;

// INI handler callback for parsing save data
static int save_ini_handler(void *user, const char *section, const char *name, const char *value) {
    save_parse_context_t *ctx = (save_parse_context_t*)user;
    save_t *save = ctx->save;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

    // Settings section
    if (MATCH("settings", "sfx_volume")) {
        save->sfx_volume = atof(value);
    } else if (MATCH("settings", "music_volume")) {
        save->music_volume = atof(value);
    } else if (MATCH("settings", "internal_roll")) {
        save->internal_roll = atof(value);
    } else if (MATCH("settings", "ui_scale")) {
        save->ui_scale = (uint8_t)atoi(value);
    } else if (MATCH("settings", "show_fps")) {
        save->show_fps = strcmp(value, "true") == 0;
    } else if (MATCH("settings", "fullscreen")) {
        save->fullscreen = strcmp(value, "true") == 0;
    } else if (MATCH("settings", "screen_res")) {
        save->screen_res = atoi(value);
    } else if (MATCH("settings", "post_effect")) {
        save->post_effect = atoi(value);
    } else if (MATCH("settings", "screen_shake")) {
        save->screen_shake = atof(value);
    } else if (MATCH("settings", "analog_response")) {
        save->analog_response = atof(value);
    } else if (MATCH("settings", "less_punishing_ship_collisions")) {
        save->less_punishing_ship_collisions = strcmp(value, "true") == 0;
    } else if (MATCH("settings", "wall_grinding_mode")) {
        save->wall_grinding_mode = strcmp(value, "true") == 0;
    } else if (MATCH("settings", "tunnel_reverb_enabled")) {
        save->tunnel_reverb_enabled = strcmp(value, "true") == 0;
    } else if (MATCH("settings", "smart_weapon_system")) {
        save->smart_weapon_displacement = strcmp(value, "true") == 0;
    } else if (MATCH("settings", "engine_trails")) {
        save->engine_trails = strcmp(value, "true") == 0;
    }
    
    // Progress section
    else if (MATCH("progress", "has_rapier_class")) {
        save->has_rapier_class = (uint32_t)atoi(value);
    } else if (MATCH("progress", "has_bonus_circuts")) {
        save->has_bonus_circuts = (uint32_t)atoi(value);
    }
    
    // Player section
    else if (MATCH("player", "highscores_name")) {
        strncpy(save->highscores_name, value, 3);
        save->highscores_name[3] = '\0';
    }
    
    // Controls section
    else if (strncmp(section, "controls", 8) == 0) {
        // Parse button mappings: button_0_0, button_0_1, etc.
        if (strncmp(name, "button_", 7) == 0) {
            int action, index;
            if (sscanf(name, "button_%d_%d", &action, &index) == 2) {
                if (action >= 0 && action < NUM_GAME_ACTIONS && index >= 0 && index < 2) {
                    save->buttons[action][index] = (uint8_t)atoi(value);
                }
            }
        }
    }
    
    // Highscores sections: highscores_class_circuit_tab
    else if (strncmp(section, "highscores_", 11) == 0) {
        int race_class, circuit, tab;
        if (sscanf(section, "highscores_%d_%d_%d", &race_class, &circuit, &tab) == 3) {
            if (race_class >= 0 && race_class < NUM_RACE_CLASSES &&
                circuit >= 0 && circuit < NUM_CIRCUTS &&
                tab >= 0 && tab < NUM_HIGHSCORE_TABS) {
                
                highscores_t *hs = &save->highscores[race_class][circuit][tab];
                
                if (strcmp(name, "lap_record") == 0) {
                    hs->lap_record = atof(value);
                } else {
                    // Parse individual entries: entry_0_name, entry_0_time, etc.
                    int entry;
                    char field[16];
                    if (sscanf(name, "entry_%d_%15s", &entry, field) == 2) {
                        if (entry >= 0 && entry < NUM_HIGHSCORES) {
                            if (strcmp(field, "name") == 0) {
                                strncpy(hs->entries[entry].name, value, 3);
                                hs->entries[entry].name[3] = '\0';
                            } else if (strcmp(field, "time") == 0) {
                                hs->entries[entry].time = atof(value);
                            }
                        }
                    }
                }
            }
        }
    }

    return 1; // Continue parsing
}

bool save_load_ini(save_t *save, const char *filename) {
    if (!save || !filename) return false;

    // Load file data
    uint32_t size;
    uint8_t *data = platform_load_userdata(filename, &size);
    if (!data || size == 0) {
        return false;
    }

    // Initialize save with defaults
    memset(save, 0, sizeof(save_t));
    save->magic = SAVE_DATA_MAGIC;
    save->sfx_volume = 0.6f;
    save->music_volume = 0.6f;
    save->internal_roll = 1.0f;
    save->ui_scale = 1;
    save->screen_res = 0;
    save->post_effect = 0;
    save->screen_shake = 1.0f;
    save->analog_response = 1.0f;
    
    // Parse INI data
    save_parse_context_t ctx = { save, true };
    
    // Create temporary null-terminated string for inih
    char *ini_data = mem_temp_alloc(size + 1);
    if (!ini_data) {
        mem_temp_free(data);
        return false;
    }
    memcpy(ini_data, data, size);
    ini_data[size] = '\0';
    
    int result = ini_parse_string(ini_data, save_ini_handler, &ctx);
    
    // Free temporary allocations
    mem_temp_free(ini_data);
    mem_temp_free(data);
    
    return result == 0 && ctx.success;
}

bool save_write_ini(const save_t *save, const char *filename) {
    if (!save || !filename) return false;

    // Build INI content
    char *content = mem_temp_alloc(32768); // 32KB should be enough
    if (!content) return false;
    
    int pos = 0;
    
    // Write header comment
    pos += snprintf(content + pos, 32768 - pos,
        "; Wipeout Rewrite Save File\n"
        "; This file stores game settings, progress, and high scores\n"
        "; You can edit this file manually if needed\n\n");
    
    // Settings section
    pos += snprintf(content + pos, 32768 - pos, "[settings]\n");
    pos += snprintf(content + pos, 32768 - pos, "sfx_volume=%.6f\n", save->sfx_volume);
    pos += snprintf(content + pos, 32768 - pos, "music_volume=%.6f\n", save->music_volume);
    pos += snprintf(content + pos, 32768 - pos, "internal_roll=%.6f\n", save->internal_roll);
    pos += snprintf(content + pos, 32768 - pos, "ui_scale=%d\n", save->ui_scale);
    pos += snprintf(content + pos, 32768 - pos, "show_fps=%s\n", save->show_fps ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "fullscreen=%s\n", save->fullscreen ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "screen_res=%d\n", save->screen_res);
    pos += snprintf(content + pos, 32768 - pos, "post_effect=%d\n", save->post_effect);
    pos += snprintf(content + pos, 32768 - pos, "screen_shake=%.6f\n", save->screen_shake);
    pos += snprintf(content + pos, 32768 - pos, "analog_response=%.6f\n", save->analog_response);
    pos += snprintf(content + pos, 32768 - pos, "less_punishing_ship_collisions=%s\n", save->less_punishing_ship_collisions ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "wall_grinding_mode=%s\n", save->wall_grinding_mode ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "tunnel_reverb_enabled=%s\n", save->tunnel_reverb_enabled ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "smart_weapon_system=%s\n", save->smart_weapon_displacement ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "engine_trails=%s\n", save->engine_trails ? "true" : "false");
    pos += snprintf(content + pos, 32768 - pos, "\n");
    
    // Progress section
    pos += snprintf(content + pos, 32768 - pos, "[progress]\n");
    pos += snprintf(content + pos, 32768 - pos, "has_rapier_class=%u\n", save->has_rapier_class);
    pos += snprintf(content + pos, 32768 - pos, "has_bonus_circuts=%u\n", save->has_bonus_circuts);
    pos += snprintf(content + pos, 32768 - pos, "\n");
    
    // Player section
    pos += snprintf(content + pos, 32768 - pos, "[player]\n");
    pos += snprintf(content + pos, 32768 - pos, "highscores_name=%.3s\n", save->highscores_name);
    pos += snprintf(content + pos, 32768 - pos, "\n");
    
    // Controls section
    pos += snprintf(content + pos, 32768 - pos, "[controls]\n");
    for (int action = 0; action < NUM_GAME_ACTIONS; action++) {
        for (int index = 0; index < 2; index++) {
            pos += snprintf(content + pos, 32768 - pos, "button_%d_%d=%d\n", 
                           action, index, save->buttons[action][index]);
        }
    }
    pos += snprintf(content + pos, 32768 - pos, "\n");
    
    // Highscores sections
    for (int race_class = 0; race_class < NUM_RACE_CLASSES; race_class++) {
        for (int circuit = 0; circuit < NUM_CIRCUTS; circuit++) {
            for (int tab = 0; tab < NUM_HIGHSCORE_TABS; tab++) {
                const highscores_t *hs = &save->highscores[race_class][circuit][tab];
                
                // Only write non-empty highscore sections
                bool has_data = (hs->lap_record > 0.0f);
                for (int i = 0; i < NUM_HIGHSCORES && !has_data; i++) {
                    if (hs->entries[i].time > 0.0f || strlen(hs->entries[i].name) > 0) {
                        has_data = true;
                    }
                }
                
                if (has_data) {
                    pos += snprintf(content + pos, 32768 - pos, 
                                   "[highscores_%d_%d_%d]\n", race_class, circuit, tab);
                    pos += snprintf(content + pos, 32768 - pos, "lap_record=%.6f\n", hs->lap_record);
                    
                    for (int entry = 0; entry < NUM_HIGHSCORES; entry++) {
                        if (hs->entries[entry].time > 0.0f || strlen(hs->entries[entry].name) > 0) {
                            pos += snprintf(content + pos, 32768 - pos, "entry_%d_name=%.3s\n", 
                                           entry, hs->entries[entry].name);
                            pos += snprintf(content + pos, 32768 - pos, "entry_%d_time=%.6f\n", 
                                           entry, hs->entries[entry].time);
                        }
                    }
                    pos += snprintf(content + pos, 32768 - pos, "\n");
                }
            }
        }
    }
    
    // Save to file
    uint32_t bytes_written = platform_store_userdata(filename, content, pos);
    mem_temp_free(content);
    
    return bytes_written == pos;
}

const char *save_get_ini_filename(void) {
    return "save.ini";
}