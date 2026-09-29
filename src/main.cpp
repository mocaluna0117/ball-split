#include <SDL.h>

#include "Simulation.h"
#include "Font.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

// Windows ではウィンドウアプリとしてビルドしているので、起動しても
// コンソールが開かない。ただしそれだと --headless の出力先が無くなるため、
// コマンドプロンプトから起動された場合に限り、その呼び出し元のコンソールに繋ぐ。
// ダブルクリックで起動した時は繋ぐ相手がいないので、何も起きず窓も増えない。
void attachParentConsole()
{
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

// 論理解像度。実際のウィンドウの大きさに関わらず、この座標系で描く。
constexpr int LOGICAL_W = 1000;
constexpr int LOGICAL_H = 1000;

constexpr int   MIN_SPEED_SCALE = 1;
constexpr int   MAX_SPEED_SCALE = 512;
constexpr int   BASE_SUBSTEPS   = 8;       // 60fps x 8 = 480 ステップ/秒 = 実時間
constexpr double FRAME_BUDGET   = 0.012;   // 1 フレームで物理に使ってよい秒数

enum class AppState { Idle, Running, Paused, Finished };

struct Button {
    SDL_FRect   rect;
    const char* label;
};

bool hitTest(const SDL_FRect& r, float x, float y)
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

void drawButton(SDL_Renderer* ren, const Button& b, bool enabled)
{
    SDL_SetRenderDrawColor(ren, enabled ? 58 : 34, enabled ? 64 : 38, enabled ? 82 : 50, 255);
    SDL_RenderFillRectF(ren, &b.rect);

    SDL_SetRenderDrawColor(ren, enabled ? 150 : 78, enabled ? 160 : 84, enabled ? 185 : 100, 255);
    SDL_RenderDrawRectF(ren, &b.rect);

    const float px = 3.0f;
    SDL_SetRenderDrawColor(ren, enabled ? 235 : 120, enabled ? 238 : 126, enabled ? 245 : 140, 255);
    drawText(ren,
             b.rect.x + (b.rect.w - textWidth(px, b.label)) * 0.5f,
             b.rect.y + (b.rect.h - textHeight(px)) * 0.5f,
             px, b.label);
}

// GUI なしで最後まで回し、到達時間と正しさを確かめるモード
int runHeadless(int initialBalls)
{
    Simulation sim;
    sim.setInitialBalls(initialBalls);
    sim.reset();

    // size_t を %zu で出すと処理系によって崩れるため、明示的に幅を揃えて渡す
    std::printf("ball radius %.2f   initial balls %d   max balls %llu   grid %d x %d\n\n",
                (double)BALL_R, sim.getInitialBalls(),
                (unsigned long long)MAX_BALLS, GRID_DIM, GRID_DIM);
    std::printf("%10s %14s %12s %14s\n", "balls", "sim time(s)", "wall(s)", "steps");

    auto      t0         = std::chrono::steady_clock::now();
    size_t    nextReport = sim.count();
    long long steps      = 0;

    while (!sim.isFinished()) {
        sim.step(DT);
        ++steps;

        if (sim.count() >= nextReport) {
            double wall = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - t0).count();
            std::printf("%10llu %14.2f %12.2f %14lld\n",
                        (unsigned long long)sim.count(), (double)sim.getSimTime(),
                        wall, (long long)steps);
            std::fflush(stdout);
            while (nextReport <= sim.count()) nextReport *= 2;
        }

        if (sim.getSimTime() > 20000.0f) {
            std::printf("\n*** timed out before reaching the limit ***\n");
            break;
        }
    }

    // 満杯の状態で 1 ステップにかかる実時間を測る
    auto m0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) sim.step(DT);
    double perStep = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - m0).count() / 100.0;

    size_t outside = 0, overlapping = 0;
    float  maxOverlap = 0.0f;
    sim.diagnose(outside, overlapping, maxOverlap);

    std::printf("\n--- result ---\n");
    std::printf("balls            : %llu\n", (unsigned long long)sim.count());
    std::printf("time per step    : %.3f ms  (%.1f steps/sec)\n", perStep * 1000.0, 1.0 / perStep);
    std::printf("outside arena    : %llu\n", (unsigned long long)outside);
    std::printf("overlapping pairs: %llu  (%.1f%% of balls)\n",
                (unsigned long long)overlapping,
                100.0 * (double)overlapping / (double)sim.count());
    std::printf("deepest overlap  : %.4f px  (%.1f%% of diameter)\n",
                (double)maxOverlap, 100.0 * (double)maxOverlap / (double)(BALL_R * 2.0f));
    // 判定基準: 上限到達、アリーナ外ゼロ、最大の重なりが 1.0 論理 px 未満。
    //
    // 論理 1000px を 900px のウィンドウに描くので、1.0 論理 px は実画面で 0.9 ピクセル。
    // つまり「1 ピクセルもずれて見えない」が基準の意味。
    //
    // 最大値は 65536 個の中の最悪値なので実行ごとに揺れる。16 回測って
    // 0.21〜0.55 px、中央値は約 0.30 px だった。基準はその最大に対して
    // 2 倍近い余裕を取ってある。
    const float OVERLAP_LIMIT = 1.0f;
    std::printf("verdict          : %s  (limit %.2f px)\n",
                (sim.count() == MAX_BALLS && outside == 0 &&
                 maxOverlap < OVERLAP_LIMIT) ? "PASS" : "CHECK",
                (double)OVERLAP_LIMIT);
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    attachParentConsole();

    const char* shotPath  = nullptr;   // 1 フレームを BMP に保存して終了する（資料用）
    int         shotAtFrame = -1;      // -1 なら上限到達時に保存
    bool        autoStart   = false;

    bool headless    = false;
    int  initialBalls = DEFAULT_INITIAL_BALLS;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;
        else if (std::strcmp(argv[i], "--initial") == 0 && i + 1 < argc) {
            initialBalls = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shotPath  = argv[++i];
            autoStart = true;
        } else if (std::strcmp(argv[i], "--shot-at") == 0 && i + 1 < argc) {
            shotAtFrame = std::atoi(argv[++i]);
        }
    }

    if (headless) return runHeadless(initialBalls);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Ball Split",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        900, 900,
        SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_RenderSetLogicalSize(renderer, LOGICAL_W, LOGICAL_H);

    Simulation sim;
    sim.setInitialBalls(initialBalls);
    sim.reset();

    AppState state      = autoStart ? AppState::Running : AppState::Idle;
    int      speedScale = autoStart ? MAX_SPEED_SCALE : 16;

    const Button btnRun   { {  20.0f, 12.0f, 150.0f, 40.0f }, "START" };
    const Button btnStop  { {  20.0f, 12.0f, 150.0f, 40.0f }, "STOP"  };
    const Button btnReset { { 182.0f, 12.0f, 120.0f, 40.0f }, "RESET" };
    const Button btnSlow  { {  86.0f, 60.0f,  36.0f, 34.0f }, "-"     };
    const Button btnFast  { { 128.0f, 60.0f,  36.0f, 34.0f }, "+"     };
    const Button btnFewer { { 340.0f, 60.0f,  36.0f, 34.0f }, "-"     };
    const Button btnMore  { { 382.0f, 60.0f,  36.0f, 34.0f }, "+"     };

    // アリーナの輪郭。座標は変わらないので一度だけ作る。
    //
    // 折れ線を SDL_RenderDrawLinesF で一度に描くと、Windows の Direct3D では
    // 線分の一部が描画されずに円が欠けた。バックエンドによって折れ線の扱いが
    // 違うため、ボールや文字と同じ「矩形の塗りつぶし」に統一して回避している。
    // 点を細かく取れば矩形が重なり合って連続した線に見える。
    std::vector<SDL_FRect> arena;
    {
        const int   ARENA_SEGMENTS = 2048;
        const float ARENA_THICK    = 2.0f;
        arena.reserve(ARENA_SEGMENTS);
        for (int i = 0; i < ARENA_SEGMENTS; ++i) {
            float a = 6.2831853f * (float)i / (float)ARENA_SEGMENTS;
            arena.push_back(SDL_FRect{
                ARENA_CX + ARENA_R * std::cos(a) - ARENA_THICK * 0.5f,
                ARENA_CY + ARENA_R * std::sin(a) - ARENA_THICK * 0.5f,
                ARENA_THICK, ARENA_THICK });
        }
    }

    std::vector<SDL_FRect> normalRects;
    std::vector<SDL_FRect> freshRects;
    normalRects.reserve(MAX_BALLS);
    freshRects.reserve(4096);

    long   totalFrames = 0;
    int    fps = 0, frameCount = 0;
    Uint64 fpsTimer = SDL_GetPerformanceCounter();
    int    effectiveScale = speedScale;

    auto toggleRun = [&]() {
        if (state == AppState::Idle || state == AppState::Paused) state = AppState::Running;
        else if (state == AppState::Running)                      state = AppState::Paused;
    };
    auto doReset = [&]() {
        sim.reset();
        state = AppState::Idle;
    };
    // 初期個数を 2 倍・半分にする。変えたらすぐ配置し直して結果を見せる
    auto changeInitial = [&](bool more) {
        int n = sim.getInitialBalls();
        sim.setInitialBalls(more ? n * 2 : n / 2);
        doReset();
    };

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE:  toggleRun();     break;
                case SDLK_r:      doReset();       break;
                case SDLK_UP:
                    if (speedScale < MAX_SPEED_SCALE) speedScale *= 2;
                    break;
                case SDLK_DOWN:
                    if (speedScale > MIN_SPEED_SCALE) speedScale /= 2;
                    break;
                case SDLK_RIGHT: changeInitial(true);  break;
                case SDLK_LEFT:  changeInitial(false); break;
                default: break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                float lx = 0.0f, ly = 0.0f;
                SDL_RenderWindowToLogical(renderer, e.button.x, e.button.y, &lx, &ly);

                if      (hitTest(btnRun.rect,   lx, ly)) toggleRun();
                else if (hitTest(btnReset.rect, lx, ly)) doReset();
                else if (hitTest(btnSlow.rect,  lx, ly)) { if (speedScale > MIN_SPEED_SCALE) speedScale /= 2; }
                else if (hitTest(btnFast.rect,  lx, ly)) { if (speedScale < MAX_SPEED_SCALE) speedScale *= 2; }
                else if (hitTest(btnFewer.rect, lx, ly)) changeInitial(false);
                else if (hitTest(btnMore.rect,  lx, ly)) changeInitial(true);
            }
        }

        // ---- 物理を進める ----
        if (state == AppState::Running) {
            const int target = BASE_SUBSTEPS * speedScale;
            int       done   = 0;

            Uint64 t0   = SDL_GetPerformanceCounter();
            double freq = (double)SDL_GetPerformanceFrequency();

            while (done < target) {
                sim.step(DT);
                ++done;

                if (sim.isFinished()) { state = AppState::Finished; break; }

                // 重くなってきたら途中で打ち切る。落ちる代わりに倍率が下がる。
                if ((double)(SDL_GetPerformanceCounter() - t0) / freq > FRAME_BUDGET) break;
            }
            effectiveScale = done / BASE_SUBSTEPS;
            if (effectiveScale < 1) effectiveScale = 1;
        }

        // ---- 描画 ----
        SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 120, 130, 160, 255);
        SDL_RenderFillRectsF(renderer, arena.data(), (int)arena.size());

        // 表示上だけの半径。実寸の 0.9px はボールが数個のうちは点にしか見えないので、
        // 個数が少ない間だけ大きく描く。当たり判定には一切影響しない。
        float drawR = BALL_R;
        {
            float minR = 5.0f / std::sqrt((float)sim.count());
            if (minR > drawR) drawR = minR;
        }

        normalRects.clear();
        freshRects.clear();
        for (const Ball& b : sim.getBalls()) {
            SDL_FRect r{ b.x - drawR, b.y - drawR, drawR * 2.0f, drawR * 2.0f };
            if (b.cooldown > 0.0f) freshRects.push_back(r);   // 分裂したて
            else                   normalRects.push_back(r);
        }
        if (!normalRects.empty()) {
            SDL_SetRenderDrawColor(renderer, 110, 210, 140, 255);
            SDL_RenderFillRectsF(renderer, normalRects.data(), (int)normalRects.size());
        }
        if (!freshRects.empty()) {
            SDL_SetRenderDrawColor(renderer, 245, 225, 120, 255);
            SDL_RenderFillRectsF(renderer, freshRects.data(), (int)freshRects.size());
        }

        // ---- UI ----
        drawButton(renderer, (state == AppState::Running) ? btnStop : btnRun,
                   state != AppState::Finished);
        drawButton(renderer, btnReset, true);
        drawButton(renderer, btnSlow,  speedScale > MIN_SPEED_SCALE);
        drawButton(renderer, btnFast,  speedScale < MAX_SPEED_SCALE);
        drawButton(renderer, btnFewer, sim.getInitialBalls() > MIN_INITIAL_BALLS);
        drawButton(renderer, btnMore,  sim.getInitialBalls() < MAX_INITIAL_BALLS);

        const char* label = "READY";
        switch (state) {
        case AppState::Idle:     label = "READY";    break;
        case AppState::Running:  label = "RUNNING";  break;
        case AppState::Paused:   label = "PAUSED";   break;
        case AppState::Finished: label = "FINISHED"; break;
        }
        if (state == AppState::Finished) SDL_SetRenderDrawColor(renderer, 245, 225, 120, 255);
        else                             SDL_SetRenderDrawColor(renderer, 190, 200, 225, 255);
        drawText(renderer, 316.0f, 22.0f, 3.0f, label);

        ++frameCount;
        Uint64 now = SDL_GetPerformanceCounter();
        if ((double)(now - fpsTimer) / (double)SDL_GetPerformanceFrequency() >= 1.0) {
            fps = frameCount;
            frameCount = 0;
            fpsTimer = now;
        }

        char line[128];

        // ボタンの見出しと現在値
        SDL_SetRenderDrawColor(renderer, 150, 158, 180, 255);
        drawText(renderer,  20.0f, 70.0f, 2.0f, "SPEED");
        drawText(renderer, 250.0f, 70.0f, 2.0f, "INITIAL");

        SDL_SetRenderDrawColor(renderer, 205, 212, 230, 255);
        std::snprintf(line, sizeof(line), "%dX", speedScale);
        drawText(renderer, 172.0f, 70.0f, 2.0f, line);
        std::snprintf(line, sizeof(line), "%d", sim.getInitialBalls());
        drawText(renderer, 426.0f, 70.0f, 2.0f, line);

        std::snprintf(line, sizeof(line), "BALLS %llu / %llu",
                      (unsigned long long)sim.count(), (unsigned long long)MAX_BALLS);
        drawText(renderer, 20.0f, 98.0f, 3.0f, line);

        SDL_SetRenderDrawColor(renderer, 150, 158, 180, 255);
        std::snprintf(line, sizeof(line), "TIME %.1fS   FPS %d   EFF %dX",
                      (double)sim.getSimTime(), fps, effectiveScale);
        drawText(renderer, 20.0f, 124.0f, 2.0f, line);

        SDL_SetRenderDrawColor(renderer, 120, 128, 150, 255);
        drawText(renderer, 20.0f, 968.0f, 2.0f,
                 "SPACE START/STOP   R RESET   UP/DOWN SPEED   LEFT/RIGHT INITIAL   ESC QUIT");

        // 資料用のフレーム保存
        if (shotPath != nullptr &&
            ((shotAtFrame >= 0 && totalFrames >= shotAtFrame) ||
             (shotAtFrame <  0 && state == AppState::Finished))) {
            int ow = 0, oh = 0;
            SDL_GetRendererOutputSize(renderer, &ow, &oh);
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
            if (shot != nullptr) {
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                     shot->pixels, shot->pitch);
                SDL_SaveBMP(shot, shotPath);
                SDL_FreeSurface(shot);
                std::printf("saved %s  (%d x %d, %llu balls)\n",
                            shotPath, ow, oh, (unsigned long long)sim.count());
            }
            running = false;
        }
        ++totalFrames;

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
