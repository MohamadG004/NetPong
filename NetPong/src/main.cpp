/*
 * PONG — A complete C++ implementation using SDL2
 *
 * Features:
 *   - Single-player vs AI (three difficulty levels) or local 2-player
 *   - Smooth ball physics with increasing speed each rally
 *   - Spin: where the ball hits the paddle affects its rebound angle
 *   - Particle effects on paddle/wall hits
 *   - Score tracking with win screen (first to 7)
 *   - Menu, pause, and game-over screens
 *   - Procedural audio (beeps generated with SDL2_mixer / SDL2 audio)
 *   - Frame-rate independent movement via delta-time
 *
 * Controls (2-player):
 *   Player 1 (right) — W / S
 *   Player 2 (left)  — UP / DOWN
 *
 * Controls (1-player):
 *   Player 1 (right) — W / S   (AI controls the left paddle)
 *
 * General:
 *   ESC / P   — Pause
 *   R         — Restart (from pause or game-over)
 *   M         — Back to menu
 */

#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <random>

// ─────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────
static constexpr int   WIN_W        = 900;
static constexpr int   WIN_H        = 600;
static constexpr int   TARGET_FPS   = 120;
static constexpr float FRAME_MS     = 1000.f / TARGET_FPS;

static constexpr float PADDLE_W     = 14.f;
static constexpr float PADDLE_H     = 90.f;
static constexpr float PADDLE_SPEED = 480.f;   // px/s
static constexpr float PADDLE_MARGIN= 24.f;

static constexpr float BALL_SIZE    = 12.f;
static constexpr float BALL_INIT_SP = 320.f;   // px/s
static constexpr float BALL_MAX_SP  = 780.f;
static constexpr float SPEED_INC    = 22.f;    // per hit

static constexpr int   WIN_SCORE    = 7;

static constexpr int   AUDIO_FREQ   = 44100;
static constexpr int   AUDIO_SAMPLES= 512;

// ─────────────────────────────────────────────
//  Colour palette
// ─────────────────────────────────────────────
struct Col { Uint8 r,g,b,a; };
static constexpr Col C_BG      = {  10,  10,  20, 255 };
static constexpr Col C_WHITE   = { 255, 255, 255, 255 };
static constexpr Col C_DIM     = { 120, 120, 140, 255 };
static constexpr Col C_P1      = {  80, 180, 255, 255 };
static constexpr Col C_P2      = { 255, 110,  80, 255 };
static constexpr Col C_BALL    = { 240, 240, 255, 255 };
static constexpr Col C_NET     = {  50,  50,  70, 255 };
static constexpr Col C_FLASH   = { 255, 230,  60, 255 };

// ─────────────────────────────────────────────
//  Simple RNG wrapper
// ─────────────────────────────────────────────
static std::mt19937 rng( (unsigned)std::time(nullptr) );
static float frand(float lo, float hi) {
    return lo + (hi - lo) * (rng() / (float)rng.max());
}

// ─────────────────────────────────────────────
//  Procedural audio
// ─────────────────────────────────────────────
struct Beep {
    double freq    = 440.0;
    double volume  = 0.25;
    double duration= 0.06;   // seconds
    double phase   = 0.0;
    double elapsed = 0.0;
    bool   active  = false;
};

static const int      NUM_BEEPS = 4;
static Beep           g_beeps[NUM_BEEPS];
static SDL_AudioDeviceID g_audioDev = 0;
static int            g_audioFreq  = AUDIO_FREQ;

static void audioCallback(void* /*ud*/, Uint8* stream, int len) {
    float* out = (float*)stream;
    int samples = len / sizeof(float);
    std::memset(stream, 0, len);

    for (int b = 0; b < NUM_BEEPS; ++b) {
        Beep& bk = g_beeps[b];
        if (!bk.active) continue;
        for (int i = 0; i < samples; ++i) {
            if (bk.elapsed >= bk.duration) { bk.active = false; break; }
            float env = 1.f - (float)(bk.elapsed / bk.duration);   // linear decay
            float s = (float)(bk.volume * env * std::sin(2.0 * M_PI * bk.freq * bk.phase));
            out[i] += s;
            bk.phase   += 1.0 / g_audioFreq;
            bk.elapsed += 1.0 / g_audioFreq;
        }
    }
    // soft clip
    for (int i = 0; i < samples; ++i) {
        out[i] = std::max(-1.f, std::min(1.f, out[i]));
    }
}

static void playBeep(double freq, double dur, double vol = 0.3) {
    if (!g_audioDev) return;
    SDL_LockAudioDevice(g_audioDev);
    for (int b = 0; b < NUM_BEEPS; ++b) {
        if (!g_beeps[b].active) {
            g_beeps[b] = { freq, vol, dur, 0.0, 0.0, true };
            break;
        }
    }
    SDL_UnlockAudioDevice(g_audioDev);
}

// ─────────────────────────────────────────────
//  Particle system
// ─────────────────────────────────────────────
struct Particle {
    float x, y, vx, vy;
    float life, maxLife;
    Col   color;
};

static std::vector<Particle> g_particles;

static void spawnParticles(float x, float y, Col color, int n = 12) {
    for (int i = 0; i < n; ++i) {
        float angle = frand(0.f, 2.f * (float)M_PI);
        float speed = frand(40.f, 220.f);
        float life  = frand(0.25f, 0.55f);
        g_particles.push_back({
            x, y,
            std::cos(angle)*speed,
            std::sin(angle)*speed,
            life, life, color
        });
    }
}

static void updateParticles(float dt) {
    for (auto& p : g_particles) {
        p.x    += p.vx * dt;
        p.y    += p.vy * dt;
        p.vx   *= 0.93f;
        p.vy   *= 0.93f;
        p.life -= dt;
    }
    g_particles.erase(
        std::remove_if(g_particles.begin(), g_particles.end(),
                       [](const Particle& p){ return p.life <= 0.f; }),
        g_particles.end());
}

static void drawParticles(SDL_Renderer* ren) {
    for (const auto& p : g_particles) {
        float alpha = p.life / p.maxLife;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren,
            p.color.r, p.color.g, p.color.b,
            (Uint8)(alpha * 255));
        SDL_FRect r{ p.x - 2, p.y - 2, 4, 4 };
        SDL_RenderFillRectF(ren, &r);
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

// ─────────────────────────────────────────────
//  Drawing helpers
// ─────────────────────────────────────────────
static void setColor(SDL_Renderer* ren, Col c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
}

static void fillRect(SDL_Renderer* ren, float x, float y, float w, float h) {
    SDL_FRect r{ x, y, w, h };
    SDL_RenderFillRectF(ren, &r);
}

// ─────────────────────────────────────────────
//  Text rendering (SDL_ttf with embedded font)
//  We embed a minimal bitmap font so no font
//  file needs to ship.
// ─────────────────────────────────────────────

struct Font5x7 {
    static const int W = 5, H = 7;
    struct Glyph { Uint8 cols[5]; };
    static Glyph get(char c);
};

static const Font5x7::Glyph GLYPHS[] = {
    // ' '=32
    {{0x00,0x00,0x00,0x00,0x00}},
    // '!'=33
    {{0x00,0x00,0x5F,0x00,0x00}},
    // '"'=34
    {{0x00,0x07,0x00,0x07,0x00}},
    {{0x00,0x00,0x00,0x00,0x00}}, // #
    {{0x00,0x00,0x00,0x00,0x00}}, // $
    {{0x00,0x00,0x00,0x00,0x00}}, // %
    {{0x00,0x00,0x00,0x00,0x00}}, // &
    {{0x00,0x00,0x00,0x00,0x00}}, // '
    {{0x00,0x1C,0x22,0x41,0x00}}, // (
    {{0x00,0x41,0x22,0x1C,0x00}}, // )
    {{0x00,0x00,0x00,0x00,0x00}}, // *
    {{0x00,0x00,0x00,0x00,0x00}}, // +
    {{0x00,0x50,0x30,0x00,0x00}}, // ,
    {{0x00,0x08,0x08,0x08,0x00}}, // -
    {{0x00,0x60,0x60,0x00,0x00}}, // .
    {{0x00,0x00,0x00,0x00,0x00}}, // /
    // '0'=48
    {{0x3E,0x51,0x49,0x45,0x3E}},
    {{0x00,0x42,0x7F,0x40,0x00}},
    {{0x42,0x61,0x51,0x49,0x46}},
    {{0x21,0x41,0x45,0x4B,0x31}},
    {{0x18,0x14,0x12,0x7F,0x10}},
    {{0x27,0x45,0x45,0x45,0x39}},
    {{0x3C,0x4A,0x49,0x49,0x30}},
    {{0x01,0x71,0x09,0x05,0x03}},
    {{0x36,0x49,0x49,0x49,0x36}},
    {{0x06,0x49,0x49,0x29,0x1E}},
    // ':'=58
    {{0x00,0x36,0x36,0x00,0x00}},
    {{0x00,0x56,0x36,0x00,0x00}},
    {{0x08,0x14,0x22,0x41,0x00}}, // <
    {{0x14,0x14,0x14,0x14,0x14}}, // =
    {{0x00,0x41,0x22,0x14,0x08}}, // >
    {{0x02,0x01,0x51,0x09,0x06}}, // ?
    {{0x00,0x00,0x00,0x00,0x00}}, // @
    // 'A'=65
    {{0x7E,0x11,0x11,0x11,0x7E}},
    {{0x7F,0x49,0x49,0x49,0x36}},
    {{0x3E,0x41,0x41,0x41,0x22}},
    {{0x7F,0x41,0x41,0x22,0x1C}},
    {{0x7F,0x49,0x49,0x49,0x41}},
    {{0x7F,0x09,0x09,0x09,0x01}},
    {{0x3E,0x41,0x49,0x49,0x7A}},
    {{0x7F,0x08,0x08,0x08,0x7F}},
    {{0x00,0x41,0x7F,0x41,0x00}},
    {{0x20,0x40,0x41,0x3F,0x01}},
    {{0x7F,0x08,0x14,0x22,0x41}},
    {{0x7F,0x40,0x40,0x40,0x40}},
    {{0x7F,0x02,0x0C,0x02,0x7F}},
    {{0x7F,0x04,0x08,0x10,0x7F}},
    {{0x3E,0x41,0x41,0x41,0x3E}},
    {{0x7F,0x09,0x09,0x09,0x06}},
    {{0x3E,0x41,0x51,0x21,0x5E}},
    {{0x7F,0x09,0x19,0x29,0x46}},
    {{0x46,0x49,0x49,0x49,0x31}},
    {{0x01,0x01,0x7F,0x01,0x01}},
    {{0x3F,0x40,0x40,0x40,0x3F}},
    {{0x1F,0x20,0x40,0x20,0x1F}},
    {{0x3F,0x40,0x38,0x40,0x3F}},
    {{0x63,0x14,0x08,0x14,0x63}},
    {{0x07,0x08,0x70,0x08,0x07}},
    {{0x61,0x51,0x49,0x45,0x43}},
    // '['=91
    {{0x00,0x7F,0x41,0x41,0x00}},
    {{0x02,0x04,0x08,0x10,0x20}}, // backslash
    {{0x00,0x41,0x41,0x7F,0x00}},
    {{0x04,0x02,0x01,0x02,0x04}}, // ^
    {{0x40,0x40,0x40,0x40,0x40}}, // _
    {{0x00,0x01,0x02,0x04,0x00}}, // `
    // 'a'=97
    {{0x20,0x54,0x54,0x54,0x78}},
    {{0x7F,0x48,0x44,0x44,0x38}},
    {{0x38,0x44,0x44,0x44,0x20}},
    {{0x38,0x44,0x44,0x48,0x7F}},
    {{0x38,0x54,0x54,0x54,0x18}},
    {{0x08,0x7E,0x09,0x01,0x02}},
    {{0x0C,0x52,0x52,0x52,0x3E}},
    {{0x7F,0x08,0x04,0x04,0x78}},
    {{0x00,0x44,0x7D,0x40,0x00}},
    {{0x20,0x40,0x44,0x3D,0x00}},
    {{0x7F,0x10,0x28,0x44,0x00}},
    {{0x00,0x41,0x7F,0x40,0x00}},
    {{0x7C,0x04,0x18,0x04,0x78}},
    {{0x7C,0x08,0x04,0x04,0x78}},
    {{0x38,0x44,0x44,0x44,0x38}},
    {{0x7C,0x14,0x14,0x14,0x08}},
    {{0x08,0x14,0x14,0x18,0x7C}},
    {{0x7C,0x08,0x04,0x04,0x08}},
    {{0x48,0x54,0x54,0x54,0x20}},
    {{0x04,0x3F,0x44,0x40,0x20}},
    {{0x3C,0x40,0x40,0x20,0x7C}},
    {{0x1C,0x20,0x40,0x20,0x1C}},
    {{0x3C,0x40,0x30,0x40,0x3C}},
    {{0x44,0x28,0x10,0x28,0x44}},
    {{0x0C,0x50,0x50,0x50,0x3C}},
    {{0x44,0x64,0x54,0x4C,0x44}},
};

Font5x7::Glyph Font5x7::get(char c) {
    int idx = (int)(unsigned char)c - 32;
    int tableSize = (int)(sizeof(GLYPHS)/sizeof(GLYPHS[0]));
    if (idx < 0 || idx >= tableSize) return {{0,0,0,0,0}};
    return GLYPHS[idx];
}

static void drawChar(SDL_Renderer* ren, char c, int px, int py, int scale, Col col) {
    auto g = Font5x7::get(c);
    setColor(ren, col);
    for (int col5 = 0; col5 < Font5x7::W; ++col5) {
        Uint8 bits = g.cols[col5];
        for (int row = 0; row < Font5x7::H; ++row) {
            if (bits & (1 << row)) {
                SDL_Rect r{ px + col5*scale, py + row*scale, scale, scale };
                SDL_RenderFillRect(ren, &r);
            }
        }
    }
}

static void drawText(SDL_Renderer* ren, const std::string& s,
                     int x, int y, int scale, Col col,
                     bool centeredX = false)
{
    int totalW = (int)s.size() * (Font5x7::W + 1) * scale;
    int cx = centeredX ? x - totalW/2 : x;
    for (char c : s) {
        drawChar(ren, c, cx, y, scale, col);
        cx += (Font5x7::W + 1) * scale;
    }
}

// ─────────────────────────────────────────────
//  Game state
// ─────────────────────────────────────────────
enum class GameState { MENU, PLAYING, PAUSED, SCORE_FLASH, GAME_OVER };
enum class Mode      { ONE_PLAYER, TWO_PLAYER };
enum class Difficulty{ EASY, MEDIUM, HARD };

struct Paddle {
    float x, y;          // top-left
    float vy = 0.f;
    Col   color;
    int   score = 0;
};

struct Ball {
    float x, y;
    float vx, vy;
    float speed;
    float trailX[8], trailY[8];
    int   trailHead = 0;
};

struct Game {
    GameState  state      = GameState::MENU;
    Mode       mode       = Mode::ONE_PLAYER;
    Difficulty diff       = Difficulty::MEDIUM;

    // 'left'  = AI side  (1-player) or Player 2 (2-player)
    // 'right' = Player 1 always
    Paddle     left, right;
    Ball       ball;

    int        lastScorer = 0;    // 1=player1(right), 2=AI/P2(left)
    float      flashTimer = 0.f;
    float      countDown  = 0.f;  // pause before serve
    bool       serving    = true;

    // menu selection
    int        menuSel    = 0;    // 0=1P easy,1=1P med,2=1P hard,3=2P
    int        menuCount  = 4;

    // AI state
    float      aiTarget   = 0.f;
    float      aiError    = 0.f;  // reaction imperfection

    void init(Mode m, Difficulty d);
    void resetBall(int serverSide);
    void updateAI(float dt);
};

static void resetBallInto(Ball& b, int serverSide) {
    b.x  = WIN_W / 2.f;
    b.y  = WIN_H / 2.f;
    b.speed = BALL_INIT_SP;
    float angle = frand(-0.45f, 0.45f);
    float dir   = (serverSide == 1) ? 1.f : -1.f;
    b.vx = dir * b.speed * std::cos(angle);
    b.vy =       b.speed * std::sin(angle);
    for (int i = 0; i < 8; ++i) { b.trailX[i]=b.x; b.trailY[i]=b.y; }
    b.trailHead = 0;
}

void Game::init(Mode m, Difficulty d) {
    mode = m; diff = d;

    // Left paddle = AI (or P2 in 2-player)
    left.x      = PADDLE_MARGIN;
    left.y      = WIN_H/2.f - PADDLE_H/2.f;
    left.score  = 0;
    left.color  = C_P2;   // AI / P2 colour

    // Right paddle = Player 1
    right.x     = WIN_W - PADDLE_MARGIN - PADDLE_W;
    right.y     = WIN_H/2.f - PADDLE_H/2.f;
    right.score = 0;
    right.color = C_P1;   // Player colour

    resetBallInto(ball, 1);
    serving    = true;
    countDown  = 1.5f;
    flashTimer = 0.f;
    state      = GameState::PLAYING;
    g_particles.clear();
    aiError    = 0.f;
    aiTarget   = WIN_H / 2.f;
}

// ─────────────────────────────────────────────
//  AI logic — controls the LEFT paddle
// ─────────────────────────────────────────────
void Game::updateAI(float dt) {
    if (mode != Mode::ONE_PLAYER) return;

    float reactionHz;
    float errorMag;
    switch (diff) {
        case Difficulty::EASY:   reactionHz=1.5f; errorMag=80.f; break;
        case Difficulty::MEDIUM: reactionHz=3.f;  errorMag=35.f; break;
        case Difficulty::HARD:   reactionHz=6.f;  errorMag=10.f; break;
    }

    float desiredY = ball.y - PADDLE_H/2.f + aiError;
    float alpha = 1.f - std::exp(-reactionHz * dt);
    aiTarget = aiTarget + (desiredY - aiTarget) * alpha;

    static float errorTimer = 0.f;
    errorTimer -= dt;
    if (errorTimer <= 0.f) {
        aiError    = frand(-errorMag, errorMag);
        errorTimer = frand(0.4f, 1.2f);
    }

    // Move LEFT paddle toward target
    float centre = left.y + PADDLE_H/2.f;
    float diff_  = (aiTarget + PADDLE_H/2.f) - centre;
    float spd    = std::min(std::abs(diff_) / dt, PADDLE_SPEED);
    left.vy      = (diff_ > 0.f ? 1.f : -1.f) * spd;
}

// ─────────────────────────────────────────────
//  Net draw
// ─────────────────────────────────────────────
static void drawNet(SDL_Renderer* ren) {
    setColor(ren, C_NET);
    int dashH = 18, gap = 10;
    int x = WIN_W/2 - 1;
    for (int y = 0; y < WIN_H; y += dashH + gap) {
        SDL_Rect r{ x, y, 3, dashH };
        SDL_RenderFillRect(ren, &r);
    }
}

// ─────────────────────────────────────────────
//  Menu screen
// ─────────────────────────────────────────────
static void drawMenu(SDL_Renderer* ren, const Game& g) {
    setColor(ren, C_BG);
    SDL_RenderClear(ren);

    std::string title = "PONG";
    drawText(ren, title, WIN_W/2, 80, 10, C_WHITE, true);
    drawText(ren, "Press ENTER to select", WIN_W/2, 190, 2, C_DIM, true);

    struct Entry { std::string label; Col col; };
    Entry entries[] = {
        {"1 PLAYER  -  EASY",   C_P1},
        {"1 PLAYER  -  MEDIUM", C_P1},
        {"1 PLAYER  -  HARD",   C_P1},
        {"2 PLAYERS",           C_P2},
    };

    for (int i = 0; i < 4; ++i) {
        int y = 240 + i * 50;
        bool sel = (g.menuSel == i);
        Col col  = sel ? C_FLASH : entries[i].col;
        if (sel) {
            setColor(ren, {30,30,50,255});
            SDL_Rect bar{ WIN_W/2 - 160, y - 8, 320, 36 };
            SDL_RenderFillRect(ren, &bar);
            drawText(ren, ">", WIN_W/2 - 150, y + 3, 3, C_FLASH, false);
        }
        drawText(ren, entries[i].label, WIN_W/2, y + 4, 2, col, true);
    }

    drawText(ren, "1P: W/S or ARROWS   2P: left = W/S   right = ARROWS", WIN_W/2, WIN_H-40, 2, C_DIM, true);
}

// ─────────────────────────────────────────────
//  HUD
//  Left side = AI / P2 score & label
//  Right side = P1 score & label
// ─────────────────────────────────────────────
static void drawHUD(SDL_Renderer* ren, const Game& g) {
    // AI / P2 score on the left
    drawText(ren, std::to_string(g.left.score),
             WIN_W/2 - 60, 20, 7, C_P2, true);
    // Player 1 score on the right
    drawText(ren, std::to_string(g.right.score),
             WIN_W/2 + 60, 20, 7, C_P1, true);

    // Labels
    if (g.mode == Mode::ONE_PLAYER)
        drawText(ren, "AI", 60, 14, 2, C_DIM, true);
    else
        drawText(ren, "P2", 60, 14, 2, C_DIM, true);
    drawText(ren, "P1", WIN_W-60, 14, 2, C_DIM, true);

    drawText(ren, "ESC=PAUSE", WIN_W/2, WIN_H - 18, 1, C_NET, true);
}

// ─────────────────────────────────────────────
//  Pause overlay
// ─────────────────────────────────────────────
static void drawPause(SDL_Renderer* ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0,0,0,140);
    SDL_Rect full{0,0,WIN_W,WIN_H};
    SDL_RenderFillRect(ren, &full);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    drawText(ren, "PAUSED", WIN_W/2, WIN_H/2 - 50, 6, C_WHITE, true);
    drawText(ren, "ESC / P  -  RESUME", WIN_W/2, WIN_H/2 + 20, 2, C_DIM, true);
    drawText(ren, "R  -  RESTART", WIN_W/2, WIN_H/2 + 50, 2, C_DIM, true);
    drawText(ren, "M  -  MENU", WIN_W/2, WIN_H/2 + 80, 2, C_DIM, true);
}

// ─────────────────────────────────────────────
//  Game over screen
// ─────────────────────────────────────────────
static void drawGameOver(SDL_Renderer* ren, const Game& g) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0,0,0,160);
    SDL_Rect full{0,0,WIN_W,WIN_H};
    SDL_RenderFillRect(ren, &full);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    // Player 1 is on the right (right.score)
    bool p1wins = g.right.score >= WIN_SCORE;
    std::string winner = p1wins ? "PLAYER 1 WINS!" :
                         (g.mode == Mode::ONE_PLAYER ? "AI WINS!" : "PLAYER 2 WINS!");
    Col wCol = p1wins ? C_P1 : C_P2;

    drawText(ren, winner, WIN_W/2, WIN_H/2 - 60, 4, wCol, true);
    // Display as AI/P2 score : P1 score
    drawText(ren, std::to_string(g.left.score) + " - " + std::to_string(g.right.score),
             WIN_W/2, WIN_H/2 - 5, 6, C_WHITE, true);
    drawText(ren, "R  -  PLAY AGAIN", WIN_W/2, WIN_H/2 + 60, 2, C_DIM, true);
    drawText(ren, "M  -  MENU",       WIN_W/2, WIN_H/2 + 90, 2, C_DIM, true);
}

// ─────────────────────────────────────────────
//  Render a frame
// ─────────────────────────────────────────────
static void render(SDL_Renderer* ren, const Game& g) {
    setColor(ren, C_BG);
    SDL_RenderClear(ren);

    if (g.state == GameState::MENU) {
        drawMenu(ren, g);
        SDL_RenderPresent(ren);
        return;
    }

    drawNet(ren);

    // Ball trail
    for (int i = 0; i < 8; ++i) {
        int idx = (g.ball.trailHead - 1 - i + 8) % 8;
        float alpha = (8 - i) / 8.f * 0.4f;
        float sz    = BALL_SIZE * (8 - i) / 8.f;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, C_BALL.r, C_BALL.g, C_BALL.b, (Uint8)(alpha*255));
        SDL_FRect tr{ g.ball.trailX[idx] - sz/2, g.ball.trailY[idx] - sz/2, sz, sz };
        SDL_RenderFillRectF(ren, &tr);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }

    // Ball
    setColor(ren, C_BALL);
    fillRect(ren, g.ball.x - BALL_SIZE/2, g.ball.y - BALL_SIZE/2, BALL_SIZE, BALL_SIZE);

    // Paddles with glow
    auto drawPaddle = [&](const Paddle& p) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, p.color.r, p.color.g, p.color.b, 40);
        SDL_FRect glw{ p.x-4, p.y-4, PADDLE_W+8, PADDLE_H+8 };
        SDL_RenderFillRectF(ren, &glw);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        setColor(ren, p.color);
        fillRect(ren, p.x, p.y, PADDLE_W, PADDLE_H);
    };
    drawPaddle(g.left);
    drawPaddle(g.right);

    drawParticles(ren);
    drawHUD(ren, g);

    // countdown
    if (g.countDown > 0.f && g.state == GameState::PLAYING) {
        int n = (int)std::ceil(g.countDown);
        if (n >= 1) {
            drawText(ren, std::to_string(n), WIN_W/2, WIN_H/2 - 20, 6, C_DIM, true);
        }
    }

    if (g.state == GameState::PAUSED)    drawPause(ren);
    if (g.state == GameState::GAME_OVER) drawGameOver(ren, g);

    SDL_RenderPresent(ren);
}

// ─────────────────────────────────────────────
//  Update logic
// ─────────────────────────────────────────────
static void clampPaddle(Paddle& p) {
    p.y = std::max(0.f, std::min(p.y, WIN_H - PADDLE_H));
}

static void update(Game& g, float dt) {
    if (g.state != GameState::PLAYING) return;

    if (g.countDown > 0.f) {
        g.countDown -= dt;
    }

    // ── paddle movement ──
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    if (g.mode == Mode::TWO_PLAYER) {
        // 2-player: P2 (LEFT) = W/S, P1 (RIGHT) = UP/DOWN
        g.left.vy = 0.f;
        if (keys[SDL_SCANCODE_W]) g.left.vy = -PADDLE_SPEED;
        if (keys[SDL_SCANCODE_S]) g.left.vy =  PADDLE_SPEED;
        g.left.y += g.left.vy * dt;
        clampPaddle(g.left);

        g.right.vy = 0.f;
        if (keys[SDL_SCANCODE_UP])   g.right.vy = -PADDLE_SPEED;
        if (keys[SDL_SCANCODE_DOWN]) g.right.vy =  PADDLE_SPEED;
        g.right.y += g.right.vy * dt;
        clampPaddle(g.right);
    } else {
        // 1-player: P1 (RIGHT) = W/S or UP/DOWN; AI controls LEFT
        g.right.vy = 0.f;
        if (keys[SDL_SCANCODE_W]    || keys[SDL_SCANCODE_UP])   g.right.vy = -PADDLE_SPEED;
        if (keys[SDL_SCANCODE_S]    || keys[SDL_SCANCODE_DOWN]) g.right.vy =  PADDLE_SPEED;
        g.right.y += g.right.vy * dt;
        clampPaddle(g.right);

        g.updateAI(dt);
        g.left.y += g.left.vy * dt;
        clampPaddle(g.left);
    }

    if (g.countDown > 0.f) return; // don't move ball yet

    // ── ball movement ──
    Ball& b = g.ball;

    b.trailX[b.trailHead] = b.x;
    b.trailY[b.trailHead] = b.y;
    b.trailHead = (b.trailHead + 1) % 8;

    b.x += b.vx * dt;
    b.y += b.vy * dt;

    // top / bottom walls
    if (b.y - BALL_SIZE/2 <= 0.f) {
        b.y   = BALL_SIZE/2;
        b.vy  = std::abs(b.vy);
        playBeep(330.0, 0.04, 0.2);
        spawnParticles(b.x, b.y, C_DIM, 6);
    }
    if (b.y + BALL_SIZE/2 >= WIN_H) {
        b.y   = WIN_H - BALL_SIZE/2;
        b.vy  = -std::abs(b.vy);
        playBeep(330.0, 0.04, 0.2);
        spawnParticles(b.x, b.y, C_DIM, 6);
    }

    // ── paddle collision ──
    auto paddleHit = [&](Paddle& paddle, float ballDir) -> bool {
        float bLeft  = b.x - BALL_SIZE/2;
        float bRight = b.x + BALL_SIZE/2;
        float bTop   = b.y - BALL_SIZE/2;
        float bBot   = b.y + BALL_SIZE/2;

        float pRight = paddle.x + PADDLE_W;
        float pLeft  = paddle.x;

        bool xOver = (ballDir > 0) ? (bRight >= pLeft && b.x <= pRight)
                                    : (bLeft  <= pRight && b.x >= pLeft);
        bool yOver = bBot >= paddle.y && bTop <= paddle.y + PADDLE_H;

        if (!xOver || !yOver) return false;

        float relY = (b.y - (paddle.y + PADDLE_H/2.f)) / (PADDLE_H/2.f);
        float bounceAngle = relY * 1.1f;

        b.speed = std::min(b.speed + SPEED_INC, BALL_MAX_SP);
        b.vx = -ballDir * b.speed * std::cos(bounceAngle);
        b.vy =            b.speed * std::sin(bounceAngle);

        float minVy = b.speed * 0.15f;
        if (std::abs(b.vy) < minVy)
            b.vy = (b.vy >= 0 ? 1 : -1) * minVy;

        if (ballDir > 0)
            b.x = pLeft - BALL_SIZE/2;
        else
            b.x = pRight + BALL_SIZE/2;

        return true;
    };

    // Ball moving left  → may hit LEFT paddle  (AI/P2)
    if (b.vx < 0 && paddleHit(g.left,  -1.f)) {
        playBeep(520.0, 0.05, 0.3);
        spawnParticles(g.left.x + PADDLE_W, b.y, C_P2);
    }
    // Ball moving right → may hit RIGHT paddle (P1)
    if (b.vx > 0 && paddleHit(g.right,  1.f)) {
        playBeep(460.0, 0.05, 0.3);
        spawnParticles(g.right.x, b.y, C_P1);
    }

    // ── scoring ──
    // Ball exits LEFT  → AI/P2 missed → Player 1 (right) scores
    if (b.x < 0) {
        g.right.score++;
        g.lastScorer = 1;
        playBeep(180.0, 0.25, 0.5);
        if (g.right.score >= WIN_SCORE) { g.state = GameState::GAME_OVER; return; }
        resetBallInto(b, 1);   // serve toward right (player)
        g.countDown = 1.5f;
    }
    // Ball exits RIGHT → P1 missed → AI/P2 (left) scores
    if (b.x > WIN_W) {
        g.left.score++;
        g.lastScorer = 2;
        playBeep(180.0, 0.25, 0.5);
        if (g.left.score >= WIN_SCORE) { g.state = GameState::GAME_OVER; return; }
        resetBallInto(b, 2);   // serve toward left (AI)
        g.countDown = 1.5f;
    }
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init error: %s", SDL_GetError());
        return 1;
    }

    // Audio
    SDL_AudioSpec want{}, have{};
    want.freq     = AUDIO_FREQ;
    want.format   = AUDIO_F32SYS;
    want.channels = 1;
    want.samples  = AUDIO_SAMPLES;
    want.callback = audioCallback;
    g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audioDev) {
        g_audioFreq = have.freq;
        SDL_PauseAudioDevice(g_audioDev, 0);
    }

    SDL_Window* win = SDL_CreateWindow(
        "PONG",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Game g;
    g.state = GameState::MENU;

    Uint32 prevTime = SDL_GetTicks();

    bool running = true;
    while (running) {
        Uint32 now = SDL_GetTicks();
        float  dt  = std::min((now - prevTime) / 1000.f, 0.05f);
        prevTime   = now;

        // ── events ──
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = false; break; }
            if (ev.type == SDL_KEYDOWN) {
                auto k = ev.key.keysym.sym;

                if (g.state == GameState::MENU) {
                    if (k == SDLK_UP   || k == SDLK_w) g.menuSel = (g.menuSel-1+g.menuCount)%g.menuCount;
                    if (k == SDLK_DOWN || k == SDLK_s) g.menuSel = (g.menuSel+1)%g.menuCount;
                    if (k == SDLK_RETURN || k == SDLK_SPACE) {
                        Mode m = (g.menuSel == 3) ? Mode::TWO_PLAYER : Mode::ONE_PLAYER;
                        Difficulty d = Difficulty::MEDIUM;
                        if (g.menuSel == 0) d = Difficulty::EASY;
                        if (g.menuSel == 1) d = Difficulty::MEDIUM;
                        if (g.menuSel == 2) d = Difficulty::HARD;
                        g.init(m, d);
                    }
                    if (k == SDLK_ESCAPE) running = false;

                } else if (g.state == GameState::PLAYING) {
                    if (k == SDLK_ESCAPE || k == SDLK_p)
                        g.state = GameState::PAUSED;

                } else if (g.state == GameState::PAUSED) {
                    if (k == SDLK_ESCAPE || k == SDLK_p)
                        g.state = GameState::PLAYING;
                    if (k == SDLK_r) g.init(g.mode, g.diff);
                    if (k == SDLK_m) g.state = GameState::MENU;

                } else if (g.state == GameState::GAME_OVER) {
                    if (k == SDLK_r) g.init(g.mode, g.diff);
                    if (k == SDLK_m) g.state = GameState::MENU;
                }
            }
        }

        updateParticles(dt);
        update(g, dt);
        render(ren, g);

        Uint32 elapsed = SDL_GetTicks() - now;
        if (elapsed < (Uint32)FRAME_MS)
            SDL_Delay((Uint32)FRAME_MS - elapsed);
    }

    if (g_audioDev) SDL_CloseAudioDevice(g_audioDev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}