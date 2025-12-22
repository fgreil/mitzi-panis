#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>

// Include generated icon assets
#include "panis_icons.h"

// Screen dimensions for Flipper Zero
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// PANIS configuration
#define MOVEMENT_SPEED 4  // Pixels per frame
#define GROUND_Y 59  // Y position of the ground line

// Jump physics
#define GRAVITY 2
#define SMALL_JUMP_VELOCITY -10
#define BIG_JUMP_VELOCITY -14
#define MAX_FALL_SPEED 10
#define JUMP_HEIGHT_THRESHOLD 25  // Height to distinguish small/big jump
#define DOUBLE_CLICK_MS 300  // Time window for double-click (milliseconds)

// Map tile configuration
#define TILE_WIDTH 128
#define NUM_TILES 3  // map_tile_0, map_tile_1, map_tile_2
#define TOTAL_MAP_WIDTH (TILE_WIDTH * NUM_TILES)

// Character dimensions
#define CHAR_WIDTH 10
#define CHAR_HEIGHT 10

// Scrolling thresholds
#define START_SCROLL_X (SCREEN_WIDTH / 2)  // Start scrolling at 1/2 screen (64px)
#define CHAR_START_X (SCREEN_WIDTH / 4)    // Character starts at 1/4 screen (32px)

// Game state structure
typedef struct {
    int world_x;           // Character's X position in the world (0 to TOTAL_MAP_WIDTH)
    int screen_x;          // Character's X position on screen
    int camera_x;          // Camera offset (how much the world is scrolled)
    bool facing_right;     // True if facing right, false if facing left
    bool running;          // Game loop control
    int y_pos;             // Character Y position (0 = top)
    int y_velocity;        // Vertical velocity
    bool on_ground;        // True if character is on ground
	uint32_t last_jump_time;  // Time of last jump press
	NotificationApp* notifications;  // For vibration feedback
} GameState;

// Draw callback function
static void draw_callback(Canvas* canvas, void* ctx) {
    GameState* state = (GameState*)ctx;
    canvas_clear(canvas);

    // Calculate which tiles are visible
    int first_tile = state->camera_x / TILE_WIDTH;
    int last_tile = (state->camera_x + SCREEN_WIDTH) / TILE_WIDTH;
    
    // Clamp tile indices
    if(first_tile < 0) first_tile = 0;
    if(last_tile >= NUM_TILES) last_tile = NUM_TILES - 1;

    // Draw background tiles
    for(int i = first_tile; i <= last_tile; i++) {
        int tile_world_x = i * TILE_WIDTH;
        int tile_screen_x = tile_world_x - state->camera_x;
        
        // Select the appropriate tile icon
        const Icon* tile_icon = NULL;
        switch(i) {
            case 0:
                tile_icon = &I_map_tile_0;
                break;
            case 1:
                tile_icon = &I_map_tile_1;
                break;
            case 2:
                tile_icon = &I_map_tile_2;
                break;
        }
        
        if(tile_icon != NULL) {
            canvas_draw_icon(canvas, tile_screen_x, 0, tile_icon);
        }
    }
	// Draw ground line
    canvas_draw_line(canvas, 0, GROUND_Y, SCREEN_WIDTH - 1, GROUND_Y);	

    // Draw character PANIS 
    const Icon* char_icon = state->facing_right ? &I_bread_r : &I_bread_l;
    canvas_draw_icon(canvas, state->screen_x, state->y_pos, char_icon);

    // Draw debug info in upper left
    char debug_str[64];
    
    // Line 1: Tiles visible
    snprintf(debug_str, sizeof(debug_str), "T:%d-%d", first_tile, last_tile);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 8, debug_str);
    
    // Line 2: World X position
    snprintf(debug_str, sizeof(debug_str), "WX:%d", state->world_x);
    canvas_draw_str(canvas, 0, 16, debug_str);
  
    // Line 3: Screen X position
    snprintf(debug_str, sizeof(debug_str), "SX:%d", state->screen_x);
    canvas_draw_str(canvas, 0, 24, debug_str);
}

// Input callback function
static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// Apply gravity and update Y position
static void update_physics(GameState* state) {
    // Apply gravity
    if(!state->on_ground) {
        state->y_velocity += GRAVITY;
        if(state->y_velocity > MAX_FALL_SPEED) {
            state->y_velocity = MAX_FALL_SPEED;
        }
    }
    state->y_pos += state->y_velocity; // Update Y position
    // Ground collision
    int ground_pos = GROUND_Y - CHAR_HEIGHT;
    if(state->y_pos >= ground_pos) {
        state->y_pos = ground_pos;
        state->y_velocity = 0;
        state->on_ground = true;
    } else {
        state->on_ground = false;
    }
}

// Handle jump input
static void handle_jump(GameState* state) {
    if(state->on_ground) {
        uint32_t current_time = furi_get_tick();
        bool is_double_click = (current_time - state->last_jump_time) < DOUBLE_CLICK_MS;
        
        // Double-click = big jump, single click = small jump
        state->y_velocity = is_double_click ? BIG_JUMP_VELOCITY : SMALL_JUMP_VELOCITY;
        
        state->last_jump_time = current_time;
        state->on_ground = false;
    }
}

// Update horizontal movement logic
static void update_game(GameState* state, InputKey key) {   
	int old_world_x = state->world_x;
    if(key == InputKeyRight) {
        state->facing_right = true;
        
        // Check if we can move right
        if(state->world_x < TOTAL_MAP_WIDTH - CHAR_WIDTH) {
            // Determine if we should scroll or move character
            if(state->screen_x >= START_SCROLL_X && 
               state->camera_x < TOTAL_MAP_WIDTH - SCREEN_WIDTH) {
                // Scroll the world
                state->camera_x += MOVEMENT_SPEED;
                state->world_x += MOVEMENT_SPEED;
                
                // Clamp camera
                if(state->camera_x > TOTAL_MAP_WIDTH - SCREEN_WIDTH) {
                    int overflow = state->camera_x - (TOTAL_MAP_WIDTH - SCREEN_WIDTH);
                    state->camera_x = TOTAL_MAP_WIDTH - SCREEN_WIDTH;
                    state->screen_x += overflow;
                }
            } else {
                // Move character on screen
                state->screen_x += MOVEMENT_SPEED;
                state->world_x += MOVEMENT_SPEED;
                
                // Clamp to screen edge
                if(state->screen_x > SCREEN_WIDTH - CHAR_WIDTH) {
                    state->screen_x = SCREEN_WIDTH - CHAR_WIDTH;
                }
            }
            
            // Clamp world position
            if(state->world_x > TOTAL_MAP_WIDTH - CHAR_WIDTH) {
                state->world_x = TOTAL_MAP_WIDTH - CHAR_WIDTH;
            }
        }
    } else if(key == InputKeyLeft) {
        state->facing_right = false;
        
        // Check if we can move left
        if(state->world_x > 0) {
            // Determine if we should scroll or move character
            if(state->screen_x <= START_SCROLL_X && state->camera_x > 0) {
                // Scroll the world (move camera left)
                state->camera_x -= MOVEMENT_SPEED;
                state->world_x -= MOVEMENT_SPEED;
                
                // Clamp camera
                if(state->camera_x < 0) {
                    int overflow = -state->camera_x;
                    state->camera_x = 0;
                    state->screen_x -= overflow;
                }
            } else {
                // Move character on screen
                state->screen_x -= MOVEMENT_SPEED;
                state->world_x -= MOVEMENT_SPEED;
                
                // Clamp to screen edge
                if(state->screen_x < 0) {
                    state->screen_x = 0;
                }
            }
            
            // Clamp world position
            if(state->world_x < 0) {
                state->world_x = 0;
            }
        }
    }
   // Vibrate if we hit a boundary
    if((old_world_x != state->world_x) && 
       (state->world_x == 0 || state->world_x == TOTAL_MAP_WIDTH - CHAR_WIDTH)) {
        notification_message(state->notifications, &sequence_single_vibro);
    }
	
}

// Main application entry point
int32_t panis_main(void* p) {
    UNUSED(p);
    
    // Create event queue for input events
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    
    // Initialize game state
    GameState* state = malloc(sizeof(GameState));
    state->world_x = CHAR_START_X;  // Start at 1/4 of screen width
    state->screen_x = CHAR_START_X;
    state->camera_x = 0;
    state->facing_right = true;
    state->running = true;
    state->y_pos = GROUND_Y - CHAR_HEIGHT;
    state->y_velocity = 0;
    state->on_ground = true;
	state->last_jump_time = 0;
	state->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Set up view port
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, input_callback, event_queue);
    
    // Register view port in GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    // Main game loop
    InputEvent event;
    while(state->running) {
        // Process input events
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            // Handle back button
            if(event.key == InputKeyBack && event.type == InputTypePress) {
                state->running = false;
                continue;
            }
            
            // Handle jumping
            if(event.key == InputKeyUp) {
                if(event.type == InputTypePress) {
                   handle_jump(state);
                }
            }
            
            // Handle horizontal movement
            if((event.type == InputTypePress || event.type == InputTypeRepeat) &&
               (event.key == InputKeyLeft || event.key == InputKeyRight)) {
                update_game(state, event.key);
            }
        }
        
        // Update physics every frame (independent of input)
        update_physics(state);
        
        // Request redraw
        view_port_update(view_port);
    }
    
    // Cleanup
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
	furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(event_queue);
    free(state);
    
    return 0;
}