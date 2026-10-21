// BEAT BOUT DS - ритм-игра в духе FNF для Nintendo DS.
// Вся графика (персонажи, фоны, шрифт) нарисована кодом, музыка играется
// на PSG-каналах DS (чиптюн) и генерируется прямо в игре, без внешних файлов.
//
// Управление (меняется в меню, строка CONTROLS):
//   STYLUS  - тап стилусом по дорожке на нижнем экране
//   BUTTONS - влево = Y, вниз = D-pad вниз, вверх = D-pad вверх, вправо = A
// START во время игры - выйти в меню.

#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SW 256
#define SH 192
#define LANES 4
#define LANE_W 64
#define HIT_Y 150
#define APPROACH_MS 1400
#define WIN_SICK 60
#define WIN_GOOD 110
#define WIN_BAD 160
#define COUNTIN 3000
#define MAX_NOTES 320
#define NUM_LEVELS 4

typedef u16 px;
#define RGB(r, g, b) (RGB15((r), (g), (b)) | BIT(15))

// ---------------------------------------------------------------------------
// Буферы кадра: рисуем в ОЗУ, потом копируем в VRAM после vblank
// ---------------------------------------------------------------------------
static px top_buf[SW * SH] __attribute__((aligned(32)));
static px bot_buf[SW * SH] __attribute__((aligned(32)));
static px *vram_top, *vram_bot;
static px *cb; // текущий буфер для рисования

// ---------------------------------------------------------------------------
// Часы в миллисекундах (аппаратные таймеры)
// ---------------------------------------------------------------------------
static void clock_init(void) {
    TIMER0_DATA = 0;
    TIMER1_DATA = 0;
    TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1024;
    TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE;
}

static u32 clock_ms(void) {
    u32 hi1, hi2, lo;
    do {
        hi1 = TIMER1_DATA;
        lo = TIMER0_DATA;
        hi2 = TIMER1_DATA;
    } while (hi1 != hi2);
    u32 ticks = (hi1 << 16) | lo;
    return (ticks / 32728u) * 1000u + ((ticks % 32728u) * 1000u) / 32728u;
}

static u32 rng_state = 1;
static u32 rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

// ---------------------------------------------------------------------------
// Примитивы рисования
// ---------------------------------------------------------------------------
static inline void putpx(int x, int y, px c) {
    if ((unsigned)x < SW && (unsigned)y < SH) cb[y * SW + x] = c;
}

static void rect(int x, int y, int w, int h, px c) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > SW ? SW : x + w, y1 = y + h > SH ? SH : y + h;
    for (int j = y0; j < y1; j++) {
        px *row = cb + j * SW;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}

static void fill_row(int y, px c) {
    u32 v = c | ((u32)c << 16);
    u32 *p = (u32 *)(cb + y * SW);
    for (int i = 0; i < SW / 2; i++) p[i] = v;
}

static px mix(px a, px b, int t) { // t: 0..256
    int ar = a & 31, ag = (a >> 5) & 31, ab = (a >> 10) & 31;
    int br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31;
    int r = ar + (((br - ar) * t) >> 8);
    int g = ag + (((bg - ag) * t) >> 8);
    int bl = ab + (((bb - ab) * t) >> 8);
    return RGB(r, g, bl);
}

static void circle(int cx, int cy, int r, px c) {
    for (int dy = -r; dy <= r; dy++) {
        int yy = cy + dy;
        if (yy < 0 || yy >= SH) continue;
        int w2 = r * r - dy * dy, dx = 0;
        while ((dx + 1) * (dx + 1) <= w2) dx++;
        rect(cx - dx, yy, 2 * dx + 1, 1, c);
    }
}

// Стрелка. dir: 0 влево, 1 вниз, 2 вверх, 3 вправо. s - половина размера.
static void arrow(int cx, int cy, int dir, int s, px c) {
    for (int v = -s; v <= s; v++) {
        for (int u = -s; u <= s; u++) {
            int au = u < 0 ? -u : u;
            int in = (v <= 0) ? (au <= v + s) : (au <= s / 3);
            if (!in) continue;
            switch (dir) {
                case 0: putpx(cx + v, cy + u, c); break;
                case 1: putpx(cx + u, cy - v, c); break;
                case 2: putpx(cx + u, cy + v, c); break;
                default: putpx(cx - v, cy + u, c); break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Шрифт 5x7
// ---------------------------------------------------------------------------
static const char GLYPHS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:!-/%?<>+ ";
static const u8 FONT[47][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // .
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // :
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // !
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, // /
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // %
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // пробел
};

static void text(int x, int y, const char *s, px c, int sc) {
    for (; *s; s++) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        const char *p = strchr(GLYPHS, ch);
        if (!p || ch == 0) { x += 6 * sc; continue; }
        int gi = (int)(p - GLYPHS);
        for (int r = 0; r < 7; r++) {
            u8 bits = FONT[gi][r];
            for (int b = 0; b < 5; b++)
                if (bits & (16 >> b)) rect(x + b * sc, y + r * sc, sc, sc, c);
        }
        x += 6 * sc;
    }
}

static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }

static void text_c(int cx, int y, const char *s, px c, int sc) {
    text(cx - text_w(s, sc) / 2, y, s, c, sc);
}

static void text_sh(int cx, int y, const char *s, px c, int sc) { // по центру с тенью
    text(cx - text_w(s, sc) / 2 + sc, y + sc, s, RGB(1, 1, 3), sc);
    text_c(cx, y, s, c, sc);
}

// ---------------------------------------------------------------------------
// Уровни
// ---------------------------------------------------------------------------
typedef struct {
    const char *name;
    int bpm, root;
    const u8 *scale;
    int stride, extra, sections;
    u32 seed;
    px sky_a, sky_b, ground, accent;
    int style;
} Level;

static const u8 SC_MAJ[5] = {0, 2, 4, 7, 9};
static const u8 SC_MIN[5] = {0, 3, 5, 7, 10};
static const int LANE_DEG[4] = {0, 2, 3, 4};
static const int PROG[4] = {0, 5, 3, 7};

static const Level LEVELS[NUM_LEVELS] = {
    {"FIRST STEPS", 90, 60, SC_MAJ, 2, 0, 8, 11u,
     RGB(2, 2, 10), RGB(9, 7, 20), RGB(4, 10, 6), RGB(24, 24, 8), 0},
    {"NEON CITY", 110, 62, SC_MIN, 2, 25, 10, 23u,
     RGB(5, 2, 12), RGB(26, 6, 18), RGB(3, 3, 8), RGB(0, 28, 28), 1},
    {"GEAR RUSH", 130, 57, SC_MIN, 2, 45, 10, 37u,
     RGB(10, 8, 8), RGB(26, 14, 6), RGB(6, 6, 7), RGB(31, 18, 2), 2},
    {"SUNSET SHOWDOWN", 150, 65, SC_MAJ, 2, 60, 12, 51u,
     RGB(9, 3, 16), RGB(31, 15, 5), RGB(5, 2, 8), RGB(31, 24, 6), 3},
};

static const px LANE_COL[4] = {RGB(22, 8, 26), RGB(4, 24, 30), RGB(6, 28, 8), RGB(31, 6, 6)};

// ---------------------------------------------------------------------------
// Звук (PSG и шум)
// ---------------------------------------------------------------------------
static int snd_on = 0; // звук по умолчанию выключен (включается в меню)
#define DUTY_25 1
#define DUTY_50 3
static int kill_on[32];
static u32 kill_t[32];

static int midi_freq(int m) {
    static const u16 base[12] = {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494};
    int oct = m / 12 - 5;
    int f = base[m % 12];
    if (oct >= 0) f <<= oct; else f >>= -oct;
    return f;
}

static void track(int ch, int dur, u32 now) {
    if (ch >= 0 && ch < 32) { kill_on[ch] = 1; kill_t[ch] = now + (u32)dur; }
}

static void tone(int midi, int dur, int vol, int duty, u32 now) {
    if (!snd_on) return;
    track(soundPlayPSG(duty, midi_freq(midi), vol, 64), dur, now);
}

static void noise(int freq, int vol, int dur, u32 now) {
    if (!snd_on) return;
    track(soundPlayNoise(freq, vol, 64), dur, now);
}

static void sound_update(u32 now) {
    for (int c = 0; c < 32; c++)
        if (kill_on[c] && now >= kill_t[c]) { soundKill(c); kill_on[c] = 0; }
}

static void sound_stop_all(void) {
    for (int c = 0; c < 32; c++)
        if (kill_on[c]) { soundKill(c); kill_on[c] = 0; }
}

// ---------------------------------------------------------------------------
// Состояние игры
// ---------------------------------------------------------------------------
enum { ST_MENU, ST_PLAY, ST_RESULT };

typedef struct { int t; u8 lane, who, state, midi; } Note;

static Note notes[MAX_NOTES];
static int n_notes, player_total, song_end;
static int state = ST_MENU, sel = 0, cur_level = 0, ctrl_mode = 0;
static u32 start_abs;
static int score, combo, max_combo, health;
static int cnt_sick, cnt_good, cnt_bad, cnt_miss;
static int won;
static int best[NUM_LEVELS];
static int enemy_pose, player_pose;
static int enemy_pose_until, player_pose_until;
static const char *judge_txt = "";
static px judge_col;
static int judge_until;
static int lane_flash[LANES];
static int next_step;

static int step_ms(const Level *L, int step) { return (step * 30000) / L->bpm; }

static void build_chart(const Level *L) {
    rng_state = L->seed;
    n_notes = 0;
    player_total = 0;
    int last = -1;
    for (int s = 0; s < L->sections; s++) {
        int who = s & 1;
        int off = PROG[s & 3];
        for (int i = 0; i < 14; i++) {
            int on = (i % L->stride) == 0;
            if (!on && (int)(rnd() % 100) < L->extra) on = 1;
            if (on && L->extra > 0 && (i % L->stride) == 0 && (int)(rnd() % 100) < 8) on = 0;
            if (!on || n_notes >= MAX_NOTES) continue;
            int lane = rnd() & 3;
            if (lane == last && (rnd() % 3) != 0) lane = (lane + 1 + (int)(rnd() % 3)) & 3;
            last = lane;
            int m = L->root + off + L->scale[LANE_DEG[lane]] + ((rnd() % 4) == 0 ? 12 : 0);
            Note *n = &notes[n_notes++];
            n->t = step_ms(L, s * 16 + i);
            n->lane = (u8)lane;
            n->who = (u8)who;
            n->state = 0;
            n->midi = (u8)m;
            if (who) player_total++;
        }
    }
    song_end = step_ms(L, L->sections * 16) + 1500;
}

static void play_step(const Level *L, int step, u32 now) {
    int i = step & 15, s = step >> 4;
    int off = PROG[s & 3];
    int beat = step_ms(L, 2);
    if ((i & 3) == 0) {
        int b = L->root - 12 + off + (i == 8 ? 7 : 0);
        tone(b, beat * 3 / 2, 55, DUTY_50, now);
    }
    if ((i & 7) == 0) noise(1500, 100, 70, now);
    else if ((i & 7) == 4) noise(7000, 80, 90, now);
    else if (i & 1) noise(12000, 40, 25, now);
}

static void play_start(int lv) {
    cur_level = lv;
    const Level *L = &LEVELS[lv];
    build_chart(L);
    score = combo = max_combo = 0;
    health = 50;
    cnt_sick = cnt_good = cnt_bad = cnt_miss = 0;
    enemy_pose = player_pose = 0;
    enemy_pose_until = player_pose_until = 0;
    judge_until = 0;
    memset(lane_flash, 0, sizeof(lane_flash));
    next_step = 0;
    start_abs = clock_ms();
    sound_stop_all();
    state = ST_PLAY;
}

static void play_end(int win) {
    sound_stop_all();
    won = win;
    if (win && score > best[cur_level]) best[cur_level] = score;
    state = ST_RESULT;
}

static void set_judge(const char *t, px c, int now_t) {
    judge_txt = t; judge_col = c; judge_until = now_t + 600;
}

static void register_hit(Note *n, int adt, int t, u32 now) {
    const Level *L = &LEVELS[cur_level];
    n->state = 1;
    if (adt <= WIN_SICK) { score += 350; cnt_sick++; health += 4; set_judge("SICK!!", RGB(8, 28, 31), t); }
    else if (adt <= WIN_GOOD) { score += 200; cnt_good++; health += 2; set_judge("GOOD", RGB(8, 31, 8), t); }
    else { score += 50; cnt_bad++; health -= 1; set_judge("BAD", RGB(31, 20, 4), t); }
    if (health > 100) health = 100;
    combo++;
    if (combo > max_combo) max_combo = combo;
    if (combo >= 10) score += combo / 10;
    player_pose = n->lane + 1;
    player_pose_until = t + 250;
    tone(n->midi, step_ms(L, 2) * 8 / 10, 85, DUTY_25, now);
}

static void register_miss(Note *n, int t) {
    n->state = 2;
    cnt_miss++;
    combo = 0;
    health -= 8;
    player_pose = 5;
    player_pose_until = t + 350;
    set_judge("MISS", RGB(31, 6, 6), t);
}

static void try_hit(int lane, int t, u32 now) {
    lane_flash[lane] = t + 120;
    int best_i = -1, best_d = WIN_BAD + 1;
    for (int i = 0; i < n_notes; i++) {
        Note *n = &notes[i];
        if (n->who != 1 || n->state != 0 || n->lane != lane) continue;
        int dt = n->t - t;
        if (dt > WIN_BAD) break;
        int adt = dt < 0 ? -dt : dt;
        if (adt < best_d) { best_d = adt; best_i = i; }
    }
    if (best_i >= 0) register_hit(&notes[best_i], best_d, t, now);
}

static void play_update(u32 now, u32 down, const touchPosition *tp) {
    const Level *L = &LEVELS[cur_level];
    int t = (int)(now - start_abs) - COUNTIN;

    if (down & KEY_START) { sound_stop_all(); state = ST_MENU; return; }

    if (t >= 0) {
        if (ctrl_mode == 0) {
            if ((down & KEY_TOUCH) && tp) {
                int lane = tp->px / LANE_W;
                if (lane >= 0 && lane < LANES) try_hit(lane, t, now);
            }
        } else {
            if (down & KEY_Y) try_hit(0, t, now);
            if (down & KEY_DOWN) try_hit(1, t, now);
            if (down & KEY_UP) try_hit(2, t, now);
            if (down & KEY_A) try_hit(3, t, now);
        }

        // подложка: бас и ударные
        int total = L->sections * 16;
        while (next_step < total) {
            int st = step_ms(L, next_step);
            if (st > t) break;
            if (t - st < 250) play_step(L, next_step, now);
            next_step++;
        }

        // ноты врага играются сами, ноты игрока засчитываются как промахи
        for (int i = 0; i < n_notes; i++) {
            Note *n = &notes[i];
            if (n->state != 0) continue;
            if (n->who == 0) {
                if (n->t <= t) {
                    n->state = 2;
                    enemy_pose = n->lane + 1;
                    enemy_pose_until = t + 250;
                    tone(n->midi, step_ms(L, 2) * 8 / 10, 85, DUTY_25, now);
                }
            } else if (t - n->t > WIN_BAD) {
                register_miss(n, t);
            }
        }
        if (health <= 0) { health = 0; play_end(0); return; }
        if (t > song_end) { play_end(1); return; }
    }
    if (enemy_pose && t > enemy_pose_until) enemy_pose = 0;
    if (player_pose && t > player_pose_until) player_pose = 0;
    sound_update(now);
}

// ---------------------------------------------------------------------------
// Персонажи
// ---------------------------------------------------------------------------
typedef struct { px body, dark, head, accent, eye; int robot; } Skin;
static const Skin SKIN_PLAYER = {RGB(5, 12, 28), RGB(2, 3, 9), RGB(30, 24, 19), RGB(31, 8, 8), RGB(1, 1, 3), 0};
static const Skin SKIN_ENEMY = {RGB(18, 18, 22), RGB(4, 4, 6), RGB(25, 25, 29), RGB(31, 16, 2), RGB(31, 4, 4), 1};

// pose: 0 покой, 1 влево, 2 вниз, 3 вверх, 4 вправо, 5 промах
static void draw_char(int cx, int fy, int pose, int bounce, const Skin *k) {
    const int u = 3;
    int legs_h = 3 * u;
    int body_h = 8 * u - bounce;
    int body_y = fy - legs_h - body_h;
    rect(cx - 4 * u, fy - legs_h, 3 * u, legs_h, k->dark);
    rect(cx + 1 * u, fy - legs_h, 3 * u, legs_h, k->dark);
    rect(cx - 5 * u, body_y, 10 * u, body_h, k->body);
    rect(cx - 5 * u, body_y + body_h - u, 10 * u, u, k->dark);

    int ay = body_y + u;
    px ac = k->body;
    switch (pose) {
        case 1:
            rect(cx - 13 * u, ay, 8 * u, 2 * u, ac);
            rect(cx - 13 * u, ay, 2 * u, 2 * u, k->head);
            rect(cx + 5 * u, ay, 2 * u, 6 * u, ac);
            break;
        case 4:
            rect(cx + 5 * u, ay, 8 * u, 2 * u, ac);
            rect(cx + 11 * u, ay, 2 * u, 2 * u, k->head);
            rect(cx - 7 * u, ay, 2 * u, 6 * u, ac);
            break;
        case 3:
            rect(cx - 7 * u, ay - 6 * u, 2 * u, 8 * u, ac);
            rect(cx + 5 * u, ay - 6 * u, 2 * u, 8 * u, ac);
            rect(cx - 7 * u, ay - 6 * u, 2 * u, 2 * u, k->head);
            rect(cx + 5 * u, ay - 6 * u, 2 * u, 2 * u, k->head);
            break;
        case 2:
            rect(cx - 8 * u, ay + 2 * u, 3 * u, 5 * u, ac);
            rect(cx + 5 * u, ay + 2 * u, 3 * u, 5 * u, ac);
            break;
        default:
            rect(cx - 7 * u, ay, 2 * u, 6 * u, ac);
            rect(cx + 5 * u, ay, 2 * u, 6 * u, ac);
            break;
    }

    int hx = cx - 5 * u, hy = body_y - 9 * u;
    if (pose == 2) hy += 2 * u;
    if (pose == 5) { hx += u; hy += u; }
    rect(hx, hy, 10 * u, 9 * u, k->head);

    if (pose == 5) {
        rect(hx + 2 * u, hy + 3 * u, u, u, k->eye);
        rect(hx + 3 * u, hy + 4 * u, u, u, k->eye);
        rect(hx + 3 * u, hy + 3 * u, u, u, k->eye);
        rect(hx + 2 * u, hy + 4 * u, u, u, k->eye);
        rect(hx + 6 * u, hy + 3 * u, 2 * u, 2 * u, k->eye);
    } else {
        int ey = hy + 3 * u;
        if (pose == 3) ey -= u;
        if (pose == 2) ey += u;
        if (k->robot) rect(hx + u, ey, 8 * u, 2 * u, k->eye);
        else { rect(hx + 2 * u, ey, 2 * u, 2 * u, k->eye); rect(hx + 6 * u, ey, 2 * u, 2 * u, k->eye); }
    }
    if (pose != 0 && pose != 5) rect(hx + 3 * u, hy + 6 * u, 4 * u, 2 * u, k->dark);
    else rect(hx + 3 * u, hy + 7 * u, 4 * u, u, k->dark);

    if (k->robot) {
        rect(cx - 1, hy - 4 * u, 3, 4 * u, k->dark);
        circle(cx, hy - 4 * u, 5, k->accent);
    } else {
        rect(hx - u, hy - u, 12 * u, 2 * u, k->accent);
        rect(hx + 6 * u, hy + u, 6 * u, u, k->accent);
    }
    if (pose >= 1 && pose <= 4) {
        int fy2 = hy - (k->robot ? 30 : 20);
        arrow(cx, fy2, pose - 1, 9, RGB(1, 1, 3));
        arrow(cx, fy2, pose - 1, 7, LANE_COL[pose - 1]);
    }
}

// ---------------------------------------------------------------------------
// Фон
// ---------------------------------------------------------------------------
static const int COS16[16] = {64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24, 0, 24, 45, 59};
static const int SIN16[16] = {0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24};

static int tri(int p, int period, int amp) {
    p %= period;
    if (p < 0) p += period;
    int d = p - period / 2;
    if (d < 0) d = -d;
    return amp - d * 2 * amp / period;
}

static void gear(int cx, int cy, int r, int ang, px c, px hole) {
    circle(cx, cy, r, c);
    for (int k = 0; k < 8; k++) {
        int a = (ang + 2 * k) & 15;
        circle(cx + COS16[a] * (r + 3) / 64, cy + SIN16[a] * (r + 3) / 64, 3, c);
    }
    circle(cx, cy, r / 3, hole);
}

static void draw_scene(const Level *L, u32 tms, int epose, int ppose, int pulse, int lost) {
    int flash = pulse / 4;
    for (int y = 0; y < 150; y++) {
        px c = mix(L->sky_a, L->sky_b, y * 256 / 150);
        if (flash) c = mix(c, RGB(31, 31, 31), flash);
        fill_row(y, c);
    }
    int scroll = (int)(tms / 40);
    switch (L->style) {
        case 0: {
            for (int i = 0; i < 28; i++) {
                int sx = (i * 89 + 17) & 255, sy = 28 + (i * 37) % 80;
                int tw = (int)(tms / 400 + i) & 3;
                px c = tw == 0 ? RGB(14, 14, 20) : RGB(30, 30, 31);
                putpx(sx, sy, c);
                if (tw == 3) { putpx(sx + 1, sy, c); putpx(sx, sy + 1, c); }
            }
            circle(205, 52, 13, RGB(30, 30, 24));
            circle(200, 48, 3, RGB(25, 25, 20));
            circle(210, 57, 2, RGB(25, 25, 20));
            for (int x = 0; x < SW; x++) {
                int y = 128 - tri(x + scroll / 4, 120, 26);
                rect(x, y, 1, 150 - y, RGB(4, 6, 14));
                int y2 = 140 - tri(x + scroll / 2, 70, 14);
                rect(x, y2, 1, 152 - y2, RGB(3, 8, 8));
            }
        } break;
        case 1: {
            int bw = 26, off = scroll / 3;
            for (int i = -1; i < 11; i++) {
                int idx = i + off / bw;
                int bx = i * bw - off % bw;
                u32 h = ((u32)idx * 2654435761u) >> 24;
                int bh = 40 + (int)(h % 70);
                int top = 150 - bh;
                rect(bx, top, bw - 2, bh, RGB(3, 3, 9));
                rect(bx, top, bw - 2, 2, RGB(8, 4, 14));
                for (int wy = top + 5; wy < 144; wy += 9)
                    for (int wx = bx + 3; wx < bx + bw - 6; wx += 7) {
                        int lit = ((idx * 7 + wy * 3 + wx) & 3) != 0;
                        if (((idx + (int)(tms / 900)) & 7) == 0 && (wy & 8)) lit = 0;
                        if (lit) rect(wx, wy, 3, 4, L->accent);
                    }
            }
        } break;
        case 2: {
            rect(0, 96, SW, 8, RGB(8, 8, 10));
            rect(0, 98, SW, 2, RGB(20, 12, 3));
            for (int c = 0; c < 3; c++) {
                int cx = 30 + c * 85;
                rect(cx, 70, 18, 80, RGB(6, 5, 6));
                rect(cx - 2, 66, 22, 6, RGB(10, 8, 8));
                for (int k = 0; k < 3; k++) {
                    int sy = 62 - (int)((tms / 30 + k * 16) % 48);
                    circle(cx + 9 + k * 2, sy, 5 + k, RGB(13, 12, 13));
                }
            }
            gear(52, 120, 18, (int)(tms / 90), RGB(7, 6, 7), RGB(3, 2, 3));
            gear(210, 112, 22, 15 - (int)((tms / 70) & 15), RGB(8, 7, 8), RGB(3, 2, 3));
        } break;
        default: {
            circle(128, 96, 34 + pulse / 60, RGB(31, 23, 6));
            for (int i = 0; i < 6; i++) {
                int y = 98 + i * 8;
                px sc = mix(L->sky_a, L->sky_b, y * 256 / 150);
                rect(80, y, 96, 1 + i / 2, sc);
            }
            for (int x = 0; x < SW; x++) {
                int y = 124 - tri(x + scroll / 6, 150, 34);
                rect(x, y, 1, 150 - y, RGB(13, 4, 12));
                int y2 = 138 - tri(x + scroll / 3, 90, 16);
                rect(x, y2, 1, 152 - y2, RGB(7, 2, 9));
            }
        } break;
    }
    rect(0, 150, SW, 42, L->ground);
    rect(0, 150, SW, 2, L->accent);
    px line_c = mix(L->ground, L->accent, 50);
    for (int x = -(scroll % 32); x < SW; x += 32) rect(x, 154, 1, 38, line_c);
    rect(0, 170, SW, 1, line_c);

    int bounce = pulse / 50;
    draw_char(62, 172, epose, bounce, &SKIN_ENEMY);
    draw_char(192, 172, lost ? 5 : ppose, bounce, &SKIN_PLAYER);
}

// ---------------------------------------------------------------------------
// Интерфейс
// ---------------------------------------------------------------------------
static int beat_pulse(const Level *L, u32 tms) {
    int beat = 60000 / L->bpm;
    int ph = (int)(tms % (u32)beat);
    return ph < 200 ? 200 - ph : 0;
}

static void draw_health(void) {
    rect(14, 5, 228, 12, RGB(1, 1, 3));
    rect(16, 7, 224, 8, RGB(24, 8, 4));
    int pw = health * 224 / 100;
    rect(240 - pw, 7, pw, 8, RGB(6, 14, 28));
    int bx = 240 - pw;
    if (bx < 24) bx = 24;
    if (bx > 232) bx = 232;
    circle(bx - 8, 11, 7, SKIN_ENEMY.head);
    rect(bx - 12, 9, 8, 2, SKIN_ENEMY.eye);
    circle(bx + 8, 11, 7, SKIN_PLAYER.head);
    rect(bx + 3, 6, 11, 3, SKIN_PLAYER.accent);
    rect(bx + 5, 11, 2, 2, SKIN_PLAYER.eye);
    rect(bx + 10, 11, 2, 2, SKIN_PLAYER.eye);
}

static void render_play_top(const Level *L, int t) {
    int pulse = beat_pulse(L, (u32)(t < 0 ? t + 600000 : t));
    draw_scene(L, (u32)(t + COUNTIN), enemy_pose, player_pose, pulse, 0);
    draw_health();
    char buf[48];
    sprintf(buf, "SCORE %d", score);
    text(16, 22, buf, RGB(31, 31, 31), 1);
    if (combo > 1) {
        sprintf(buf, "COMBO %d", combo);
        text(240 - text_w(buf, 1), 22, buf, RGB(31, 28, 6), 1);
    }
    int secms = 480000 / L->bpm;
    if (t < 0) {
        int n = (-t) / 1000 + 1;
        if (n > 3) n = 3;
        sprintf(buf, "%d", n);
        text_sh(128, 50, buf, RGB(31, 31, 31), 8);
    } else {
        int who = (t / secms) & 1;
        int into = t % secms;
        if (who == 1) text_c(128, 34, "YOUR TURN!", L->accent, 1);
        else if (into > secms - 900) text_c(128, 34, "GET READY!", RGB(31, 31, 31), 1);
        else text_c(128, 34, "LISTEN...", RGB(16, 16, 20), 1);
        if (t < judge_until) text_sh(192, 62, judge_txt, judge_col, 2);
    }
}

static void render_play_bottom(const Level *L, int t) {
    int secms = 480000 / L->bpm;
    int who = t < 0 ? 0 : ((t / secms) & 1);
    for (int y = 0; y < SH; y++)
        fill_row(y, who ? mix(RGB(3, 3, 8), RGB(9, 4, 14), y * 256 / SH) : mix(RGB(2, 2, 5), RGB(4, 3, 7), y * 256 / SH));
    for (int l = 0; l < LANES; l++) {
        int x = l * LANE_W;
        if (who) rect(x + 1, 0, LANE_W - 1, SH, mix(RGB(4, 4, 9), LANE_COL[l], 28));
        rect(x, 0, 1, SH, RGB(10, 10, 14));
        int cx = x + LANE_W / 2;
        int fl = t < lane_flash[l];
        arrow(cx, HIT_Y, l, fl ? 19 : 17, RGB(2, 2, 4));
        arrow(cx, HIT_Y, l, fl ? 17 : 15, fl ? RGB(31, 31, 31) : mix(RGB(10, 10, 12), LANE_COL[l], 70));
        const char *lab = ctrl_mode == 0 ? "TAP" : (l == 0 ? "Y" : l == 1 ? "DOWN" : l == 2 ? "UP" : "A");
        text_c(cx, 177, lab, mix(RGB(10, 10, 12), LANE_COL[l], 120), 1);
    }
    for (int i = 0; i < n_notes; i++) {
        const Note *n = &notes[i];
        if (n->who != 1 || n->state != 0) continue;
        int dt = n->t - t;
        if (dt > APPROACH_MS) break;
        int y = HIT_Y - (dt * (HIT_Y + 10)) / APPROACH_MS;
        int cx = n->lane * LANE_W + LANE_W / 2;
        arrow(cx, y, n->lane, 17, RGB(2, 2, 4));
        arrow(cx, y, n->lane, 15, LANE_COL[n->lane]);
    }
    if (t < 0) text_c(128, 80, "GET READY", RGB(20, 20, 26), 2);
    else if (!who) text_c(128, 80, "LISTEN...", RGB(14, 14, 18), 2);
    if (t > 0 && song_end > 0) rect(0, 0, t * SW / song_end > SW ? SW : t * SW / song_end, 3, L->accent);
}

static void render_menu_top(u32 now) {
    const Level *L = &LEVELS[sel < NUM_LEVELS ? sel : cur_level];
    draw_scene(L, now, 0, 0, beat_pulse(L, now), 0);
    text_sh(128, 14, "BEAT BOUT", RGB(31, 31, 31), 4);
    text_sh(128, 50, "DS EDITION", L->accent, 2);
    char buf[48];
    sprintf(buf, "%s  BPM %d", L->name, L->bpm);
    text_sh(128, 76, buf, RGB(31, 31, 31), 1);
}

static void render_menu_bottom(void) {
    for (int y = 0; y < SH; y++) fill_row(y, mix(RGB(2, 2, 6), RGB(7, 3, 12), y * 256 / SH));
    char buf[64];
    for (int i = 0; i < NUM_LEVELS + 2; i++) {
        int y = 6 + i * 28;
        int on = (i == sel);
        px tc = on ? RGB(31, 31, 31) : RGB(18, 18, 22);
        rect(8, y, 240, 25, on ? RGB(8, 10, 24) : RGB(3, 3, 8));
        if (on) { rect(8, y, 240, 1, RGB(20, 24, 31)); rect(8, y + 24, 240, 1, RGB(20, 24, 31)); }
        if (i < NUM_LEVELS) {
            sprintf(buf, "%d %s", i + 1, LEVELS[i].name);
            text(16, y + 1, buf, tc, 2);
            sprintf(buf, "BPM %d   BEST %d", LEVELS[i].bpm, best[i]);
            text(16, y + 16, buf, LEVELS[i].accent, 1);
        } else if (i == NUM_LEVELS) {
            text(16, y + 1, "CONTROLS", tc, 2);
            text(16, y + 16, ctrl_mode == 0 ? "STYLUS: TAP THE LANES" : "BUTTONS: Y  DOWN  UP  A",
                 RGB(24, 24, 8), 1);
        } else {
            text(16, y + 1, snd_on ? "SOUND: ON" : "SOUND: OFF", tc, 2);
            text(16, y + 16, snd_on ? "CHIPTUNE MUSIC (OFF IF IT FREEZES)" : "SILENT SAFE MODE",
                 RGB(24, 24, 8), 1);
        }
    }
    text_c(128, 176, "D-PAD + A  OR  TAP", RGB(14, 14, 18), 1);
    text_c(128, 185, "START IN GAME: BACK TO MENU", RGB(9, 9, 12), 1);
}

static const char *rank_of(int acc) {
    return acc >= 95 ? "S" : acc >= 85 ? "A" : acc >= 70 ? "B" : acc >= 50 ? "C" : "D";
}

static int accuracy(void) {
    if (player_total <= 0) return 0;
    return (cnt_sick * 100 + cnt_good * 70 + cnt_bad * 40) / player_total;
}

static void render_result_top(u32 now) {
    const Level *L = &LEVELS[cur_level];
    draw_scene(L, now, 0, won ? 0 : 5, beat_pulse(L, now), !won);
    rect(24, 14, 208, 98, RGB(1, 1, 4));
    rect(24, 14, 208, 2, L->accent);
    text_c(128, 20, won ? "YOU WIN!" : "YOU LOST", won ? RGB(8, 31, 12) : RGB(31, 6, 6), 3);
    char buf[48];
    sprintf(buf, "SCORE %d", score);
    text_c(128, 52, buf, RGB(31, 31, 31), 2);
    int acc = accuracy();
    sprintf(buf, "ACC %d%%  RANK %s", acc, rank_of(acc));
    text_c(128, 72, buf, RGB(31, 28, 6), 1);
    sprintf(buf, "SICK %d GOOD %d BAD %d MISS %d", cnt_sick, cnt_good, cnt_bad, cnt_miss);
    text_c(128, 86, buf, RGB(20, 20, 26), 1);
    sprintf(buf, "MAX COMBO %d", max_combo);
    text_c(128, 98, buf, RGB(20, 20, 26), 1);
}

static void render_result_bottom(void) {
    for (int y = 0; y < SH; y++) fill_row(y, mix(RGB(2, 2, 6), RGB(7, 3, 12), y * 256 / SH));
    rect(12, 10, 232, 80, RGB(6, 12, 24));
    rect(12, 10, 232, 2, RGB(20, 26, 31));
    text_c(128, 34, "RETRY", RGB(31, 31, 31), 4);
    text_c(128, 72, "A  OR  TAP", RGB(16, 18, 24), 1);
    rect(12, 102, 232, 80, RGB(12, 6, 20));
    rect(12, 102, 232, 2, RGB(26, 20, 31));
    text_c(128, 126, "MENU", RGB(31, 31, 31), 4);
    text_c(128, 164, "B  OR  TAP", RGB(18, 14, 24), 1);
}

// ---------------------------------------------------------------------------
// Меню и главный цикл
// ---------------------------------------------------------------------------
static void menu_activate(int i) {
    if (i < NUM_LEVELS) play_start(i);
    else if (i == NUM_LEVELS) ctrl_mode ^= 1;
    else snd_on ^= 1;
}

static void menu_input(u32 down, const touchPosition *tp) {
    if (down & KEY_UP) sel = (sel + NUM_LEVELS + 1) % (NUM_LEVELS + 2);
    if (down & KEY_DOWN) sel = (sel + 1) % (NUM_LEVELS + 2);
    if (down & (KEY_LEFT | KEY_RIGHT)) {
        if (sel == NUM_LEVELS) ctrl_mode ^= 1;
        else if (sel == NUM_LEVELS + 1) snd_on ^= 1;
    }
    if (down & KEY_A) menu_activate(sel);
    if ((down & KEY_TOUCH) && tp) {
        int row = (tp->py - 6) / 28;
        if (tp->py >= 6 && row >= 0 && row <= NUM_LEVELS + 1) { sel = row; menu_activate(row); }
    }
}

static void result_input(u32 down, const touchPosition *tp) {
    if (down & KEY_A) play_start(cur_level);
    else if (down & KEY_B) state = ST_MENU;
    else if ((down & KEY_TOUCH) && tp) {
        if (tp->py < 96) play_start(cur_level);
        else state = ST_MENU;
    }
}

int main(void) {
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    int bgm = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_top = (px *)bgGetGfxPtr(bgm);

    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_bot = (px *)bgGetGfxPtr(bgs);

    soundEnable();
    clock_init();

    touchPosition tp;
    memset(&tp, 0, sizeof(tp));

    while (1) {
        scanKeys();
        u32 down = keysDown();
        if (down & KEY_TOUCH) touchRead(&tp);
        u32 now = clock_ms();

        if (state == ST_MENU) menu_input(down, &tp);
        else if (state == ST_PLAY) play_update(now, down, &tp);
        else result_input(down, &tp);

        if (state == ST_MENU) {
            cb = top_buf; render_menu_top(now);
            cb = bot_buf; render_menu_bottom();
        } else if (state == ST_PLAY) {
            const Level *L = &LEVELS[cur_level];
            int t = (int)(now - start_abs) - COUNTIN;
            cb = top_buf; render_play_top(L, t);
            cb = bot_buf; render_play_bottom(L, t);
        } else {
            cb = top_buf; render_result_top(now);
            cb = bot_buf; render_result_bottom();
        }

        swiWaitForVBlank();
        DC_FlushRange(top_buf, sizeof(top_buf));
        DC_FlushRange(bot_buf, sizeof(bot_buf));
        dmaCopy(top_buf, vram_top, SW * SH * sizeof(px));
        dmaCopy(bot_buf, vram_bot, SW * SH * sizeof(px));
    }
    return 0;
}
