// CC0 1.0 Universal (CC0 1.0)
// Public Domain Dedication
// https://github.com/nmrr

#include <stdio.h>
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <furi_hal_random.h>

#include "i2c/vl6180x.h"

#define SCREEN_SIZE_X 128
#define SCREEN_SIZE_Y 64
#define FPS           10

#define PAD_SIZE_X        3
#define PAD_SIZE_Y        8
#define PLAYER1_PAD_SPEED 4

#define PLAYER2_PAD_SPEED 2
#define BALL_SIZE         4

#define LOG_TAG "vl6180x_flipper_pong_main"

typedef enum {
    EventTypeInput,
    ClockEventTypeTick,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} EventApp;

typedef struct Players {
    NotificationApp* notification;
    FuriMutex* mutex;
    uint8_t vl6180x_address;
    uint8_t player1_X, player1_Y, player2_X, player2_Y;
    uint16_t player1_score, player2_score;
    uint8_t ball_X, ball_Y, ball_X_speed, ball_Y_speed, ball_X_direction, ball_Y_direction;
} Players;

static void draw_callback(Canvas* canvas, void* ctx) {
    FURI_LOG_T(LOG_TAG, __func__);
    furi_assert(ctx);
    Players* playersMutex = ctx;
    furi_mutex_acquire(playersMutex->mutex, FuriWaitForever);
    notification_message(playersMutex->notification, &sequence_display_backlight_on);
    
    canvas_draw_frame(canvas, 0, 0, 128, 64);
    // Player 1 is stored on the right hand side, while it should be displayed on the left.
    // Flipping the view to achieve this quickly
    canvas_draw_box(canvas, SCREEN_SIZE_X - PAD_SIZE_X - playersMutex->player1_X, playersMutex->player1_Y, PAD_SIZE_X, PAD_SIZE_Y);
    canvas_draw_box(canvas, SCREEN_SIZE_X - PAD_SIZE_X - playersMutex->player2_X, playersMutex->player2_Y, PAD_SIZE_X, PAD_SIZE_Y);
    canvas_draw_box(canvas, SCREEN_SIZE_X - BALL_SIZE - playersMutex->ball_X, playersMutex->ball_Y, BALL_SIZE, BALL_SIZE);

    canvas_set_font(canvas, FontPrimary);
    char buffer[16];
    
    if(playersMutex->vl6180x_address == VL6180X_NO_DEVICE_FOUND_ADDRESS){
        snprintf(
            buffer,
            sizeof(buffer),
            "Missing VL6180X"
        );
    }else{
        snprintf(
            buffer,
            sizeof(buffer),
            "%u - %u",
            playersMutex->player1_score,
            playersMutex->player2_score
        );
    }
    canvas_draw_str_aligned(canvas, SCREEN_SIZE_X / 2, SCREEN_SIZE_Y / 2, AlignCenter, AlignTop, buffer);

    furi_mutex_release(playersMutex->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FURI_LOG_T(LOG_TAG, __func__);
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    EventApp event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void clock_tick(void* ctx) {
    FURI_LOG_T(LOG_TAG, __func__);
    furi_assert(ctx);
    FuriMessageQueue* queue = ctx;
    EventApp event = {.type = ClockEventTypeTick};
    furi_message_queue_put(queue, &event, 0);
}

bool insidePad(uint8_t x, uint8_t y, uint8_t playerX, uint8_t playerY) {
    FURI_LOG_T(LOG_TAG, __func__);
    
    return x >= playerX && x <= playerX + PAD_SIZE_X && y >= playerY && y <= playerY + PAD_SIZE_Y;
}

uint8_t changeSpeed() {
    FURI_LOG_T(LOG_TAG, __func__);
    uint8_t randomuint8[1];
    while(1) {
        furi_hal_random_fill_buf(randomuint8, 1);
        randomuint8[0] &= 0b00000011;
        if(randomuint8[0] >= 1) break;
    }
    return randomuint8[0];
}

uint8_t changeDirection() {
    FURI_LOG_T(LOG_TAG, __func__);
    uint8_t randomuint8[1];
    furi_hal_random_fill_buf(randomuint8, 1);
    randomuint8[0] &= 0b1;
    return randomuint8[0];
}

int32_t flipper_pong_app(void* p) {
    FURI_LOG_T(LOG_TAG, __func__);
    UNUSED(p);
    EventApp event;
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(EventApp));

    Players players;
    players.player1_X = SCREEN_SIZE_X - PAD_SIZE_X - 1;
    players.player1_Y = SCREEN_SIZE_Y / 2 - PAD_SIZE_Y / 2;
    players.player1_score = 0;

    players.player2_X = 1;
    players.player2_Y = SCREEN_SIZE_Y / 2 - PAD_SIZE_Y / 2;
    players.player2_score = 0;

    players.ball_X = SCREEN_SIZE_X / 2 - BALL_SIZE / 2;
    players.ball_Y = SCREEN_SIZE_Y / 2 - BALL_SIZE / 2;
    players.ball_X_speed = 1;
    players.ball_Y_speed = 1;
    players.ball_X_direction = changeDirection();
    players.ball_Y_direction = changeDirection();

    players.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!players.mutex) {
        furi_message_queue_free(event_queue);
        return 255;
    }
    
    players.vl6180x_address = find_vl6180x_address();
    if(players.vl6180x_address == VL6180X_NO_DEVICE_FOUND_ADDRESS){
        FURI_LOG_I(LOG_TAG, "Failed to find VL6180X");
    }else{
        configure_vl6180x(players.vl6180x_address);
    }

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, &players);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    players.notification = furi_record_open(RECORD_NOTIFICATION);

    FuriTimer* timer = furi_timer_alloc(clock_tick, FuriTimerTypePeriodic, event_queue);
    furi_timer_start(timer, 1000 / FPS);

    while(1) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, FuriWaitForever);
        furi_mutex_acquire(players.mutex, FuriWaitForever);
        FURI_LOG_T(LOG_TAG, "Got Event: %d", event_status);

        if(event_status == FuriStatusOk) {
            if(event.type == EventTypeInput) {
                if(event.input.key == InputKeyBack) {
                    furi_mutex_release(players.mutex);
                    break;
                }
            } else if(event.type == ClockEventTypeTick){
                if(players.vl6180x_address == VL6180X_NO_DEVICE_FOUND_ADDRESS) {
                    players.vl6180x_address = find_vl6180x_address();
                    
                    // If the sensor is found afterwards, configure it and start it
                    if(players.vl6180x_address != VL6180X_NO_DEVICE_FOUND_ADDRESS){
                        configure_vl6180x(players.vl6180x_address);
                    }
                }else{
                    uint8_t distance = read_vl6180x_range(players.vl6180x_address);
                    FURI_LOG_T(LOG_TAG, "Distance Read: %d mm", distance);
                    
                    if(distance == VL6180X_FAILED_DISTANCE) {
                        players.vl6180x_address = VL6180X_NO_DEVICE_FOUND_ADDRESS;
                    }else{
                        if(distance < 20) distance = 20;
                        if(distance > 150) distance = 150;
                        players.player1_Y = SCREEN_SIZE_Y - PAD_SIZE_Y - (distance - 20) * (SCREEN_SIZE_Y - PAD_SIZE_Y) / 130;
                        
                        if(players.ball_X + BALL_SIZE / 2 <= SCREEN_SIZE_X * 0.35 && players.ball_X_direction == 0) {
                            if(players.ball_Y + BALL_SIZE / 2 < players.player2_Y + PAD_SIZE_Y / 2) {
                                if(players.player2_Y >= 1 + PLAYER2_PAD_SPEED)
                                    players.player2_Y -= PLAYER2_PAD_SPEED;
                                else
                                    players.player2_Y = 1;
                            } else if(players.ball_Y + BALL_SIZE / 2 > players.player2_Y + PAD_SIZE_Y / 2) {
                                if(players.player2_Y <= SCREEN_SIZE_Y - PAD_SIZE_Y - PLAYER2_PAD_SPEED - 1)
                                    players.player2_Y += PLAYER2_PAD_SPEED;
                                else
                                    players.player2_Y = SCREEN_SIZE_Y - PAD_SIZE_Y - 1;
                            }
                        }

                        uint8_t ball_corner_X[4] = {
                            players.ball_X,
                            players.ball_X + BALL_SIZE,
                            players.ball_X + BALL_SIZE,
                            players.ball_X
                        };
                        uint8_t ball_corner_Y[4] = {
                            players.ball_Y,
                            players.ball_Y,
                            players.ball_Y + BALL_SIZE,
                            players.ball_Y + BALL_SIZE
                        };
                        bool insidePlayer1 = false, insidePlayer2 = false;

                        for(int i = 0; i < 4; i++) {
                            if(insidePad(ball_corner_X[i], ball_corner_Y[i], players.player1_X, players.player1_Y) == true) {
                                insidePlayer1 = true;
                                break;
                            }

                            if(insidePad(ball_corner_X[i], ball_corner_Y[i], players.player2_X, players.player2_Y) == true) {
                                insidePlayer2 = true;
                                break;
                            }
                        }

                        if(insidePlayer1 == true) {
                            players.ball_X_direction = 0;
                            players.ball_X -= players.ball_X_speed;
                            players.ball_X_speed = changeSpeed();
                            players.ball_Y_speed = changeSpeed();
                        } else if(insidePlayer2 == true) {
                            players.ball_X_direction = 1;
                            players.ball_X += players.ball_X_speed;
                            players.ball_X_speed = changeSpeed();
                            players.ball_Y_speed = changeSpeed();
                        } else {
                            if(players.ball_X_direction == 1) {
                                if(players.ball_X <= SCREEN_SIZE_X - BALL_SIZE - 1 - players.ball_X_speed) {
                                    players.ball_X += players.ball_X_speed;
                                } else {
                                    players.ball_X = SCREEN_SIZE_X / 2 - BALL_SIZE / 2;
                                    players.ball_Y = SCREEN_SIZE_Y / 2 - BALL_SIZE / 2;
                                    players.ball_X_speed = 1;
                                    players.ball_Y_speed = 1;
                                    players.ball_X_direction = 0;
                                    players.player2_score++;
                                }
                            } else {
                                if(players.ball_X >= 1 + players.ball_X_speed) {
                                    players.ball_X -= players.ball_X_speed;
                                } else {
                                    players.ball_X = SCREEN_SIZE_X / 2 - BALL_SIZE / 2;
                                    players.ball_Y = SCREEN_SIZE_Y / 2 - BALL_SIZE / 2;
                                    players.ball_X_speed = 1;
                                    players.ball_Y_speed = 1;
                                    players.ball_X_direction = 1;
                                    players.player1_score++;
                                }
                            }
                        }

                        if(players.ball_Y_direction == 1) {
                            if(players.ball_Y <= SCREEN_SIZE_Y - BALL_SIZE - 1 - players.ball_Y_speed) {
                                players.ball_Y += players.ball_Y_speed;
                            } else {
                                players.ball_Y = SCREEN_SIZE_Y - BALL_SIZE - 1;
                                players.ball_X_speed = changeSpeed();
                                players.ball_Y_speed = changeSpeed();
                                players.ball_Y_direction = 0;
                            }
                        } else {
                            if(players.ball_Y >= 1 + players.ball_Y_speed) {
                                players.ball_Y -= players.ball_Y_speed;
                            } else {
                                players.ball_Y = 1;
                                players.ball_X_speed = changeSpeed();
                                players.ball_Y_speed = changeSpeed();
                                players.ball_Y_direction = 1;
                            }
                        }
                    }
                }
            }
        }

        furi_mutex_release(players.mutex);
        view_port_update(view_port);
    }

    furi_timer_stop(timer);
    notification_message(players.notification, &sequence_reset_rgb);
    furi_message_queue_free(event_queue);
    furi_mutex_free(players.mutex);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_timer_free(timer);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    return 0;
}
