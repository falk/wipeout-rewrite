#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <malloc.h>

#include "platform.h"
#include "input.h"
#include "system.h"
#include "utils.h"
#include "mem.h"

// Global state
static bool wants_to_exit = false;
static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLSurface egl_surface;
static void (*audio_callback)(float *buffer, uint32_t len) = NULL;
static char *path_assets = "sdmc:/switch/wipeout/";
static char *path_userdata = "sdmc:/switch/wipeout/save/";
static PadState pad;
static HidTouchScreenState touch_state;
// RetroArch switch_audio.c style implementation
#define BUFFER_COUNT 5
#define SAMPLE_RATE 48000
#define NUM_CHANNELS 2
#define BUFFER_SIZE 8192  // Larger buffer for smoother audio

static AudioOutBuffer audio_buffers[BUFFER_COUNT];
static AudioOutBuffer *current_buffer = NULL;
static s16 *audio_buffer_data[BUFFER_COUNT];
static uint64_t last_append = 0;
static bool audio_blocking = false;
static bool audio_paused = false;
static unsigned audio_latency = 64;

// HD Rumble state
static bool hd_rumble_enabled = false;
static HidVibrationDeviceHandle vibration_handles[2]; // Left and right joy-con handles
static float rumble_intensity_left = 0.0f;
static float rumble_intensity_right = 0.0f;
static double rumble_fade_timer = 0.0;
static const double RUMBLE_FADE_TIME = 0.25; // 250ms fade time

// Exit button combination state
static double quit_button_hold_time = 0.0;
static const double QUIT_HOLD_TIME = 1.0; // 1 second to quit
static bool quit_buttons_held = false;

// Nintendo Switch button mapping
uint8_t platform_switch_button_map[] = {
    [HidNpadButton_A] = INPUT_GAMEPAD_A,
    [HidNpadButton_B] = INPUT_GAMEPAD_B,
    [HidNpadButton_X] = INPUT_GAMEPAD_X,
    [HidNpadButton_Y] = INPUT_GAMEPAD_Y,
    [HidNpadButton_StickL] = INPUT_GAMEPAD_L_STICK_PRESS,
    [HidNpadButton_StickR] = INPUT_GAMEPAD_R_STICK_PRESS,
    [HidNpadButton_L] = INPUT_GAMEPAD_L_SHOULDER,
    [HidNpadButton_R] = INPUT_GAMEPAD_R_SHOULDER,
    [HidNpadButton_ZL] = INPUT_GAMEPAD_L_TRIGGER,
    [HidNpadButton_ZR] = INPUT_GAMEPAD_R_TRIGGER,
    [HidNpadButton_Plus] = INPUT_GAMEPAD_START,
    [HidNpadButton_Minus] = INPUT_GAMEPAD_SELECT,
    [HidNpadButton_Left] = INPUT_GAMEPAD_DPAD_LEFT,
    [HidNpadButton_Up] = INPUT_GAMEPAD_DPAD_UP,
    [HidNpadButton_Right] = INPUT_GAMEPAD_DPAD_RIGHT,
    [HidNpadButton_Down] = INPUT_GAMEPAD_DPAD_DOWN,
    [HidNpadButton_StickLLeft] = INPUT_GAMEPAD_L_STICK_LEFT,
    [HidNpadButton_StickLUp] = INPUT_GAMEPAD_L_STICK_UP,
    [HidNpadButton_StickLRight] = INPUT_GAMEPAD_L_STICK_RIGHT,
    [HidNpadButton_StickLDown] = INPUT_GAMEPAD_L_STICK_DOWN,
    [HidNpadButton_StickRLeft] = INPUT_GAMEPAD_R_STICK_LEFT,
    [HidNpadButton_StickRUp] = INPUT_GAMEPAD_R_STICK_UP,
    [HidNpadButton_StickRRight] = INPUT_GAMEPAD_R_STICK_RIGHT,
    [HidNpadButton_StickRDown] = INPUT_GAMEPAD_R_STICK_DOWN,
};

// RetroArch-style audio write function
static size_t switch_audio_write(const void *buf, size_t size) {
    if (audio_paused)
        return 0;
        
    AudioOutBuffer *buffer = NULL;
    u32 released_count = 0;
    
    // Get released buffer (non-blocking)
    Result rc = audoutGetReleasedAudioOutBuffer(&buffer, &released_count);
    if (R_FAILED(rc) || buffer == NULL)
        return 0;
        
    // Calculate how much we can write
    size_t to_write = size;
    if (to_write > BUFFER_SIZE)
        to_write = BUFFER_SIZE;
        
    // Copy data to buffer
    memcpy(buffer->buffer, buf, to_write);
    buffer->data_size = to_write;
    
    // Flush cache and append
    armDCacheFlush(buffer->buffer, to_write);
    audoutAppendAudioOutBuffer(buffer);
    
    last_append = armGetSystemTick();
    return to_write;
}

// Audio update function
static void update_audio(void) {
    if (!audio_callback)
        return;
        
    // Try to fill available buffers
    AudioOutBuffer *buffer = NULL;
    u32 released_count = 0;
    
    // Check if we have released buffers to fill
    while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&buffer, &released_count)) && buffer != NULL) {
        // Get audio data (number of samples, not bytes)
        size_t sample_count = BUFFER_SIZE / sizeof(s16);
        static float temp_buffer[8192 / sizeof(s16)];  // Max buffer size in samples
        memset(temp_buffer, 0, sample_count * sizeof(float));
        audio_callback(temp_buffer, sample_count);
        
        // Convert to s16
        s16 *dst = (s16*)buffer->buffer;
        for (size_t i = 0; i < sample_count; i++) {
            float sample = temp_buffer[i];
            if (sample > 1.0f) sample = 1.0f;
            else if (sample < -1.0f) sample = -1.0f;
            dst[i] = (s16)(sample * 32767.0f);
        }
        
        buffer->data_size = BUFFER_SIZE;
        
        // Flush cache and append
        armDCacheFlush(buffer->buffer, BUFFER_SIZE);
        audoutAppendAudioOutBuffer(buffer);
        
        last_append = armGetSystemTick();
    }
}

// Initialize EGL
static bool init_egl(void) {
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        printf("Failed to get EGL display\n");
        return false;
    }
    
    if (!eglInitialize(egl_display, NULL, NULL)) {
        printf("Failed to initialize EGL\n");
        return false;
    }
    
    EGLConfig config;
    EGLint num_configs;
    static const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    
    if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs)) {
        printf("Failed to choose EGL config\n");
        return false;
    }
    
    egl_surface = eglCreateWindowSurface(egl_display, config, nwindowGetDefault(), NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        printf("Failed to create EGL surface\n");
        return false;
    }
    
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        printf("Failed to create EGL context\n");
        return false;
    }
    
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        printf("Failed to make EGL context current\n");
        return false;
    }
    
    if (!gladLoadGL()) {
        printf("Failed to load OpenGL\n");
        return false;
    }
    
    return true;
}

// Initialize audio (RetroArch switch_audio.c style)
static bool init_audio(void) {
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        printf("Failed to initialize audio: 0x%x\n", rc);
        return false;
    }
    
    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        printf("Failed to start audio: 0x%x\n", rc);
        audoutExit();
        return false;
    }
    
    // Allocate audio buffers (RetroArch uses BUFFER_COUNT = 5)
    for (int i = 0; i < BUFFER_COUNT; i++) {
        audio_buffer_data[i] = (s16*)memalign(0x1000, BUFFER_SIZE);
        if (!audio_buffer_data[i]) {
            printf("Failed to allocate audio buffer %d\n", i);
            return false;
        }
        
        memset(audio_buffer_data[i], 0, BUFFER_SIZE);
        
        audio_buffers[i].next = NULL;
        audio_buffers[i].buffer = audio_buffer_data[i];
        audio_buffers[i].buffer_size = BUFFER_SIZE;
        audio_buffers[i].data_size = BUFFER_SIZE;
        audio_buffers[i].data_offset = 0;
        
        armDCacheFlush(audio_buffer_data[i], BUFFER_SIZE);
        audoutAppendAudioOutBuffer(&audio_buffers[i]);
    }
    
    printf("Audio initialized (RetroArch style): %d buffers, %d bytes each\n", BUFFER_COUNT, BUFFER_SIZE);
    return true;
}

// Initialize HD Rumble
static void init_hd_rumble(void) {
    Result rc = hidInitializeVibrationDevices(vibration_handles, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    if (R_SUCCEEDED(rc)) {
        hd_rumble_enabled = true;
        printf("HD Rumble initialized successfully\n");
    } else {
        printf("Failed to initialize HD Rumble: 0x%x\n", rc);
        hd_rumble_enabled = false;
    }
}

// Update HD Rumble (call every frame)
static void update_hd_rumble(double delta_time) {
    if (!hd_rumble_enabled) return;
    
    // Fade out rumble over time
    if (rumble_fade_timer > 0.0) {
        rumble_fade_timer -= delta_time;
        if (rumble_fade_timer <= 0.0) {
            rumble_intensity_left = 0.0f;
            rumble_intensity_right = 0.0f;
            rumble_fade_timer = 0.0;
        } else {
            // Linear fade
            float fade_factor = (float)(rumble_fade_timer / RUMBLE_FADE_TIME);
            rumble_intensity_left *= fade_factor;
            rumble_intensity_right *= fade_factor;
        }
        
        // Apply rumble
        HidVibrationValue vibration_values[2];
        
        // Left Joy-Con
        vibration_values[0].amp_low = rumble_intensity_left;
        vibration_values[0].freq_low = 160.0f;
        vibration_values[0].amp_high = rumble_intensity_left * 0.8f;
        vibration_values[0].freq_high = 320.0f;
        
        // Right Joy-Con
        vibration_values[1].amp_low = rumble_intensity_right;
        vibration_values[1].freq_low = 160.0f;
        vibration_values[1].amp_high = rumble_intensity_right * 0.8f;
        vibration_values[1].freq_high = 320.0f;
        
        hidSendVibrationValues(vibration_handles, vibration_values, 2);
    } else {
        // Stop rumble when timer expires
        HidVibrationValue stop_values[2] = {0};
        hidSendVibrationValues(vibration_handles, stop_values, 2);
    }
}

// Initialize input
static void init_input(void) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    
    // Initialize HD rumble
    init_hd_rumble();
}

// Platform API implementation
void platform_exit(void) {
    wants_to_exit = true;
}

// Docked/handheld resolution handling
static vec2i_t current_screen_size = {1280, 720};
static AppletOperationMode last_operation_mode = AppletOperationMode_Handheld;
static bool resolution_changed = false;
static double dock_check_timer = 0.0;
static const double DOCK_CHECK_INTERVAL = 1.0; // Check once per second

static void configure_resolution(void) {
    AppletOperationMode operation_mode = appletGetOperationMode();
    
    if (operation_mode == AppletOperationMode_Console) {
        current_screen_size = (vec2i_t){1920, 1080}; // Full HD when docked
    } else {
        current_screen_size = (vec2i_t){1280, 720};  // HD when handheld
    }
    
    // Set proper window crop for the resolution
    nwindowSetCrop(nwindowGetDefault(), 0, 0, current_screen_size.x, current_screen_size.y);
    
    printf("Resolution configured: %dx%d (%s mode)\n", 
           current_screen_size.x, current_screen_size.y,
           operation_mode == AppletOperationMode_Console ? "docked" : "handheld");
}

static void check_dock_state(void) {
    AppletOperationMode operation_mode = appletGetOperationMode();
    
    // Check if dock state changed
    if (operation_mode != last_operation_mode) {
        last_operation_mode = operation_mode;
        configure_resolution();
        resolution_changed = true;
    }
}

vec2i_t platform_screen_size(void) {
    return current_screen_size;
}

double platform_now(void) {
    return (double)armGetSystemTick() / (double)armGetSystemTickFreq();
}

bool platform_get_fullscreen(void) {
    return true; // Always fullscreen on Switch
}

void platform_set_fullscreen(bool fullscreen) {
    // No-op on Switch
}

void platform_set_audio_mix_cb(void (*cb)(float *buffer, uint32_t len)) {
    audio_callback = cb;
}

void platform_rumble_impact(float intensity, bool left_side) {
    if (!hd_rumble_enabled) return;
    
    // Clamp intensity
    if (intensity > 1.0f) intensity = 1.0f;
    if (intensity < 0.0f) intensity = 0.0f;
    
    // Set rumble for specified side
    if (left_side) {
        rumble_intensity_left = intensity;
        rumble_intensity_right = intensity * 0.3f; // Light rumble on other side
    } else {
        rumble_intensity_right = intensity;
        rumble_intensity_left = intensity * 0.3f; // Light rumble on other side
    }
    
    // Reset fade timer
    rumble_fade_timer = RUMBLE_FADE_TIME;
}

void platform_rumble_strong_impact(float intensity) {
    if (!hd_rumble_enabled) return;
    
    // Clamp intensity
    if (intensity > 1.0f) intensity = 1.0f;
    if (intensity < 0.0f) intensity = 0.0f;
    
    // Set strong rumble on both sides
    rumble_intensity_left = intensity;
    rumble_intensity_right = intensity;
    
    // Reset fade timer
    rumble_fade_timer = RUMBLE_FADE_TIME;
}

FILE *platform_open_asset(const char *name, const char *mode) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", path_assets, name);
    return fopen(path, mode);
}

uint8_t *platform_load_asset(const char *name, uint32_t *bytes_read) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", path_assets, name);
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (bytes_read) *bytes_read = 0;
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *buffer = mem_temp_alloc(size);
    if (!buffer) {
        fclose(f);
        if (bytes_read) *bytes_read = 0;
        return NULL;
    }
    
    uint32_t read = fread(buffer, 1, size, f);
    fclose(f);
    
    if (bytes_read) *bytes_read = read;
    return buffer;
}

uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", path_userdata, name);
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (bytes_read) *bytes_read = 0;
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *buffer = mem_temp_alloc(size);
    if (!buffer) {
        fclose(f);
        if (bytes_read) *bytes_read = 0;
        return NULL;
    }
    
    uint32_t read = fread(buffer, 1, size, f);
    fclose(f);
    
    if (bytes_read) *bytes_read = read;
    return buffer;
}

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", path_userdata, name);
    
    // Create directory if it doesn't exist
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path_userdata);
    mkdir(dir, 0777);
    
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    
    uint32_t written = fwrite(bytes, 1, len, f);
    fclose(f);
    
    return written;
}

// Process input
static void process_input(void) {
    padUpdate(&pad);
    u64 buttons_held = padGetButtons(&pad);
    
    // Digital buttons - check each button directly
    input_set_button_state(INPUT_GAMEPAD_A, (buttons_held & HidNpadButton_A) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_B, (buttons_held & HidNpadButton_B) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_X, (buttons_held & HidNpadButton_X) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_Y, (buttons_held & HidNpadButton_Y) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_L_SHOULDER, (buttons_held & HidNpadButton_L) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_R_SHOULDER, (buttons_held & HidNpadButton_R) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_L_TRIGGER, (buttons_held & HidNpadButton_ZL) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_R_TRIGGER, (buttons_held & HidNpadButton_ZR) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_START, (buttons_held & HidNpadButton_Plus) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_SELECT, (buttons_held & HidNpadButton_Minus) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_L_STICK_PRESS, (buttons_held & HidNpadButton_StickL) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_R_STICK_PRESS, (buttons_held & HidNpadButton_StickR) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_DPAD_UP, (buttons_held & HidNpadButton_Up) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_DPAD_DOWN, (buttons_held & HidNpadButton_Down) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_DPAD_LEFT, (buttons_held & HidNpadButton_Left) ? 1.0f : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_DPAD_RIGHT, (buttons_held & HidNpadButton_Right) ? 1.0f : 0.0f);
    
    // Check for quit combination (Start + Select)
    bool start_held = (buttons_held & HidNpadButton_Plus) != 0;
    bool select_held = (buttons_held & HidNpadButton_Minus) != 0;
    
    if (start_held && select_held) {
        if (!quit_buttons_held) {
            // Just started holding both buttons
            quit_buttons_held = true;
            quit_button_hold_time = 0.0;
        }
    } else {
        // Released one or both buttons
        quit_buttons_held = false;
        quit_button_hold_time = 0.0;
    }
    
    // Analog sticks
    HidAnalogStickState left_stick = padGetStickPos(&pad, 0);
    HidAnalogStickState right_stick = padGetStickPos(&pad, 1);
    
    // Left stick
    float left_x = (float)left_stick.x / 32767.0f;
    float left_y = (float)left_stick.y / 32767.0f;
    
    input_set_button_state(INPUT_GAMEPAD_L_STICK_LEFT, left_x < -0.3f ? -left_x : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_L_STICK_RIGHT, left_x > 0.3f ? left_x : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_L_STICK_UP, left_y > 0.3f ? left_y : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_L_STICK_DOWN, left_y < -0.3f ? -left_y : 0.0f);
    
    // Right stick
    float right_x = (float)right_stick.x / 32767.0f;
    float right_y = (float)right_stick.y / 32767.0f;
    
    input_set_button_state(INPUT_GAMEPAD_R_STICK_LEFT, right_x < -0.3f ? -right_x : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_R_STICK_RIGHT, right_x > 0.3f ? right_x : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_R_STICK_UP, right_y > 0.3f ? right_y : 0.0f);
    input_set_button_state(INPUT_GAMEPAD_R_STICK_DOWN, right_y < -0.3f ? -right_y : 0.0f);
    
    // Handle touchscreen
    if (hidGetTouchScreenStates(&touch_state, 1)) {
        if (touch_state.count > 0) {
            // Convert touch to screen coordinates
            float touch_x = (float)touch_state.touches[0].x / 1280.0f;
            float touch_y = (float)touch_state.touches[0].y / 720.0f;
            
            // Map touch to input if needed
            // This could be used for menu navigation
        }
    }
}

// Main loop
int main(int argc, char *argv[]) {
    printf("Wipeout Rewrite - Nintendo Switch Port\n");
    
    // Initialize system services
    if (!init_egl()) {
        printf("Failed to initialize EGL\n");
        return 1;
    }
    
    if (!init_audio()) {
        printf("Failed to initialize audio\n");
        return 1;
    }
    
    init_input();
    
    // Initialize game systems
    system_init();
    
    // Main game loop
    while (!wants_to_exit && appletMainLoop()) {
        // Process input
        process_input();
        
        // Update audio (RetroArch style)
        update_audio();
        
        // Update game
        double current_time = platform_now();
        static double last_time = 0;
        if (last_time == 0) last_time = current_time;
        double delta_time = current_time - last_time;
        last_time = current_time;
        
        system_update(delta_time);
        
        // Check for dock state changes (once per second)
        dock_check_timer += delta_time;
        if (dock_check_timer >= DOCK_CHECK_INTERVAL) {
            dock_check_timer = 0.0;
            check_dock_state();
        }
        
        // Handle resolution changes
        if (resolution_changed) {
            vec2i_t screen_size = platform_screen_size();
            system_resize(screen_size);
            resolution_changed = false;
        }
        
        // Update HD rumble
        update_hd_rumble(delta_time);
        
        // Update quit button timer
        if (quit_buttons_held) {
            quit_button_hold_time += delta_time;
            if (quit_button_hold_time >= QUIT_HOLD_TIME) {
                printf("Quit requested (Start+Select held for %.1fs)\n", QUIT_HOLD_TIME);
                wants_to_exit = true;
            }
        }
        
        // Swap buffers
        eglSwapBuffers(egl_display, egl_surface);
        
    }
    
    // Cleanup
    system_cleanup();
    
    // Audio cleanup
    audoutStopAudioOut();
    audoutExit();
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (audio_buffer_data[i]) {
            free(audio_buffer_data[i]);
        }
    }
    
    // EGL cleanup
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
    
    return 0;
}