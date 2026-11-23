/*
 * FISHNOID - Criado por: wmo586 & Gemini (Google AI) - 22/11/2026
 *
 * v2.0 - tijolos mais altos (altura calculada pelo tamanho da tela, ocupando
 * ~metade da arena) e setas desenhadas em pontos nos botoes laterais.
 * v1.9 - correcao real da colisao nas paredes e teto.
 */
#include "lakeshark_app.h"
#include <stdio.h>
#include <math.h>

#define BRICK_ROWS   4
#define BRICK_COLS   6
#define MAX_STAGES   10
#define PADDLE_W     5
#define MIN_BTN_W    8
#define MAX_VX       0.68f
#define MAX_VY       0.68f

#define NUM_BUBBLES  8
#define NUM_PARTS    24
#define TRAIL_LEN    3

#define BRICK_TOP    3      /* primeira linha de tijolos */
#define BRICK_H_MAX  4      /* altura maxima de cada tijolo, em celulas */
#define ARROW_MAX_W  16     /* area da seta, em celulas */
#define ARROW_MAX_H  9

enum {
    STATE_TITLE = 0,
    STATE_PLAYING,
    STATE_STAGE_CLEAR,
    STATE_GAMEOVER,
    STATE_VICTORY
};

static int state, cols, rows, btn_w, arena_x, arena_w, paddle_x, score, lives, stage;
static int state_timer;
static int brick_h = 1;     /* altura de cada tijolo em celulas */
static uint8_t arrow_dots[2][(ARROW_MAX_W * 2) * (ARROW_MAX_H * 3)];
static float ball_x, ball_y, ball_vx, ball_vy, base_speed;
static uint8_t bricks[BRICK_ROWS][BRICK_COLS];
static bool btn_left_held, btn_right_held, intro_played;

/* Efeitos visuais (nao afetam a jogabilidade) */
static int paddle_flash, banner_timer;
static int trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
static float part_x[NUM_PARTS], part_y[NUM_PARTS], part_vx[NUM_PARTS], part_vy[NUM_PARTS];
static uint8_t part_life[NUM_PARTS], part_col[NUM_PARTS];

static const uint8_t row_colors[BRICK_ROWS] = {
    LAP_RED | LAP_BRIGHT,
    LAP_YELLOW | LAP_BRIGHT,
    LAP_GREEN | LAP_BRIGHT,
    LAP_CYAN | LAP_BRIGHT
};

static const uint8_t confetti_col[5] = {
    LAP_RED | LAP_BRIGHT, LAP_GREEN | LAP_BRIGHT, LAP_YELLOW | LAP_BRIGHT,
    LAP_CYAN | LAP_BRIGHT, LAP_MAGENTA | LAP_BRIGHT
};

/* Posicao, velocidade e fase de cada bolha (sem estado: so depende do frame) */
static const uint8_t bub_x[NUM_BUBBLES]   = {7, 23, 41, 13, 59, 31, 47, 19};
static const uint8_t bub_spd[NUM_BUBBLES] = {3, 5, 4, 6, 3, 5, 4, 2};
static const uint8_t bub_off[NUM_BUBBLES] = {0, 9, 4, 14, 7, 2, 11, 5};

/* Matriz de chars (sem ponteiros) para nao exigir relocacao no .lap */
static const char stage_names[MAX_STAGES][14] = {
    "CLASSICA", "XADREZ", "ONDAS", "PIRAMIDE", "CARDUME",
    "FORTALEZA", "PILARES", "TUNEL", "RECIFE", "DESAFIO FINAL"
};

/* 0 = vazio, 1 = bloco normal (1 hit), 2 = bloco reforcado (2 hits) */
static const uint8_t stage_layouts[MAX_STAGES][BRICK_ROWS][BRICK_COLS] = {
    /* Fase 1: Classica */
    {
        {1,1,1,1,1,1},
        {1,1,1,1,1,1},
        {1,1,1,1,1,1},
        {1,1,1,1,1,1}
    },
    /* Fase 2: Xadrez */
    {
        {1,0,1,0,1,0},
        {0,1,0,1,0,1},
        {1,0,1,0,1,0},
        {0,1,0,1,0,1}
    },
    /* Fase 3: Ondas */
    {
        {1,1,0,0,1,1},
        {0,1,1,0,0,1},
        {0,0,1,1,0,0},
        {1,0,0,1,1,0}
    },
    /* Fase 4: Piramide invertida */
    {
        {1,1,1,1,1,1},
        {0,1,1,1,1,0},
        {0,0,1,1,0,0},
        {0,0,2,2,0,0}
    },
    /* Fase 5: Cardume */
    {
        {0,1,1,1,1,0},
        {1,2,1,1,2,1},
        {0,1,1,1,1,0},
        {1,0,1,1,0,1}
    },
    /* Fase 6: Fortaleza */
    {
        {2,1,1,1,1,2},
        {2,0,2,2,0,2},
        {2,0,2,2,0,2},
        {2,1,1,1,1,2}
    },
    /* Fase 7: Pilares */
    {
        {2,0,2,0,2,0},
        {1,0,1,0,1,0},
        {2,0,2,0,2,0},
        {1,1,1,1,1,1}
    },
    /* Fase 8: Tunel */
    {
        {2,2,2,2,2,2},
        {2,1,0,0,1,2},
        {2,1,0,0,1,2},
        {2,0,2,2,0,2}
    },
    /* Fase 9: Recife de Coral */
    {
        {2,1,2,2,1,2},
        {1,2,1,1,2,1},
        {2,1,2,2,1,2},
        {1,2,0,0,2,1}
    },
    /* Fase 10: Desafio Final */
    {
        {2,2,2,2,2,2},
        {2,2,1,1,2,2},
        {2,1,2,2,1,2},
        {2,2,2,2,2,2}
    }
};

/* ------------------------------------------------------------------ */
/* Utilidades de desenho                                              */
/* ------------------------------------------------------------------ */

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void put_center(int x0, int w, int y, const char *s, uint8_t attr) {
    int x = x0 + (w - slen(s)) / 2;
    if (x < x0) x = x0;
    lap_put_str(x, y, s, attr);
}

/* Linha de ondas: dois tons que correm lateralmente */
static void draw_wave(int x0, int w, int y, int phase, uint8_t a, uint8_t b) {
    for (int i = 0; i < w; i++) {
        int k = (i + phase) & 7;
        lap_put_cell(x0 + i, y, '~', (k < 4) ? a : b);
    }
}

/* Bolhas subindo; posicao calculada so a partir do frame */
static void draw_bubbles(int x0, int w, int y0, int y1) {
    if (w <= 0 || y1 < y0) return;
    uint32_t span = (uint32_t)(y1 - y0 + 1);
    uint32_t f = lap_frame();
    for (int i = 0; i < NUM_BUBBLES; i++) {
        uint32_t t = (f * bub_spd[i] / 6 + (uint32_t)bub_off[i] * 5) % span;
        int y = y1 - (int)t;
        int s = (int)((f / 12 + (uint32_t)i * 3) % 4);
        int sway = (s == 1) ? 1 : ((s == 3) ? -1 : 0);
        int x = (bub_x[i] * 11) % w + sway;
        if (x < 0) x = 0;
        if (x >= w) x = w - 1;
        if (i & 1) lap_put_cell(x0 + x, y, '.', LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK));
        else       lap_put_cell(x0 + x, y, 'o', LAP_ATTR(LAP_CYAN, LAP_BLACK));
    }
}

/* ------------------------------------------------------------------ */
/* Particulas                                                         */
/* ------------------------------------------------------------------ */

static void clear_parts(void) {
    for (int i = 0; i < NUM_PARTS; i++) part_life[i] = 0;
}

static void spawn_part(float x, float y, float vx, float vy, int life, uint8_t col) {
    for (int i = 0; i < NUM_PARTS; i++) {
        if (part_life[i] == 0) {
            part_x[i] = x;
            part_y[i] = y;
            part_vx[i] = vx;
            part_vy[i] = vy;
            part_life[i] = (uint8_t)life;
            part_col[i] = col;
            return;
        }
    }
}

static void spawn_burst(int cx, int cy, uint8_t col) {
    for (int k = 0; k < 5; k++) {
        float vx = ((int)(lap_random() % 11) - 5) * 0.07f;
        float vy = -0.08f - (float)(lap_random() % 5) * 0.05f;
        spawn_part((float)cx, (float)cy, vx, vy, 10 + (int)(lap_random() % 6), col);
    }
}

static void update_parts(void) {
    for (int i = 0; i < NUM_PARTS; i++) {
        if (part_life[i] == 0) continue;
        part_x[i] += part_vx[i];
        part_y[i] += part_vy[i];
        part_vy[i] += 0.04f;
        part_life[i]--;
    }
}

static void draw_parts(void) {
    for (int i = 0; i < NUM_PARTS; i++) {
        if (part_life[i] == 0) continue;
        int ix = (int)part_x[i], iy = (int)part_y[i];
        if (ix <= arena_x || ix >= arena_x + arena_w - 1 || iy < 2 || iy >= rows - 1) continue;
        int16_t ch = (part_life[i] > 8) ? '*' : ((part_life[i] > 4) ? '+' : '.');
        uint8_t col = (part_life[i] > 4) ? part_col[i] : (uint8_t)(part_col[i] & 7);
        lap_put_cell(ix, iy, ch, LAP_ATTR(col, LAP_BLACK));
    }
}

/* ------------------------------------------------------------------ */
/* Logica do jogo                                                     */
/* ------------------------------------------------------------------ */

static void compute_layout(void) {
    cols = lap_cols();
    rows = lap_rows();

    /* Aumenta a zona lateral usando o espaco extra liberado pelas 6 colunas */
    int desired_btn = cols / 5;
    if (desired_btn < MIN_BTN_W) desired_btn = MIN_BTN_W;
    if (desired_btn > 16) desired_btn = 16;

    /* Garante arena central minima para 6 colunas e paddle */
    if (cols - (desired_btn * 2) < 20) {
        desired_btn = (cols - 20) / 2;
        if (desired_btn < 4) desired_btn = 4;
    }

    btn_w = desired_btn;
    arena_x = btn_w;
    arena_w = cols - (btn_w * 2);
    if (arena_w < 16) arena_w = 16;

    /* Tijolos ocupam ~metade do espaco entre o topo e a raquete,
       sempre deixando pelo menos 8 linhas livres acima da raquete */
    int field = (rows - 2) - BRICK_TOP;
    int bh = (field / 2) / BRICK_ROWS;
    if (bh > BRICK_H_MAX) bh = BRICK_H_MAX;
    while (bh > 1 && field - bh * BRICK_ROWS < 8) bh--;
    if (bh < 1) bh = 1;
    brick_h = bh;

    int min_px = arena_x + 1;
    int max_px = arena_x + arena_w - PADDLE_W - 1;
    if (max_px < min_px) max_px = min_px;
    if (paddle_x < min_px || paddle_x > max_px) {
        paddle_x = arena_x + (arena_w - PADDLE_W) / 2;
    }
}

static void normalize_vel(float target_speed) {
    float vx = ball_vx;
    float vy = ball_vy;

    if (fabsf(vx) < 0.0001f) vx = (lap_random() % 2 == 0) ? 0.35f : -0.35f;
    if (fabsf(vy) < 0.0001f) vy = (vy < 0.0f) ? -0.35f : 0.35f;

    float mag = sqrtf(vx * vx + vy * vy);
    if (mag < 0.0001f) mag = 1.0f;

    float spd = target_speed;
    if (spd < 0.38f) spd = 0.38f;
    if (spd > 0.85f) spd = 0.85f;

    vx = (vx / mag) * spd;
    vy = (vy / mag) * spd;

    /* Garante angulo minimo (~22 graus) em ambos os eixos */
    float min_comp = spd * 0.38f;
    if (fabsf(vy) < min_comp) {
        float sx = (vx >= 0.0f) ? 1.0f : -1.0f;
        float sy = (vy >= 0.0f) ? 1.0f : -1.0f;
        float diff = spd * spd - min_comp * min_comp;
        float rem = sqrtf((diff > 0.05f) ? diff : 0.05f);
        vx = sx * rem;
        vy = sy * min_comp;
    }
    if (fabsf(vx) < min_comp) {
        float sx = (vx >= 0.0f) ? 1.0f : -1.0f;
        float sy = (vy >= 0.0f) ? 1.0f : -1.0f;
        float diff = spd * spd - min_comp * min_comp;
        float rem = sqrtf((diff > 0.05f) ? diff : 0.05f);
        vx = sx * min_comp;
        vy = sy * rem;
    }

    if (vx > MAX_VX) vx = MAX_VX;
    if (vx < -MAX_VX) vx = -MAX_VX;
    if (vy > MAX_VY) vy = MAX_VY;
    if (vy < -MAX_VY) vy = -MAX_VY;

    ball_vx = vx;
    ball_vy = vy;
}

static void reset_ball(void) {
    ball_x = arena_x + (arena_w / 2.0f);
    ball_y = rows - 4.0f;
    ball_vx = (lap_random() % 2 == 0) ? base_speed : -base_speed;
    ball_vy = -base_speed;
    normalize_vel(base_speed);
    for (int i = 0; i < TRAIL_LEN; i++) {
        trail_x[i] = (int)ball_x;
        trail_y[i] = (int)ball_y;
    }
}

static void load_stage(int st) {
    if (st < 1) st = 1;
    if (st > MAX_STAGES) st = MAX_STAGES;
    stage = st;

    compute_layout();
    paddle_x = arena_x + (arena_w - PADDLE_W) / 2;
    btn_left_held = false;
    btn_right_held = false;
    state_timer = 0;
    paddle_flash = 0;
    banner_timer = 45;
    clear_parts();

    base_speed = 0.42f + ((stage - 1) * 0.022f);

    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            bricks[r][c] = stage_layouts[stage - 1][r][c];
        }
    }

    reset_ball();
    state = STATE_PLAYING;
    lap_tone(660 + (stage * 40), 70, 50, LAP_WAVE_SQUARE);
}

static void start_match(void) {
    score = 0;
    lives = 3;
    load_stage(1);
}

static void enter_end_state(int next_state, uint16_t freq, uint16_t ms, uint8_t vol, int wave) {
    state = next_state;
    state_timer = 25;
    btn_left_held = false;
    btn_right_held = false;
    lap_tone(freq, ms, vol, wave);
}

static void enter(void) {
    lap_immersive(true);
    lap_keep_awake(true);
    compute_layout();
    state = STATE_TITLE;
    intro_played = false;
    state_timer = 0;
    paddle_flash = 0;
    banner_timer = 0;
    clear_parts();
}

static void leave(void) {
    lap_hold_rotation(false);
    lap_immersive(false);
    lap_keep_awake(false);
    lap_tone_stop();
}

/* ------------------------------------------------------------------ */
/* Tela de titulo                                                     */
/* ------------------------------------------------------------------ */

/* Logo grande: as letras trocam de cor em onda */
static void draw_logo(const char *word, int y) {
    static const uint8_t pal[3] = {
        LAP_CYAN | LAP_BRIGHT, LAP_GREEN | LAP_BRIGHT, LAP_WHITE | LAP_BRIGHT
    };
    int len = slen(word);
    int start_x = (cols - ((len * 4) - 1)) / 2;
    if (start_x < 0) start_x = 0;
    int ph = (int)(lap_frame() / 6);
    for (int i = 0; i < len; i++)
        lap_big_char(start_x + (i * 4), y, word[i], LAP_ATTR(pal[(i + ph) % 3], LAP_BLACK));
}

static void handle_title(void) {
    if (!intro_played) {
        intro_played = true;
        lap_tone(523, 80, 45, LAP_WAVE_TRIANGLE);
        lap_tone(659, 80, 45, LAP_WAVE_TRIANGLE);
        lap_tone(784, 80, 45, LAP_WAVE_TRIANGLE);
        lap_tone(1046, 180, 55, LAP_WAVE_TRIANGLE);
    }

    uint32_t f = lap_frame();

    lap_box(0, 0, cols, rows, NULL, LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK));

    /* Agua mais densa na metade de baixo */
    if (rows >= 12)
        lap_fill(1, rows / 2, cols - 2, rows - 1 - rows / 2, LAP_SHADE_25, LAP_ATTR(LAP_BLUE, LAP_BLACK));
    draw_bubbles(1, cols - 2, 1, rows - 2);

    lap_put_str(cols - 4, 1, " X ", LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_RED));

    int logo_y = (rows >= 16) ? 2 : 1;
    draw_logo("FISHNOID", logo_y);

    int fish_y = logo_y + 6;
    if (fish_y < rows - 3 && cols > 12) {
        int swim = (int)((f / 4) % (uint32_t)(cols - 12));
        char eye = ((f / 8) % 2) ? 'o' : '*';

        uint8_t a1 = LAP_ATTR(LAP_YELLOW | LAP_BRIGHT, LAP_BLACK);
        int x1 = 2 + swim;
        lap_put_str(x1, fish_y, "><(((", a1);
        lap_put_cell(x1 + 5, fish_y, eye, a1);
        lap_put_cell(x1 + 6, fish_y, '>', a1);

        uint8_t a2 = LAP_ATTR(LAP_GREEN | LAP_BRIGHT, LAP_BLACK);
        int x2 = cols - 9 - swim;
        lap_put_cell(x2, fish_y + 1, '<', a2);
        lap_put_cell(x2 + 1, fish_y + 1, eye, a2);
        lap_put_str(x2 + 2, fish_y + 1, ")))><", a2);
    }

    if (rows - 5 > fish_y + 3)
        put_center(1, cols - 2, fish_y + 3, "10 FASES - 3 VIDAS", LAP_ATTR(LAP_WHITE, LAP_BLACK));

    /* Ondas no pe da tela */
    if (rows - 4 > fish_y + 1) {
        uint8_t wa = LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK);
        uint8_t wb = LAP_ATTR(LAP_BLUE | LAP_BRIGHT, LAP_BLACK);
        draw_wave(1, cols - 2, rows - 4, (int)(f / 3), wa, wb);
        draw_wave(1, cols - 2, rows - 3, -(int)(f / 3), wb, wa);
    }

    /* Botao piscante */
    {
        const char *msg = " TOQUE OU ENTER P/ JOGAR ";
        uint8_t pa = (((f / 10) % 2) == 0)
            ? LAP_ATTR(LAP_BLACK, LAP_CYAN | LAP_BRIGHT)
            : LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_BLUE);
        put_center(1, cols - 2, rows - 2, msg, pa);
    }

    lap_touch_t t;
    if (lap_touch(&t) && t.down) {
        int cw, ch;
        lap_cell_px(&cw, &ch);
        if (cw > 0 && ch > 0) {
            if ((t.y / ch) <= 2 && (t.x / cw) >= cols - 5) {
                lap_exit();
                return;
            }
            start_match();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Partida                                                            */
/* ------------------------------------------------------------------ */

static void update_playing(void) {
    btn_left_held = btn_right_held = false;

    /* efeitos (so visual) */
    update_parts();
    if (paddle_flash > 0) paddle_flash--;
    if (state == STATE_PLAYING && banner_timer > 0) banner_timer--;
    if (state == STATE_VICTORY && (lap_frame() % 2) == 0 && arena_w > 6) {
        float cx = (float)(arena_x + 2 + (int)(lap_random() % (uint32_t)(arena_w - 4)));
        float cvx = ((int)(lap_random() % 7) - 3) * 0.03f;
        float cvy = 0.12f + (float)(lap_random() % 4) * 0.04f;
        spawn_part(cx, 3.0f, cvx, cvy, 20, confetti_col[lap_random() % 5]);
    }

    if (state_timer > 0) {
        state_timer--;
    }

    if (state == STATE_STAGE_CLEAR) {
        if (state_timer == 0) {
            load_stage(stage + 1);
        }
        return;
    }

    lap_touch_t t;
    if (lap_touch(&t) && t.down) {
        int cw, ch;
        lap_cell_px(&cw, &ch);
        if (cw > 0 && ch > 0) {
            int tx = t.x / cw, ty = t.y / ch;
            if (ty <= 1 && tx >= arena_x + arena_w - 4 && tx < arena_x + arena_w) {
                lap_exit();
                return;
            }
            if ((state == STATE_GAMEOVER || state == STATE_VICTORY) &&
                state_timer == 0 &&
                tx >= arena_x && tx < arena_x + arena_w) {
                start_match();
                return;
            }
            if (state == STATE_PLAYING) {
                if (tx < btn_w) btn_left_held = true;
                else if (tx >= cols - btn_w) btn_right_held = true;
            }
        }
    }

    if (state != STATE_PLAYING) return;

    int min_px = arena_x + 1, max_px = arena_x + arena_w - PADDLE_W - 1;
    if (btn_left_held && paddle_x > min_px) paddle_x--;
    if (btn_right_held && paddle_x < max_px) paddle_x++;

    ball_x += ball_vx;
    ball_y += ball_vy;
    int bx = (int)ball_x, by = (int)ball_y;

    /* rastro da bola: so avanca quando ela troca de celula */
    if (bx != trail_x[0] || by != trail_y[0]) {
        for (int i = TRAIL_LEN - 1; i > 0; i--) {
            trail_x[i] = trail_x[i - 1];
            trail_y[i] = trail_y[i - 1];
        }
        trail_x[0] = bx;
        trail_y[0] = by;
    }

    float cur_speed = sqrtf(ball_vx * ball_vx + ball_vy * ball_vy);
    if (cur_speed < base_speed) cur_speed = base_speed;

    /* Paredes laterais: so rebate se estiver indo em direcao a parede */
    float left_limit = (float)(arena_x + 1);
    float right_limit = (float)(arena_x + arena_w - 2);
    if (ball_vx < 0.0f && ball_x <= left_limit) {
        ball_x = left_limit + (left_limit - ball_x);
        if (ball_x < left_limit) ball_x = left_limit;
        ball_vx = -ball_vx;
        normalize_vel(cur_speed);
        lap_tone(440, 20, 40, LAP_WAVE_SQUARE);
    } else if (ball_vx > 0.0f && ball_x >= right_limit) {
        ball_x = right_limit - (ball_x - right_limit);
        if (ball_x > right_limit) ball_x = right_limit;
        ball_vx = -ball_vx;
        normalize_vel(cur_speed);
        lap_tone(440, 20, 40, LAP_WAVE_SQUARE);
    }

    /* Teto: so rebate se estiver subindo (ball_vy < 0) */
    float top_limit = 2.0f;
    if (ball_vy < 0.0f && ball_y <= top_limit) {
        ball_y = top_limit + (top_limit - ball_y);
        if (ball_y < top_limit) ball_y = top_limit;
        ball_vy = -ball_vy;
        normalize_vel(cur_speed);
        lap_tone(440, 20, 40, LAP_WAVE_SQUARE);
    }

    bx = (int)ball_x;
    by = (int)ball_y;

    /* Raquete */
    if (ball_vy > 0.0f && ball_y >= (float)(rows - 2) && ball_y <= (float)(rows - 1) &&
        bx >= paddle_x && bx < paddle_x + PADDLE_W) {
        ball_y = (float)(rows - 3) - 0.05f;
        float offset = (ball_x - (paddle_x + PADDLE_W / 2.0f)) / (PADDLE_W / 2.0f);
        ball_vx = offset * MAX_VX;
        ball_vy = -fabsf(ball_vy);
        normalize_vel(cur_speed + 0.015f);
        paddle_flash = 6;
        lap_tone(660, 30, 50, LAP_WAVE_TRIANGLE);
    }

    if (by >= rows - 1) {
        lives--;
        if (lives <= 0) {
            enter_end_state(STATE_GAMEOVER, 150, 200, 60, LAP_WAVE_SAW);
        } else {
            lap_tone(150, 200, 60, LAP_WAVE_SAW);
            reset_ball();
        }
        return;
    }

    int inner_w = arena_w - 2;
    int brick_w = (inner_w / BRICK_COLS) < 2 ? 2 : (inner_w / BRICK_COLS);
    int start_x = arena_x + 1 + (inner_w - (brick_w * BRICK_COLS)) / 2;

    if (by >= BRICK_TOP && by < BRICK_TOP + BRICK_ROWS * brick_h && bx >= start_x) {
        int r = (by - BRICK_TOP) / brick_h, c = (bx - start_x) / brick_w;
        if (c >= 0 && c < BRICK_COLS && bricks[r][c] > 0) {
            int prev_by = (int)(ball_y - ball_vy);
            bricks[r][c]--;
            if (prev_by != by) {
                ball_y = (ball_vy > 0.0f) ? ((float)by - 0.10f) : ((float)by + 1.05f);
                ball_vy = -ball_vy;
            } else {
                ball_x += (ball_vx > 0.0f) ? -0.35f : 0.35f;
                ball_vx = -ball_vx;
            }
            normalize_vel(cur_speed);
            score += 10;
            int cx = start_x + c * brick_w + (brick_w - 1) / 2;
            int cy = BRICK_TOP + r * brick_h + (brick_h - 1) / 2;
            if (bricks[r][c] > 0) {
                lap_tone(520, 25, 45, LAP_WAVE_TRIANGLE);
                spawn_part((float)cx, (float)cy, -0.12f, -0.10f, 8, LAP_WHITE | LAP_BRIGHT);
                spawn_part((float)cx, (float)cy, 0.12f, -0.10f, 8, LAP_WHITE | LAP_BRIGHT);
            } else {
                lap_tone(880 + (BRICK_ROWS - r) * 110, 35, 50, LAP_WAVE_SQUARE);
                spawn_burst(cx, cy, row_colors[r]);
            }
        }
    }

    bool any_left = false;
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            if (bricks[r][c] > 0) any_left = true;

    if (!any_left) {
        if (stage < MAX_STAGES) {
            state = STATE_STAGE_CLEAR;
            state_timer = 35;
            lap_tone(988, 120, 55, LAP_WAVE_TRIANGLE);
        } else {
            enter_end_state(STATE_VICTORY, 1046, 250, 60, LAP_WAVE_TRIANGLE);
        }
    }
}

/* Botao lateral: azul escuro em repouso, ciano quando pressionado.
   A seta e desenhada em pontos (2x3 por celula): ponta triangular + haste. */
static void draw_side(int x, bool held, int dir /* -1 esquerda, +1 direita */) {
    uint8_t fg = held ? LAP_BLACK : (LAP_CYAN | LAP_BRIGHT);
    uint8_t bg = held ? (LAP_CYAN | LAP_BRIGHT) : LAP_BLUE;
    uint8_t a = LAP_ATTR(fg, bg);
    lap_fill(x, 0, btn_w, rows, ' ', a);
    lap_box(x, 0, btn_w, rows, NULL, a);

    int cw = btn_w - 2;
    if (cw > ARROW_MAX_W) cw = ARROW_MAX_W;
    int chh = rows / 4;
    if (chh > ARROW_MAX_H) chh = ARROW_MAX_H;
    if (chh > rows - 2) chh = rows - 2;
    if (chh < 3 || cw < 2) return;

    int W = cw * 2, H = chh * 3;
    int mx = W / 8; if (mx < 1) mx = 1;
    int L = H / 2 - 1;                      /* comprimento da ponta */
    if (L > (W - 2 * mx) / 2) L = (W - 2 * mx) / 2;
    if (L < 1) L = 1;
    int sh = H / 10; if (sh < 1) sh = 1;     /* meia espessura da haste */

    uint8_t *d = arrow_dots[dir < 0 ? 0 : 1];
    for (int y = 0; y < H; y++) {
        int dy2 = 2 * y - (H - 1);
        if (dy2 < 0) dy2 = -dy2;
        for (int xx = 0; xx < W; xx++) {
            int px = (dir < 0) ? xx : (W - 1 - xx);  /* espelha p/ direita */
            int t = px - mx;
            bool on = false;
            if (t >= 0 && t <= L && dy2 <= 2 * t + 1) on = true;          /* ponta */
            if (px >= mx + L / 2 && px < W - mx && dy2 <= 2 * sh + 1) on = true; /* haste */
            d[y * W + xx] = on ? fg : bg;
        }
    }
    lap_dots(x + 1 + (btn_w - 2 - cw) / 2, (rows - chh) / 2, cw, chh, d);
}

/* Tijolo alto: corpo cheio na cor clara e a ultima linha so com a metade
   de cima (a de baixo fica escura) = sombra/efeito 3D e espaco entre fileiras */
static void draw_brick(int x, int y, int len, int h, uint8_t hp, int r) {
    uint8_t attr;
    if (hp >= 2) attr = LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_MAGENTA);
    else         attr = LAP_ATTR(row_colors[r], row_colors[r] & 7);
    for (int i = 0; i < h - 1; i++)
        lap_hline(x, y + i, len, LAP_BLOCK, attr);
    lap_hline(x, y + h - 1, len, LAP_QUAD(1, 1, 0, 0), attr);
    /* tijolo reforcado: marca no meio */
    if (hp >= 2 && h >= 2 && len >= 3)
        lap_put_cell(x + len / 2, y + (h - 1) / 2, '#', LAP_ATTR(LAP_MAGENTA, LAP_WHITE | LAP_BRIGHT));
}

static void draw_paddle(void) {
    bool fl = paddle_flash > 0;
    uint8_t body = LAP_ATTR(fl ? (LAP_YELLOW | LAP_BRIGHT) : (LAP_WHITE | LAP_BRIGHT), LAP_BLACK);
    uint8_t cap  = LAP_ATTR(fl ? (LAP_YELLOW | LAP_BRIGHT) : (LAP_CYAN | LAP_BRIGHT), LAP_BLACK);
    int y = rows - 2;
    lap_put_cell(paddle_x, y, LAP_QUAD(0, 1, 1, 1), cap);
    lap_hline(paddle_x + 1, y, PADDLE_W - 2, LAP_BLOCK, body);
    lap_put_cell(paddle_x + PADDLE_W - 1, y, LAP_QUAD(1, 0, 1, 1), cap);
}

/* Painel de mensagem centralizado na arena (5 linhas de altura) */
static void panel(const char *title, uint8_t attr, int *out_x, int *out_w, int *out_y) {
    int bw = 22;
    if (bw > arena_w - 2) bw = arena_w - 2;
    int bx = arena_x + (arena_w - bw) / 2;
    int by = rows / 2 - 2;
    if (by < 3) by = 3;
    lap_box(bx, by, bw, 5, title, attr);
    *out_x = bx;
    *out_w = bw;
    *out_y = by;
}

static void draw_hud(void) {
    uint8_t bar = LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_BLUE);
    char buf[24];

    lap_fill(arena_x, 0, arena_w, 1, ' ', bar);

    if (arena_w >= 36) {
        lap_put_str(arena_x + 1, 0, "PTS", LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLUE));
        snprintf(buf, sizeof(buf), "%04d", score);
        lap_put_str(arena_x + 5, 0, buf, bar);

        snprintf(buf, sizeof(buf), "FASE %02d/%02d", stage, MAX_STAGES);
        put_center(arena_x, arena_w, 0, buf, LAP_ATTR(LAP_YELLOW | LAP_BRIGHT, LAP_BLUE));

        /* vidas = peixinhos */
        int lx = arena_x + arena_w - 5 - (lives * 4 - 1);
        for (int i = 0; i < lives; i++)
            lap_put_str(lx + i * 4, 0, "><>", LAP_ATTR(LAP_RED | LAP_BRIGHT, LAP_BLUE));
    } else {
        snprintf(buf, sizeof(buf), "PTS%04d F%02d V%d", score, stage, lives);
        lap_put_str(arena_x + 1, 0, buf, bar);
    }

    lap_put_str(arena_x + arena_w - 3, 0, " X ", LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_RED));
}

static void draw_playing(void) {
    uint32_t f = lap_frame();

    draw_side(0, btn_left_held, -1);
    draw_side(cols - btn_w, btn_right_held, 1);

    /* moldura da arena */
    lap_box(arena_x, 1, arena_w, rows - 1, NULL, LAP_ATTR(LAP_BLUE | LAP_BRIGHT, LAP_BLACK));
    int ix = arena_x + 1, iw = arena_w - 2;

    /* agua: mais densa no alto, escurecendo com a profundidade */
    int wh = rows - 4;
    if (wh > 3 && iw > 0) {
        int h1 = wh / 3;
        lap_fill(ix, 3, iw, h1, LAP_SHADE_50, LAP_ATTR(LAP_BLUE, LAP_BLACK));
        lap_fill(ix, 3 + h1, iw, h1, LAP_SHADE_25, LAP_ATTR(LAP_BLUE, LAP_BLACK));
    }
    draw_wave(ix, iw, 2, (int)(f / 3), LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK),
              LAP_ATTR(LAP_BLUE | LAP_BRIGHT, LAP_BLACK));
    /* linha de perigo no fundo */
    lap_hline(ix, rows - 1, iw, LAP_SHADE_50, LAP_ATTR(LAP_RED, LAP_BLACK));
    draw_bubbles(ix, iw, 3, rows - 2);

    /* rastro da bola */
    if (state == STATE_PLAYING) {
        static const uint8_t tr_ch[TRAIL_LEN] = {'o', '+', '.'};
        for (int i = TRAIL_LEN - 1; i >= 1; i--) {
            uint8_t col = (i == 1) ? (LAP_YELLOW) : (LAP_RED | LAP_BRIGHT);
            if (i == 2) col = LAP_RED;
            lap_put_cell(trail_x[i], trail_y[i], tr_ch[i - 1], LAP_ATTR(col, LAP_BLACK));
        }
    }

    /* tijolos */
    int inner_w = arena_w - 2;
    int brick_w = (inner_w / BRICK_COLS) < 2 ? 2 : (inner_w / BRICK_COLS);
    int start_x = arena_x + 1 + (inner_w - (brick_w * BRICK_COLS)) / 2;

    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            uint8_t hp = bricks[r][c];
            if (hp > 0) draw_brick(start_x + c * brick_w, BRICK_TOP + r * brick_h, brick_w - 1, brick_h, hp, r);
        }
    }

    /* nome da fase ao comecar */
    if (state == STATE_PLAYING && banner_timer > 0) {
        char b[32];
        snprintf(b, sizeof(b), "FASE %02d - %s", stage, stage_names[stage - 1]);
        put_center(ix, iw, BRICK_TOP + BRICK_ROWS * brick_h + 1, b, LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK));
    }

    draw_paddle();
    if (state == STATE_PLAYING)
        lap_put_cell((int)ball_x, (int)ball_y, 'O', LAP_ATTR(LAP_YELLOW | LAP_BRIGHT, LAP_BLACK));

    draw_parts();
    draw_hud();

    /* paineis de fim de fase / fim de jogo */
    if (state == STATE_STAGE_CLEAR) {
        char ttl[16], msg[24];
        int px, pw, py;
        snprintf(ttl, sizeof(ttl), " FASE %02d ", stage);
        panel(ttl, LAP_ATTR(LAP_CYAN | LAP_BRIGHT, LAP_BLACK), &px, &pw, &py);
        snprintf(msg, sizeof(msg), "CONCLUIDA!");
        put_center(px + 1, pw - 2, py + 2, msg, LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_BLACK));
    } else if (state == STATE_GAMEOVER || state == STATE_VICTORY) {
        bool win = (state == STATE_VICTORY);
        char msg[24];
        int px, pw, py;
        panel(win ? " VITORIA! " : " GAME OVER ",
              LAP_ATTR(win ? (LAP_GREEN | LAP_BRIGHT) : (LAP_RED | LAP_BRIGHT), LAP_BLACK),
              &px, &pw, &py);
        snprintf(msg, sizeof(msg), "PONTOS %04d", score);
        put_center(px + 1, pw - 2, py + 1, msg, LAP_ATTR(LAP_YELLOW | LAP_BRIGHT, LAP_BLACK));
        if (state_timer == 0 && ((f / 10) % 2) == 0)
            put_center(px + 1, pw - 2, py + 3, "TOQUE P/ JOGAR", LAP_ATTR(LAP_WHITE | LAP_BRIGHT, LAP_BLACK));
    }
}

static void draw(void) {
    if (lap_cols() != cols || lap_rows() != rows) {
        compute_layout();
        if (state != STATE_TITLE) {
            if (ball_x < arena_x + 1) ball_x = (float)(arena_x + 1);
            if (ball_x > arena_x + arena_w - 2) ball_x = (float)(arena_x + arena_w - 2);
            if (ball_y > rows - 2) ball_y = (float)(rows - 3);
        }
    }
    lap_clear(LAP_ATTR(LAP_WHITE, LAP_BLACK));
    if (cols <= rows) {
        lap_put_str((cols - 16) / 2, rows / 2, "GIRE P/ PAISAGEM", LAP_ATTR(LAP_YELLOW | LAP_BRIGHT, LAP_BLACK));
        return;
    }
    lap_hold_rotation(true);
    if (state == STATE_TITLE) handle_title();
    else { update_playing(); draw_playing(); }
}

static bool key(lap_key_t k, char ch) {
    (void)ch;
    if (state == STATE_TITLE) {
        if (k == LAP_KEY_ENTER) {
            start_match();
            return true;
        }
        return false;
    }
    int min_px = arena_x + 1, max_px = arena_x + arena_w - PADDLE_W - 1;
    if (k == LAP_KEY_LEFT) {
        paddle_x = (paddle_x - 2 < min_px) ? min_px : paddle_x - 2;
        return true;
    }
    if (k == LAP_KEY_RIGHT) {
        paddle_x = (paddle_x + 2 > max_px) ? max_px : paddle_x + 2;
        return true;
    }
    if (k == LAP_KEY_ENTER && state_timer == 0 && (state == STATE_GAMEOVER || state == STATE_VICTORY)) {
        start_match();
        return true;
    }
    return false;
}

LAP_GROUP(LAP_GROUP_USER);
LAP_APP(
    .id = "fishnoid", .name = "FISHNOID", .sub = "22/11/2026",
    .version = "2.0", .author = "wmo586 & Gemini", .icon = LAP_ICON_SHARK,
    .color = LAP_CYAN, .enter = enter, .leave = leave, .draw = draw, .key = key
);
