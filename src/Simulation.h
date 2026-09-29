#pragma once

#include <vector>
#include <random>
#include <cstddef>

// ---------------- アリーナとボールのパラメータ ----------------

constexpr float ARENA_CX = 500.0f;   // アリーナ中心 X（論理座標）
constexpr float ARENA_CY = 548.0f;   // アリーナ中心 Y（上部を UI に空ける）
constexpr float ARENA_R  = 400.0f;   // アリーナ半径

constexpr float BALL_R = 0.9f;            // ボール半径。65536 個で充填率 約 33%
constexpr float SPEED  = 240.0f;          // 速さ（px/秒）。向きだけが変わる
constexpr float DT     = 1.0f / 480.0f;   // 1 ステップの時間

constexpr float  SPLIT_COOLDOWN = 0.25f;  // 分裂後、再分裂できるまでの秒数
constexpr size_t MAX_BALLS      = 65536;  // 2^16。ここに達したら終了

// 初期配置の個数。実行中に変更できる
constexpr int DEFAULT_INITIAL_BALLS = 2;
constexpr int MIN_INITIAL_BALLS     = 2;
constexpr int MAX_INITIAL_BALLS     = 8192;

// ---------------- 空間分割の格子 ----------------
// マスの一辺をボールの直径にすると、接触しうる相手は自分のマスと周囲 8 マスだけになる。

constexpr float CELL_SIZE = BALL_R * 2.0f;
constexpr int   GRID_DIM  = (int)(2.0f * ARENA_R / CELL_SIZE) + 2;

struct Ball {
    float x, y;          // 中心座標
    float vx, vy;        // 速度。大きさは常に SPEED
    float cooldown;      // 0 より大きい間は分裂しない
};

class Simulation {
public:
    void reset();          // ボールを消して初期状態に戻す
    void step(float dt);   // dt 秒ぶん時間を進める

    // 初期配置の個数。範囲外の値は丸められる。次の reset から反映される
    void setInitialBalls(int n);
    int  getInitialBalls() const { return initialBalls; }

    const std::vector<Ball>& getBalls() const { return balls; }
    size_t count()      const { return balls.size(); }
    bool   isFinished() const { return balls.size() >= MAX_BALLS; }
    float  getSimTime() const { return simTime; }

    // 検証用。アリーナ外に出たボール数と、重なっているペア数を数える
    void diagnose(size_t& outside, size_t& overlapping, float& maxOverlap);

private:
    void integrate(float dt);       // 位置を進める
    void resolveCollisions();       // ボール同士の衝突を解く（splitEnabled で分裂の可否を切り替える）
    void resolveWalls();            // 壁のはみ出しを直す
    void applySplits();             // 予約された分裂を適用する

    void testPair(size_t i, size_t j);
    void randomVelocity(float& vx, float& vy);
    void normalizeSpeed(float& vx, float& vy);

    void cellCoords(float x, float y, int& cx, int& cy) const;
    void buildGrid();

    std::vector<Ball> balls;

    std::vector<int>    cellHead;        // 各マスの先頭のボール番号。空なら -1
    std::vector<int>    nextBall;        // ボール i の次のボール番号。終端なら -1
    std::vector<int>    occupiedCells;   // ボールが入っているマスだけを覚えておく
    std::vector<size_t> pendingSplits;   // 走査後にまとめて分裂させるボールの番号

    int   initialBalls = DEFAULT_INITIAL_BALLS;
    float simTime      = 0.0f;
    bool  splitEnabled = true;   // 2 巡目の解決では分裂させない

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> angleDist{0.0f, 6.2831853f};
    std::uniform_real_distribution<float> unitDist{0.0f, 1.0f};
};
