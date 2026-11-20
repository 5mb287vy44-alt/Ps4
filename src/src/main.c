// Neon Snake - homebrew game for PS4 (OpenOrbis toolchain + SDL2 port)
// Controls: D-pad or left stick to steer, any button to restart after game over.

#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  else
#    include <SDL.h>
#  endif
#else
#  include <SDL2/SDL.h>
#endif

#include <stdint.h>
#include <stdlib.h>

#define COLS 32
#define ROWS 18
#define MAX_LEN (COLS * ROWS)
#define AXIS_DEADZONE 16000

typedef struct { int x, y; } Pt;

static Pt snake[MAX_LEN];
static int len;
static int dir_x, dir_y;     // direction applied on the last step
static int next_x, next_y;   // direction requested by the player
static Pt food;
static int score, best;
static int alive;
static Uint32 death_time;
static uint32_t rng_state = 2463534242u;

static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void place_food(void) {
    for (;;) {
        Pt p;
        int ok = 1;
        p.x = (int)(rnd() % COLS);
        p.y = (int)(rnd() % ROWS);
        for (int i = 0; i < len; i++) {
            if (snake[i].x == p.x && snake[i].y == p.y) { ok = 0; break; }
        }
        if (ok) { food = p; return; }
    }
}

static void reset_game(void) {
    len = 4;
    for (int i = 0; i < len; i++) {
        snake[i].x = COLS / 2 - i;
        snake[i].y = ROWS / 2;
    }
    dir_x = next_x = 1;
    dir_y = next_y = 0;
    score = 0;
    alive = 1;
    place_food();
}

static void die(void) {
    alive = 0;
    death_time = SDL_GetTicks();
    if (score > best) best = score;
}

static void set_dir(int dx, int dy) {
    if (dx == -dir_x && dy == -dir_y) return; // no instant reverse
    next_x = dx;
    next_y = dy;
}

static void step(void) {
    Pt h;
    int eat, check_len;

    dir_x = next_x;
    dir_y = next_y;
    h.x = snake[0].x + dir_x;
    h.y = snake[0].y + dir_y;

    if (h.x < 0 || h.x >= COLS || h.y < 0 || h.y >= ROWS) { die(); return; }

    eat = (h.x == food.x && h.y == food.y);
    check_len = eat ? len : len - 1; // the tail moves away if we don't eat
    for (int i = 0; i < check_len; i++) {
        if (snake[i].x == h.x && snake[i].y == h.y) { die(); return; }
    }

    if (eat && len < MAX_LEN) len++;
    for (int i = len - 1; i > 0; i--) snake[i] = snake[i - 1];
    snake[0] = h;

    if (eat) {
        score += 10;
        if (len >= MAX_LEN) { die(); return; } // board is full: you win
        place_food();
    }
}

static int step_interval_ms(void) {
    int ms = 130 - (score / 10) * 3;
    return ms < 60 ? 60 : ms;
}

// ---------- drawing ----------

static void fill(SDL_Renderer *r, int x, int y, int w, int h,
                 Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
    SDL_Rect rc;
    rc.x = x; rc.y = y; rc.w = w; rc.h = h;
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_RenderFillRect(r, &rc);
}

static void glow_cell(SDL_Renderer *r, int px, int py, int cell,
                      Uint8 R, Uint8 G, Uint8 B, int grow) {
    int g1 = cell / 3 + grow, g2 = cell / 6 + grow / 2;
    fill(r, px - g1, py - g1, cell + 2 * g1, cell + 2 * g1, R, G, B, 35);
    fill(r, px - g2, py - g2, cell + 2 * g2, cell + 2 * g2, R, G, B, 80);
    fill(r, px + 2, py + 2, cell - 4, cell - 4, R, G, B, 255);
}

static const Uint8 SEG[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static void draw_digit(SDL_Renderer *r, int x, int y, int w, int d,
                       Uint8 R, Uint8 G, Uint8 B) {
    int t = w / 5 < 2 ? 2 : w / 5;
    Uint8 m = SEG[d];
    if (m & 0x01) fill(r, x + t, y,               w - 2 * t, t,     R, G, B, 255); // a
    if (m & 0x02) fill(r, x + w - t, y + t,       t, w - t,         R, G, B, 255); // b
    if (m & 0x04) fill(r, x + w - t, y + w,       t, w - t,         R, G, B, 255); // c
    if (m & 0x08) fill(r, x + t, y + 2 * w - t,   w - 2 * t, t,     R, G, B, 255); // d
    if (m & 0x10) fill(r, x, y + w,               t, w - t,         R, G, B, 255); // e
    if (m & 0x20) fill(r, x, y + t,               t, w - t,         R, G, B, 255); // f
    if (m & 0x40) fill(r, x + t, y + w - t / 2,   w - 2 * t, t,     R, G, B, 255); // g
}

// Draws a number starting at x (left-aligned). Returns the width used.
static int draw_number(SDL_Renderer *r, int x, int y, int w, int value,
                       Uint8 R, Uint8 G, Uint8 B) {
    int digits[12], n = 0, gap = w / 3;
    if (value < 0) value = 0;
    if (value == 0) digits[n++] = 0;
    while (value > 0 && n < 12) { digits[n++] = value % 10; value /= 10; }
    for (int i = 0; i < n; i++) {
        draw_digit(r, x + i * (w + gap), y, w, digits[n - 1 - i], R, G, B);
    }
    return n * (w + gap);
}

static void render(SDL_Renderer *r, Uint32 now) {
    int W = 1920, H = 1080;
    int hud_h, cell, ox, oy, phase, tri;

    SDL_GetRendererOutputSize(r, &W, &H);
    if (W < 64 || H < 64) { W = 1920; H = 1080; }

    hud_h = H / 9;
    cell = (H - hud_h) / ROWS;
    if (W / COLS < cell) cell = W / COLS;
    ox = (W - cell * COLS) / 2;
    oy = hud_h + ((H - hud_h) - cell * ROWS) / 2;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 8, 8, 22, 255);
    SDL_RenderClear(r);

    // faint grid
    SDL_SetRenderDrawColor(r, 80, 80, 160, 22);
    for (int i = 0; i <= COLS; i++)
        SDL_RenderDrawLine(r, ox + i * cell, oy, ox + i * cell, oy + ROWS * cell);
    for (int j = 0; j <= ROWS; j++)
        SDL_RenderDrawLine(r, ox, oy + j * cell, ox + COLS * cell, oy + j * cell);

    // pulsing frame
    phase = (int)((now / 3) % 512);
    tri = phase < 256 ? phase : 511 - phase;
    {
        SDL_Rect fr;
        Uint8 fr_r = alive ? 0 : 255, fr_g = alive ? 200 : 40, fr_b = alive ? 255 : 60;
        for (int k = 0; k < 4; k++) {
            fr.x = ox - 2 - k; fr.y = oy - 2 - k;
            fr.w = cell * COLS + 4 + 2 * k; fr.h = cell * ROWS + 4 + 2 * k;
            SDL_SetRenderDrawColor(r, fr_r, fr_g, fr_b, (Uint8)((alive ? 150 : 100 + tri / 2) / (k + 1)));
            SDL_RenderDrawRect(r, &fr);
        }
    }

    // food (pulsing)
    glow_cell(r, ox + food.x * cell, oy + food.y * cell, cell, 57, 255, 20, tri * cell / 2048);

    // snake: cyan head -> magenta tail (grey-red when dead)
    for (int i = len - 1; i >= 0; i--) {
        int t = len > 1 ? (i * 255) / (len - 1) : 0;
        Uint8 R = (Uint8)(t), G = (Uint8)(255 - t), B = (Uint8)(255 - t / 4);
        if (!alive) { R = (Uint8)(150 + t / 4); G = 50; B = 60; }
        glow_cell(r, ox + snake[i].x * cell, oy + snake[i].y * cell, cell, R, G, B, 0);
    }

    // HUD: score (left) and best (right)
    {
        int dw = hud_h / 4;
        int y = (hud_h - 2 * dw) / 2;
        draw_number(r, ox, y, dw, score, 0, 255, 255);
        draw_number(r, ox + cell * COLS - 6 * (dw + dw / 3), y, dw, best, 255, 0, 200);
    }

    // game over: big final score in the middle
    if (!alive) {
        int dw = H / 8;
        int total_w = 0, v = score, n = 0;
        if (v == 0) n = 1;
        while (v > 0) { n++; v /= 10; }
        total_w = n * (dw + dw / 3);
        fill(r, 0, 0, W, H, 0, 0, 0, 120);
        draw_number(r, (W - total_w) / 2, (H - 2 * dw) / 2, dw, score, 255, 60, 90);
    }

    SDL_RenderPresent(r);
}

// ---------- input ----------

static void read_input(SDL_Joystick *joy) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    if (k) {
        if (k[SDL_SCANCODE_UP])    set_dir(0, -1);
        if (k[SDL_SCANCODE_DOWN])  set_dir(0, 1);
        if (k[SDL_SCANCODE_LEFT])  set_dir(-1, 0);
        if (k[SDL_SCANCODE_RIGHT]) set_dir(1, 0);
    }
    if (joy) {
        if (SDL_JoystickNumHats(joy) > 0) {
            Uint8 hat = SDL_JoystickGetHat(joy, 0);
            if (hat & SDL_HAT_UP)    set_dir(0, -1);
            if (hat & SDL_HAT_DOWN)  set_dir(0, 1);
            if (hat & SDL_HAT_LEFT)  set_dir(-1, 0);
            if (hat & SDL_HAT_RIGHT) set_dir(1, 0);
        }
        if (SDL_JoystickNumAxes(joy) >= 2) {
            int ax = SDL_JoystickGetAxis(joy, 0);
            int ay = SDL_JoystickGetAxis(joy, 1);
            if (abs(ax) > abs(ay)) {
                if (ax > AXIS_DEADZONE) set_dir(1, 0);
                else if (ax < -AXIS_DEADZONE) set_dir(-1, 0);
            } else {
                if (ay > AXIS_DEADZONE) set_dir(0, 1);
                else if (ay < -AXIS_DEADZONE) set_dir(0, -1);
            }
        }
    }
}

static int any_button(SDL_Joystick *joy) {
    int n;
    if (!joy) return 0;
    n = SDL_JoystickNumButtons(joy);
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        if (SDL_JoystickGetButton(joy, i)) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Joystick *joy = NULL;
    SDL_Event ev;
    int running = 1;
    int press = 0;
    Uint32 last_step, last_joy_try = 0;

    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    }

    window = SDL_CreateWindow("Neon Snake", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1920, 1080, SDL_WINDOW_SHOWN);
    if (!window) { SDL_Quit(); return 1; }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    rng_state ^= (uint32_t)SDL_GetTicks() * 2654435761u;
    if (rng_state == 0) rng_state = 2463534242u;
    reset_game();
    last_step = SDL_GetTicks();

    while (running) {
        Uint32 now = SDL_GetTicks();
        press = 0;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_JOYBUTTONDOWN) press = 1;
            else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE) press = 1;
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            }
        }

        // (re)try to open the gamepad now and then
        if (!joy && now - last_joy_try > 1000) {
            last_joy_try = now;
            if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);
        }

        read_input(joy);

        if (alive) {
            if (now - last_step >= (Uint32)step_interval_ms()) {
                last_step = now;
                step();
            }
        } else if (now - death_time > 600 && (press || any_button(joy))) {
            reset_game();
            last_step = now;
        }

        render(renderer, now);
        SDL_Delay(2);
    }

    if (joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
