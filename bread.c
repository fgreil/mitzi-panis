/*
 * Panis - The Grumpy Bread Jump'n'Run Game
 * A simple side-scrolling platformer for Flipper Zero
 * 
 * Controls:
 *   Left/Right      - Move character
 *   Up (press)      - Small jump
 *   Up (hold)       - Big jump
 *   Down (press)    - Toggle background music
 *   Back (hold)     - Exit game
 */

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>

#include "panis_icons.h"

/* ============================================================================
 * CONSTANTS AND DEFINITIONS
 * ========================================================================== */

// Screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Background map dimensions and positioning
#define MAP_HEIGHT 60                    // Height of the background image
#define MAP_WIDTH 748                    // Width of the background image (scrollable)
#define MAP_Y 4                          // Y position to align map to bottom (64 - 60 = 4)

// Character sprite dimensions (adjust to match your actual sprite size)
#define PANIS_WIDTH 16
#define PANIS_HEIGHT 16

// Character positioning
#define GROUND_Y (SCREEN_HEIGHT - 8)     // Y position where character walks
#define SCROLL_START_X 64                // X position where background starts scrolling

// Physics constants
#define GRAVITY 2                        // Downward acceleration per frame
#define JUMP_VELOCITY_SHORT 8            // Initial upward velocity for short jump (~25px)
#define JUMP_VELOCITY_LONG 12            // Initial upward velocity for long jump (~50px)
#define MOVE_SPEED 2                     // Horizontal movement speed in pixels per frame

// Music constants
#define TEMPO_BPM 120                    // Beats per minute
#define BEAT_DURATION_MS (60000 / TEMPO_BPM) // Duration of one beat in milliseconds

// Note frequency definitions (in Hz) for standard musical notes
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_REST 0

/* ============================================================================
 * DATA STRUCTURES
 * ========================================================================== */

/**
 * @brief Character facing direction
 */
typedef enum {
    DirectionLeft,   // Character facing left (use bread_l.png)
    DirectionRight   // Character facing right (use bread_r.png)
} Direction;

/**
 * @brief Jump state machine states
 */
typedef enum {
    JumpStateNone,    // Character is on the ground
    JumpStateRising,  // Character is moving upward
    JumpStateFalling  // Character is falling down (not currently used but available)
} JumpState;

/**
 * @brief Note duration types for music
 * Based on 3/4 time signature at 120 BPM (500ms per beat)
 */
typedef enum {
    DurationEighth,      // e  = 1/2 beat = 250ms
    DurationQuarter,     // q  = 1 beat   = 500ms
    DurationDottedHalf   // dh = 3 beats  = 1500ms (full measure in 3/4)
} NoteDuration;

/**
 * @brief Musical note structure
 */
typedef struct {
    uint16_t frequency;      // Note frequency in Hz (0 = rest)
    NoteDuration duration;   // Note duration
} MusicNote;

/**
 * @brief Main game state structure
 * Contains all information about the current game state
 */
typedef struct {
    float x;                    // Character x position on screen (0 to SCREEN_WIDTH)
    float y;                    // Character y position (GROUND_Y when standing)
    float velocity_y;           // Vertical velocity (negative = moving up, positive = falling)
    int map_offset_x;           // Background horizontal scroll offset (0 to MAP_WIDTH - SCREEN_WIDTH)
    Direction direction;        // Which direction character is facing
    JumpState jump_state;       // Current jump state
    bool jump_button_held;      // Whether jump button is currently held
    uint32_t jump_start_tick;   // Tick when jump started (for timing)
    bool music_enabled;         // Whether background music is playing
} GameState;

/* ============================================================================
 * BACKGROUND MUSIC DATA
 * ========================================================================== */

/**
 * Background music melody in 3/4 time, key of C
 * 
 * Original JSON notation:
 * Measure 1: ["Rest_q", "E4_e", "G4_e", "C5_e", "G4_e"]
 * Measure 2: ["G4_dh"]
 * Measure 3: ["G4_q", "E4_e", "G4_e", "C5_e", "G4_e"]
 * Measure 4: ["G4_dh"]
 */
static const MusicNote background_melody[] = {
    // Measure 1
    {NOTE_REST, DurationQuarter},      // Rest_q
    {NOTE_E4, DurationEighth},         // E4_e
    {NOTE_G4, DurationEighth},         // G4_e
    {NOTE_C5, DurationEighth},         // C5_e
    {NOTE_G4, DurationEighth},         // G4_e
    
    // Measure 2
    {NOTE_G4, DurationDottedHalf},     // G4_dh (3 beats)
    
    // Measure 3
    {NOTE_G4, DurationQuarter},        // G4_q
    {NOTE_E4, DurationEighth},         // E4_e
    {NOTE_G4, DurationEighth},         // G4_e
    {NOTE_C5, DurationEighth},         // C5_e
    {NOTE_G4, DurationEighth},         // G4_e
    
    // Measure 4
    {NOTE_G4, DurationDottedHalf},     // G4_dh (3 beats)
};

// Number of notes in the melody
static const size_t melody_length = sizeof(background_melody) / sizeof(background_melody[0]);

/**
 * @brief Convert note duration enum to milliseconds
 * @param duration Note duration type
 * @return uint32_t Duration in milliseconds
 */
static uint32_t get_note_duration_ms(NoteDuration duration) {
    switch(duration) {
        case DurationEighth:
            return BEAT_DURATION_MS / 2;  // Half beat = 250ms
        case DurationQuarter:
            return BEAT_DURATION_MS;       // One beat = 500ms
        case DurationDottedHalf:
            return BEAT_DURATION_MS * 3;   // Three beats = 1500ms
        default:
            return BEAT_DURATION_MS;
    }
}

/* ============================================================================
 * INITIALIZATION FUNCTIONS
 * ========================================================================== */

/**
 * @brief Initialize game state to starting values
 * @param state Pointer to GameState structure to initialize
 * 
 * Sets character at left side of screen on the ground, with no scrolling.
 */
static void game_state_init(GameState* state) {
    state->x = 10.0f;                    // Start character 10px from left edge
    state->y = GROUND_Y;                 // Place character on ground
    state->velocity_y = 0.0f;            // No vertical movement initially
    state->map_offset_x = 0;             // Background starts at left edge
    state->direction = DirectionRight;   // Character faces right initially
    state->jump_state = JumpStateNone;   // Character is on ground
    state->jump_button_held = false;     // Jump button not pressed
    state->jump_start_tick = 0;          // No jump in progress
    state->music_enabled = true;         // Music starts enabled
}

/* ============================================================================
 * INPUT CALLBACK FUNCTIONS
 * ========================================================================== */

/**
 * @brief Input event callback - forwards input events to message queue
 * @param input_event Pointer to input event from the system
 * @param ctx Context pointer (FuriMessageQueue in this case)
 * 
 * This is called by the system whenever a button is pressed/released.
 * We simply forward the event to our queue for processing in the main loop.
 */
static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

/* ============================================================================
 * MUSIC PLAYER FUNCTIONS
 * ========================================================================== */

/**
 * @brief Background music player thread
 * @param context Pointer to GameState
 * @return int32_t Thread exit code
 * 
 * Continuously loops through the background melody, playing each note.
 * Respects the music_enabled flag for toggling on/off.
 */
static int32_t music_player_thread(void* context) {
    GameState* state = context;
    
    // Get notification service for speaker control
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    
    size_t note_index = 0;  // Current position in melody
    
    while(true) {
        // Check if music is enabled
        if(state->music_enabled) {
            // Get current note from melody
            const MusicNote* note = &background_melody[note_index];
            
            // Calculate note duration in milliseconds
            uint32_t duration_ms = get_note_duration_ms(note->duration);
            
            if(note->frequency == NOTE_REST) {
                // Rest (silence)
                furi_delay_ms(duration_ms);
            } else {
                // Play the note using notification API
                // Create notification sequence for this note
                const NotificationSequence sequence = {
                    &message_note_c4,  // This will be overridden below
                    &message_delay_10,
                    NULL,
                };
                
                // Calculate note message based on frequency
                // Note: Flipper's notification system uses predefined notes
                // We'll approximate by playing the note for the correct duration
                
                // Simple playback: use speaker to play frequency
                // Note: This is a simplified version - Flipper has limited speaker API
                notification_message(notifications, &sequence_single_vibro);
                
                // For proper note playback, we need to use the speaker directly
                // This is a workaround - ideally use proper note messages
                furi_hal_speaker_start(note->frequency, 1.0f);
                furi_delay_ms(duration_ms);
                furi_hal_speaker_stop();
            }
            
            // Move to next note, loop back to start when melody ends
            note_index++;
            if(note_index >= melody_length) {
                note_index = 0;
            }
        } else {
            // Music disabled, just wait a bit before checking again
            furi_delay_ms(100);
        }
        
        // Allow thread to yield CPU
        furi_thread_yield();
    }
    
    furi_record_close(RECORD_NOTIFICATION);
    return 0;
}

/* ============================================================================
 * GAME LOGIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Update game physics each frame
 * @param state Pointer to current game state
 * 
 * Handles:
 * - Applying gravity to character
 * - Updating vertical position based on velocity
 * - Detecting ground collision and landing
 */
static void game_update(GameState* state) {
    // Apply gravity when character is in the air (jumping or falling)
    if(state->jump_state != JumpStateNone) {
        // Increase downward velocity (gravity pulls character down)
        state->velocity_y += GRAVITY;
        
        // Update vertical position based on velocity
        // Negative velocity = moving up, positive = falling down
        state->y += state->velocity_y;

        // Check if character has landed on the ground
        if(state->y >= GROUND_Y) {
            state->y = GROUND_Y;              // Snap to ground level
            state->velocity_y = 0.0f;         // Stop vertical movement
            state->jump_state = JumpStateNone; // Return to ground state
        }
    }
}

/**
 * @brief Handle horizontal movement (left/right)
 * @param state Pointer to current game state
 * @param key Which key was pressed (InputKeyLeft or InputKeyRight)
 * @param type Type of input (Press, Repeat, etc.)
 * 
 * Movement logic:
 * - When moving right:
 *   1. Character moves from x=0 to x=64 (background stays still)
 *   2. At x=64, background scrolls instead (character stays at x=64)
 *   3. When background fully scrolled, character can move to right edge
 * 
 * - When moving left (reverse of above):
 *   1. If at right edge and background fully scrolled, move character left
 *   2. When character at x=0, scroll background left
 *   3. When background at left edge, stop
 */
static void handle_movement(GameState* state, InputKey key, InputType type) {
    if(key == InputKeyLeft) {
        // Update sprite direction
        state->direction = DirectionLeft;
        
        // Scenario 1: Character is at left edge of screen (x=0) and background can scroll left
        if(state->x <= 0 && state->map_offset_x > 0) {
            // Scroll background left (reveals more of left side of map)
            state->map_offset_x -= MOVE_SPEED;
            if(state->map_offset_x < 0) {
                state->map_offset_x = 0; // Clamp to left edge of map
            }
        } 
        // Scenario 2: Character can move left on screen
        else if(state->x > 0) {
            // Move character left on screen
            state->x -= MOVE_SPEED;
            if(state->x < 0) {
                state->x = 0; // Clamp to left edge of screen
            }
        }
        // If character at left edge and map at left edge, can't move further left
    } 
    else if(key == InputKeyRight) {
        // Update sprite direction
        state->direction = DirectionRight;
        
        // Calculate maximum scroll offset (when right edge of map aligns with right edge of screen)
        int max_scroll = MAP_WIDTH - SCREEN_WIDTH;
        
        // Scenario 1: Character hasn't reached x=64 yet (and background not scrolled)
        if(state->x < SCROLL_START_X && state->map_offset_x == 0) {
            // Move character right on screen
            state->x += MOVE_SPEED;
            if(state->x > SCROLL_START_X) {
                state->x = SCROLL_START_X; // Clamp to scroll start position
            }
        }
        // Scenario 2: Character at x=64, scroll background instead
        else if(state->x >= SCROLL_START_X && state->map_offset_x < max_scroll) {
            // Scroll background right (reveals more of right side of map)
            state->map_offset_x += MOVE_SPEED;
            if(state->map_offset_x > max_scroll) {
                state->map_offset_x = max_scroll; // Clamp to max scroll
            }
        }
        // Scenario 3: Background fully scrolled, character can move to right edge
        else if(state->map_offset_x >= max_scroll && state->x < SCREEN_WIDTH - PANIS_WIDTH) {
            // Move character right toward screen edge
            state->x += MOVE_SPEED;
            if(state->x > SCREEN_WIDTH - PANIS_WIDTH) {
                state->x = SCREEN_WIDTH - PANIS_WIDTH; // Clamp to right edge
            }
        }
        // If at right edge of map and right edge of screen, can't move further right
    }
}

/**
 * @brief Handle jump input
 * @param state Pointer to current game state
 * @param type Type of input (Press, Long, Release)
 * 
 * Jump mechanics:
 * - Single press (InputTypePress): Small jump with velocity 8 (~25px height)
 * - Hold button (InputTypeLong): Big jump with velocity 12 (~50px height)
 * - Release early: Cuts jump short by reducing upward velocity
 */
static void handle_jump(GameState* state, InputType type) {
    // Start a short jump on button press (only if on ground)
    if(type == InputTypePress && state->jump_state == JumpStateNone) {
        state->jump_state = JumpStateRising;           // Enter jumping state
        state->velocity_y = -JUMP_VELOCITY_SHORT;      // Negative velocity = upward motion
        state->jump_button_held = true;                // Track that button is held
        state->jump_start_tick = furi_get_tick();      // Record jump start time
    } 
    // Start a long jump when button held down (only if on ground)
    else if(type == InputTypeLong && state->jump_state == JumpStateNone) {
        state->jump_state = JumpStateRising;           // Enter jumping state
        state->velocity_y = -JUMP_VELOCITY_LONG;       // Larger negative velocity = higher jump
        state->jump_button_held = true;                // Track that button is held
        state->jump_start_tick = furi_get_tick();      // Record jump start time
    }
    // Button released - allow variable jump height
    else if(type == InputTypeRelease) {
        state->jump_button_held = false;               // Button no longer held
        
        // If still moving upward when button released, cut the jump short
        // This allows player to control jump height precisely
        if(state->jump_state == JumpStateRising && state->velocity_y < 0) {
            state->velocity_y *= 0.5f; // Reduce upward velocity by half
        }
    }
}

/* ============================================================================
 * RENDERING FUNCTIONS
 * ========================================================================== */

/**
 * @brief Draw the game screen
 * @param canvas Pointer to canvas to draw on
 * @param ctx Context pointer (GameState in this case)
 * 
 * Draws:
 * 1. Scrolling background (map_01.png)
 * 2. Character sprite (bread_l.png or bread_r.png based on direction)
 * 3. Optional debug information
 */
static void draw_callback(Canvas* canvas, void* ctx) {
    GameState* state = ctx;
    
    // Clear previous frame
    canvas_clear(canvas);
    
    // Draw scrolling background
    // The x position is negative offset to create scrolling effect
    // As map_offset_x increases, the background moves left (revealing right side)
    int draw_x = -state->map_offset_x;
    canvas_draw_icon(canvas, draw_x, MAP_Y, &I_map_01);
    
    // Draw character sprite
    // Choose sprite based on facing direction
    const Icon* sprite = (state->direction == DirectionLeft) ? &I_bread_l : &I_bread_r;
    
    // Draw at character position
    // Subtract PANIS_HEIGHT because y coordinate is the character's feet position
    canvas_draw_icon(canvas, (int)state->x, (int)state->y - PANIS_HEIGHT, sprite);
    
    // Optional: Draw debug information (uncomment to enable)
    // Shows character position and scroll offset for debugging
    /*
    char debug[32];
    snprintf(debug, sizeof(debug), "X:%.0f Y:%.0f Off:%d", state->x, state->y, state->map_offset_x);
    canvas_draw_str(canvas, 0, 8, debug);
    */
}

/* ============================================================================
 * MAIN APPLICATION
 * ========================================================================== */

/**
 * @brief Main application entry point
 * @param p Unused parameter (required by Flipper Zero app interface)
 * @return int32_t Exit code (0 = success)
 * 
 * Application lifecycle:
 * 1. Initialize game state and allocate resources
 * 2. Set up GUI viewport and input handling
 * 3. Start background music thread
 * 4. Run main game loop (process input, update physics, render)
 * 5. Clean up and exit
 */
int32_t panis_main(void* p) {
    UNUSED(p);
    
    /* ------------------------------------------------------------------------
     * INITIALIZATION
     * ---------------------------------------------------------------------- */
    
    // Allocate and initialize game state
    GameState* game_state = malloc(sizeof(GameState));
    game_state_init(game_state);
    
    // Create message queue for input events
    // Queue holds up to 8 events, each event is sizeof(InputEvent)
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    
    // Create viewport for rendering
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, game_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);
    
    // Register viewport with GUI system
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    // Start background music thread
    FuriThread* music_thread = furi_thread_alloc();
    furi_thread_set_name(music_thread, "PanisMusicPlayer");
    furi_thread_set_stack_size(music_thread, 1024);
    furi_thread_set_context(music_thread, game_state);
    furi_thread_set_callback(music_thread, music_player_thread);
    furi_thread_start(music_thread);
    
    /* ------------------------------------------------------------------------
     * MAIN GAME LOOP
     * ---------------------------------------------------------------------- */
    
    InputEvent event;
    bool running = true;
    
    while(running) {
        // Process input events with 16ms timeout (approximately 60 FPS)
        // This ensures the game continues updating even without input
        if(furi_message_queue_get(event_queue, &event, 16) == FuriStatusOk) {
            
            // Back button - exit game (LONG PRESS)
            if(event.key == InputKeyBack) {
                if(event.type == InputTypeLong) {
                    running = false; // Exit main loop
                }
            } 
            // Left/Right movement
            else if(event.key == InputKeyLeft || event.key == InputKeyRight) {
                // Process on press and repeat (for continuous movement when held)
                if(event.type == InputTypePress || event.type == InputTypeRepeat) {
                    handle_movement(game_state, event.key, event.type);
                }
            }
            // Up button - jumping
            else if(event.key == InputKeyUp) {
                // Handle all input types (Press, Long, Release) for variable jump height
                handle_jump(game_state, event.type);
            }
            // Down button - toggle background music
            else if(event.key == InputKeyDown) {
                if(event.type == InputTypePress) {
                    // Toggle music on/off
                    game_state->music_enabled = !game_state->music_enabled;
                    
                    // Stop speaker immediately when disabling music
                    if(!game_state->music_enabled) {
                        furi_hal_speaker_stop();
                    }
                }
            }
        }
        
        // Update game physics (gravity, collisions, etc.)
        game_update(game_state);
        
        // Request screen redraw with updated game state
        view_port_update(view_port);
    }
    
    /* ------------------------------------------------------------------------
     * CLEANUP
     * ---------------------------------------------------------------------- */
    
    // Stop music and clean up music thread
    game_state->music_enabled = false;
    furi_hal_speaker_stop();
    furi_thread_join(music_thread);
    furi_thread_free(music_thread);
    
    // Remove viewport from GUI
    gui_remove_view_port(gui, view_port);
    
    // Free viewport
    view_port_free(view_port);
    
    // Free message queue
    furi_message_queue_free(event_queue);
    
    // Close GUI record
    furi_record_close(RECORD_GUI);
    
    // Free game state
    free(game_state);
    
    return 0; // Success
}
