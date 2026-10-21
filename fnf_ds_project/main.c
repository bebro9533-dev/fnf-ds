// FNF-DS lite, шаг 1: ноты + стилус, без звука и спрайтов.
// Нижний экран (тач): 4 дорожки, тапаешь стилусом по дорожке, когда нота доходит до линии.
// Верхний экран: счёт, комбо, оценка попадания.

#include <nds.h>
#include <stdio.h>

#define LANES        4
#define LANE_W       64
#define HIT_Y        160
#define NOTE_H       12
#define APPROACH_MS  1500   // за сколько мс нота долетает сверху до линии
#define WIN_SICK     70
#define WIN_GOOD     120
#define WIN_BAD      170
#define MAX_NOTES    256

typedef struct {
    int time_ms;
    int lane;
    int state;      // 0 - ждёт, 1 - сыграна, 2 - пропущена
} Note;

static Note notes[MAX_NOTES];
static int note_count = 0;

static u16 *fb;
static int score = 0, combo = 0, max_combo = 0;
static int sick = 0, good = 0, bad = 0, miss = 0;
static const char *last_judge = "";

static const u16 lane_colors[LANES] = {
    RGB15(20, 8, 24) | BIT(15),   // лево
    RGB15(6, 24, 28) | BIT(15),   // низ
    RGB15(8, 28, 8)  | BIT(15),   // верх
    RGB15(30, 8, 8)  | BIT(15)    // право
};

// Тестовый чарт: 120 BPM, нота каждые полбита, дорожки по простому паттерну.
static void build_test_chart(void) {
    static const int pattern[] = {0, 1, 2, 3, 0, 2, 1, 3, 3, 2, 1, 0, 0, 0, 3, 3};
    int step_ms = 250;
    int start_ms = 3000;
    note_count = 0;
    for (int i = 0; i < 64 && note_count < MAX_NOTES; i++) {
        notes[note_count].time_ms = start_ms + i * step_ms;
        notes[note_count].lane = pattern[i % 16];
        notes[note_count].state = 0;
        note_count++;
    }
}

static void fill_rect(int x, int y, int w, int h, u16 color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= 192) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= 256) continue;
            fb[j * 256 + i] = color;
        }
    }
}

static void draw_bottom(int now_ms) {
    fill_rect(0, 0, 256, 192, RGB15(2, 2, 4) | BIT(15));

    for (int l = 0; l < LANES; l++) {
        // разделители дорожек
        fill_rect(l * LANE_W, 0, 1, 192, RGB15(8, 8, 10) | BIT(15));
        // рамка зоны попадания
        fill_rect(l * LANE_W + 4, HIT_Y, LANE_W - 8, 2, lane_colors[l]);
    }

    for (int i = 0; i < note_count; i++) {
        Note *n = &notes[i];
        if (n->state != 0) continue;
        int dt = n->time_ms - now_ms;          // сколько мс до попадания
        if (dt > APPROACH_MS) break;           // чарт отсортирован по времени
        int y = HIT_Y - (dt * HIT_Y) / APPROACH_MS;
        fill_rect(n->lane * LANE_W + 8, y - NOTE_H / 2, LANE_W - 16, NOTE_H, lane_colors[n->lane]);
    }
}

static void draw_top(int now_ms) {
    consoleClear();
    printf("\x1b[1;2HFNF DS lite");
    printf("\x1b[4;2HScore: %d", score);
    printf("\x1b[6;2HCombo: %d (max %d)", combo, max_combo);
    printf("\x1b[9;2H%s", last_judge);
    printf("\x1b[13;2HSick:%d Good:%d Bad:%d Miss:%d", sick, good, bad, miss);
    printf("\x1b[21;2Ht=%d ms", now_ms);
}

static void register_hit(int dt_abs, Note *n) {
    n->state = 1;
    if (dt_abs <= WIN_SICK)      { score += 350; sick++; last_judge = "SICK!!"; }
    else if (dt_abs <= WIN_GOOD) { score += 200; good++; last_judge = "Good"; }
    else                         { score += 50;  bad++;  last_judge = "Bad"; }
    combo++;
    if (combo > max_combo) max_combo = combo;
}

static void register_miss(Note *n) {
    n->state = 2;
    miss++;
    combo = 0;
    last_judge = "Miss";
}

static void handle_tap(int lane, int now_ms) {
    int best = -1, best_dt = WIN_BAD + 1;
    for (int i = 0; i < note_count; i++) {
        Note *n = &notes[i];
        if (n->state != 0 || n->lane != lane) continue;
        int dt = n->time_ms - now_ms;
        if (dt > WIN_BAD) break;
        int adt = dt < 0 ? -dt : dt;
        if (adt < best_dt) { best_dt = adt; best = i; }
    }
    if (best >= 0) register_hit(best_dt, &notes[best]);
    // тап мимо нот штрафа не даёт (ghost tapping)
}

int main(void) {
    // верхний экран: текст
    videoSetMode(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 0, true, true);

    // нижний экран: 16-битный фреймбуфер
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    int bg = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    fb = bgGetGfxPtr(bg);

    build_test_chart();

    int frames = 0;
    touchPosition touch;

    while (1) {
        scanKeys();
        int down = keysDown();
        int now_ms = (frames * 1000) / 60;

        if (down & KEY_TOUCH) {
            touchRead(&touch);
            int lane = touch.px / LANE_W;
            if (lane >= 0 && lane < LANES) handle_tap(lane, now_ms);
        }

        // автопропуск нот, которые уже улетели
        for (int i = 0; i < note_count; i++) {
            Note *n = &notes[i];
            if (n->state == 0 && now_ms - n->time_ms > WIN_BAD) register_miss(n);
        }

        swiWaitForVBlank();
        draw_bottom(now_ms);
        draw_top(now_ms);

        frames++;
    }
    return 0;
}
