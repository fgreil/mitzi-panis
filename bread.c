/*
 * Panis - The Grumpy Bread Jump'n'Run Game
 * A simple side-scrolling platformer for Flipper Zero
 * 
 * Controls:
 *   Left/Right      - Move character
 *   Up (press)      - Small jump
 *   Up (hold)       - Big jump
 *   Back (hold)     - Exit game
 */

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#include "panis_icons.h"

// Logging tag for debugging
#define TAG "Panis"

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
#define PANIS_WIDTH 10
#define PANIS_HEIGHT 10

// Character positioning
#define GROUND_Y (SCREEN_HEIGHT - 8)     // Y position where character walks
#define SCROLL_START_X 64                // X position where background starts scrolling

// Physics constants
#define GRAVITY 2                        // Downward acceleration per frame
#define JUMP_VELOCITY_SHORT 8            // Initial upward velocity for short jump (~25px)
#define JUMP_VELOCITY_LONG 12            // Initial upward velocity for long jump (~50px)
#define MOVE_SPEED 2                     // Horizontal movement speed in pixels per frame

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
} GameState;

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
    FURI_LOG_I(TAG, "Initializing game state");
    
    state->x = 10.0f;                    // Start character 10px from left edge
    state->y = GROUND_Y;                 // Place character on ground
    state->velocity_y = 0.0f;            // No vertical movement initially
    state->map_offset_x = 0;             // Background starts at left edge
    state->direction = DirectionRight;   // Character faces right initially
    state->jump_state = JumpStateNone;   // Character is on ground
    state->jump_button_held = false;     // Jump button not pressed
    state->jump_start_tick = 0;          // No jump in progress
    
    FURI_LOG_I(TAG, "Game state initialized");
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
 * @param type Type of input (Press, Repeat, etc.) - unused but kept for consistency
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
    UNUSED(type);  // Parameter kept for API consistency but not used in function
    
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
 * 1. Scrolling background (map_01.png) - TEMPORARILY DISABLED TO TEST
 * 2. Character sprite (bread_l.png or bread_r.png based on direction)
 * 3. Debug information
 */
static void draw_callback(Canvas* canvas, void* ctx) {
    static uint32_t draw_count = 0;
    draw_count++;
    
    if(draw_count == 1) {
        FURI_LOG_I(TAG, "draw_callback called for first time");
    }
    
    GameState* state = ctx;
    
    // Safety check
    if(state == NULL) {
        FURI_LOG_E(TAG, "draw_callback: NULL state!");
        return;
    }
    
    if(canvas == NULL) {
        FURI_LOG_E(TAG, "draw_callback: NULL canvas!");
        return;
    }
    
    // Clear previous frame
    canvas_clear(canvas);
    
    /* TEMPORARILY DISABLED - Background hangs due to large size (748px wide)
    // Draw background with clipping
    // The map is very wide (748px), so we use canvas clipping to only draw visible portion
    if(draw_count == 1) {
        FURI_LOG_I(TAG, "Setting up canvas clip for background");
    }
    
    // Set clipping region to screen boundaries
    canvas_set_clip(canvas, 0, MAP_Y, SCREEN_WIDTH, MAP_HEIGHT);
    
    int draw_x = -state->map_offset_x;
    
    if(draw_count == 1) {
        FURI_LOG_I(TAG, "Drawing background at x=%d with clip enabled", draw_x);
    }
    
    canvas_draw_icon(canvas, draw_x, MAP_Y, &I_map_01);
    
    if(draw_count == 1) {
        FURI_LOG_I(TAG, "Background drawn, resetting clip");
    }
    
    // Reset clipping to full screen
    canvas_reset_clip(canvas);
    */
    
    // Draw a simple ground line instead of background
    canvas_draw_line(canvas, 0, GROUND_Y, SCREEN_WIDTH, GROUND_Y);
    
    // Draw character sprite
    if(draw_count == 1) {
        FURI_LOG_I(TAG, "Drawing character sprite");
    }
    
    const Icon* sprite = (state->direction == DirectionLeft) ? &I_bread_l : &I_bread_r;
    canvas_draw_icon(canvas, (int)state->x, (int)state->y - PANIS_HEIGHT, sprite);
    
    if(draw_count == 1) {
        FURI_LOG_I(TAG, "Character sprite drawn successfully");
    }
    
    // Debug text overlay
    char debug[64];
    snprintf(debug, sizeof(debug), "X:%.0f Y:%.0f Off:%d", (double)state->x, (double)state->y, state->map_offset_x);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 8, debug);
    
    canvas_draw_str(canvas, 0, 18, "Use arrows to move!");
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
 * 3. Run main game loop (process input, update physics, render)
 * 4. Clean up and exit
 */
int32_t panis_main(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "=== Panis Game Starting ===");
    
    /* ------------------------------------------------------------------------
     * INITIALIZATION
     * ---------------------------------------------------------------------- */
    
    FURI_LOG_I(TAG, "Allocating game state");
    // Allocate and initialize game state
    GameState* game_state = malloc(sizeof(GameState));
    if(game_state == NULL) {
        FURI_LOG_E(TAG, "Failed to allocate game state!");
        return -1;
    }
    
    game_state_init(game_state);
    
    FURI_LOG_I(TAG, "Creating event queue");
    // Create message queue for input events
    // Queue holds up to 8 events, each event is sizeof(InputEvent)
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    if(event_queue == NULL) {
        FURI_LOG_E(TAG, "Failed to allocate event queue!");
        free(game_state);
        return -1;
    }
    
    FURI_LOG_I(TAG, "Creating viewport");
    // Create viewport for rendering
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, game_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);
    
    FURI_LOG_I(TAG, "Opening GUI");
    // Register viewport with GUI system
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    FURI_LOG_I(TAG, "Initialization complete, entering main loop");
    
    /* ------------------------------------------------------------------------
     * MAIN GAME LOOP
     * ---------------------------------------------------------------------- */
    
    InputEvent event;
    bool running = true;
    uint32_t frame_count = 0;
    
    FURI_LOG_I(TAG, "Entering while loop");
    
    while(running) {
        if(frame_count == 0) {
            FURI_LOG_I(TAG, "First iteration of main loop");
        }
        
        // Process input events with 16ms timeout (approximately 60 FPS)
        // This ensures the game continues updating even without input
        FuriStatus queue_status = furi_message_queue_get(event_queue, &event, 16);
        
        if(frame_count == 0) {
            FURI_LOG_I(TAG, "furi_message_queue_get returned, status=%d", queue_status);
        }
        
        if(queue_status == FuriStatusOk) {
            
            FURI_LOG_D(TAG, "Input: key=%d type=%d", event.key, event.type);
            
            // Back button - exit game (LONG PRESS)
            if(event.key == InputKeyBack) {
                if(event.type == InputTypeLong) {
                    FURI_LOG_I(TAG, "Exit requested");
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
                FURI_LOG_D(TAG, "Jump triggered");
                // Handle all input types (Press, Long, Release) for variable jump height
                handle_jump(game_state, event.type);
            }
        }
        
        // Update game physics (gravity, collisions, etc.)
        game_update(game_state);
        
        if(frame_count == 0) {
            FURI_LOG_I(TAG, "game_update completed");
        }
        
        // Request screen redraw with updated game state
        if(frame_count == 0) {
            FURI_LOG_I(TAG, "About to call first view_port_update");
        }
        view_port_update(view_port);
        if(frame_count == 0) {
            FURI_LOG_I(TAG, "First view_port_update completed");
        }
        
        // Log every 60 frames (approximately every second at 60 FPS)
        frame_count++;
        if(frame_count == 1) {
            FURI_LOG_I(TAG, "First frame completed, frame_count=%lu", frame_count);
        }
        if(frame_count % 60 == 0) {
            FURI_LOG_I(TAG, "Running: frame=%lu x=%.1f y=%.1f offset=%d", 
                       frame_count, (double)game_state->x, (double)game_state->y, game_state->map_offset_x);
        }
    }
    
    FURI_LOG_I(TAG, "Main loop exited");
    
    /* ------------------------------------------------------------------------
     * CLEANUP
     * ---------------------------------------------------------------------- */
    
    FURI_LOG_I(TAG, "Starting cleanup");
    
    // Remove viewport from GUI
    FURI_LOG_I(TAG, "Removing viewport");
    gui_remove_view_port(gui, view_port);
    
    // Free viewport
    view_port_free(view_port);
    
    // Free message queue
    furi_message_queue_free(event_queue);
    
    // Close GUI record
    furi_record_close(RECORD_GUI);
    
    // Free game state
    free(game_state);
    
    FURI_LOG_I(TAG, "=== Panis Game Exited Successfully ===");
    return 0; // Success
}