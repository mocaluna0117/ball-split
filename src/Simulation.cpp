#include "Simulation.h"
#include <cmath>

// ---------------- 小道具 ----------------

// 0〜360 度のランダムな向きに、大きさ SPEED の速度を作る。
// (cos, sin) は長さ 1 のベクトルなので、SPEED 倍すれば速さが必ず SPEED になる。
void Simulation::randomVelocity(float& vx, float& vy)
{
    float angle = angleDist(rng);
    vx = SPEED * std::cos(angle);
    vy = SPEED * std::sin(angle);
}

// 向きを保ったまま速さだけを SPEED に揃え直す。
// 物理的には正しくないが、遅いボールが出て連鎖が失速するのを防ぐための妥協。
void Simulation::normalizeSpeed(float& vx, float& vy)
{
    float s = std::sqrt(vx * vx + vy * vy);
    if (s > 0.0001f) {
        vx = vx / s * SPEED;
        vy = vy / s * SPEED;
    }
}

// ---------------- 初期化 ----------------

void Simulation::setInitialBalls(int n)
{
    if (n < MIN_INITIAL_BALLS) n = MIN_INITIAL_BALLS;
    if (n > MAX_INITIAL_BALLS) n = MAX_INITIAL_BALLS;
    initialBalls = n;
}

void Simulation::reset()
{
    balls.clear();
    balls.reserve(MAX_BALLS);
    nextBall.reserve(MAX_BALLS);
    occupiedCells.reserve(MAX_BALLS);
    pendingSplits.reserve(4096);

    // 格子はここで一度だけ -1 で埋める。以降は使ったマスだけを戻す。
    cellHead.assign((size_t)GRID_DIM * (size_t)GRID_DIM, -1);
    occupiedCells.clear();

    simTime = 0.0f;

    // 個数が増えても密になりすぎないよう、撒く範囲を個数に応じて広げる
    float spread = ARENA_R * 0.5f;
    float needed = BALL_R * 8.0f * std::sqrt((float)initialBalls);
    if (needed > spread)             spread = needed;
    if (spread > ARENA_R - BALL_R)   spread = ARENA_R - BALL_R;

    for (int i = 0; i < initialBalls; ++i) {
        float a = angleDist(rng);

        // 円の中で一様に散らすには、半径に平方根をかける必要がある。
        // そのまま乱数を使うと中心付近に偏る。
        float d = spread * std::sqrt(unitDist(rng));

        Ball b;
        b.x = ARENA_CX + d * std::cos(a);
        b.y = ARENA_CY + d * std::sin(a);
        randomVelocity(b.vx, b.vy);

        // 初期配置はランダムなので、まれに重なって置かれる。
        // 猶予を与えておけば、生まれた瞬間に分裂してしまうことがなく、
        // 猶予が切れる前に押し戻しで離れる。
        b.cooldown = SPLIT_COOLDOWN;

        balls.push_back(b);
    }
}

// ---------------- 1 ステップ ----------------

// 1 ステップは「進める → 解決 → 壁」を 2 巡する。
// 壁の押し戻しはボールを内側へ動かすため、その過程で新しい重なりを作ってしまう。
// 1 巡で終えるとその重なりが誰にも直されないまま画面に出るので、もう一度解決を回す。
// 2 巡目では分裂させない。分裂は 1 ステップにつき 1 回までにしたいため。
void Simulation::step(float dt)
{
    integrate(dt);

    splitEnabled = true;
    resolveCollisions();
    resolveWalls();

    splitEnabled = false;
    resolveCollisions();
    resolveWalls();        // 壁を最後に置き、アリーナ外に出ないことを保証する

    simTime += dt;
}

void Simulation::integrate(float dt)
{
    for (Ball& b : balls) {
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        b.cooldown -= dt;
    }
}

// ---------------- 空間分割 ----------------

void Simulation::cellCoords(float x, float y, int& cx, int& cy) const
{
    cx = (int)((x - (ARENA_CX - ARENA_R)) / CELL_SIZE);
    cy = (int)((y - (ARENA_CY - ARENA_R)) / CELL_SIZE);

    // 衝突解決は壁の処理より前に走るので、わずかにはみ出していることがある。
    // 範囲外の添字で配列に触らないよう必ず押し込める。
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= GRID_DIM) cx = GRID_DIM - 1;
    if (cy >= GRID_DIM) cy = GRID_DIM - 1;
}

// 配列 2 本で「マスごとの鎖」を作る。
// cellHead[c] が鎖の入口、nextBall[i] が次への矢印、-1 が終端。
// マスの数がいくら増えても確保するメモリはボールの数ぶんだけで済む。
void Simulation::buildGrid()
{
    for (int c : occupiedCells) cellHead[c] = -1;   // 使ったマスだけ戻す
    occupiedCells.clear();

    nextBall.resize(balls.size());

    for (size_t i = 0; i < balls.size(); ++i) {
        int cx, cy;
        cellCoords(balls[i].x, balls[i].y, cx, cy);
        int c = cy * GRID_DIM + cx;

        if (cellHead[c] == -1) occupiedCells.push_back(c);

        nextBall[i] = cellHead[c];
        cellHead[c] = (int)i;
    }
}

// ---------------- 1 組のペアを処理する ----------------

void Simulation::testPair(size_t i, size_t j)
{
    const float minDist   = BALL_R * 2.0f;
    const float minDistSq = minDist * minDist;

    Ball& a = balls[i];
    Ball& b = balls[j];

    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float distSq = dx * dx + dy * dy;

    // 平方根は使わない。両辺 2 乗しても大小関係は変わらないため。
    if (distSq >= minDistSq) return;

    // 両方とも猶予が切れていて、上限に余裕があるなら分裂させる
    bool canSplit = (splitEnabled &&
                     a.cooldown <= 0.0f &&
                     b.cooldown <= 0.0f &&
                     balls.size() + pendingSplits.size() + 2 <= MAX_BALLS);

    if (canSplit) {
        // ここで猶予を立てることが、同じパスでの二重予約の防止も兼ねる
        a.cooldown = SPLIT_COOLDOWN;
        b.cooldown = SPLIT_COOLDOWN;
        pendingSplits.push_back(i);
        pendingSplits.push_back(j);
        return;                       // 分裂するので反射はしない
    }

    float d = std::sqrt(distSq);

    float nx, ny;
    if (d > 0.0001f) {
        nx = dx / d;
        ny = dy / d;
    } else {
        nx = 1.0f;                    // 完全に重なっている場合の逃げ道
        ny = 0.0f;
        d  = 0.0f;
    }

    // めり込み量を半分ずつ分け合って引き離す
    float overlap = minDist - d;
    a.x -= nx * overlap * 0.5f;
    a.y -= ny * overlap * 0.5f;
    b.x += nx * overlap * 0.5f;
    b.y += ny * overlap * 0.5f;

    // 法線方向の相対速度。正なら近づいている
    float vrel = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
    if (vrel > 0.0f) {
        // 質量が等しい弾性衝突は、法線方向の成分をそっくり交換するだけでよい
        a.vx -= vrel * nx;
        a.vy -= vrel * ny;
        b.vx += vrel * nx;
        b.vy += vrel * ny;

        normalizeSpeed(a.vx, a.vy);
        normalizeSpeed(b.vx, b.vy);
    }
}

// ---------------- 衝突解決 ----------------

void Simulation::resolveCollisions()
{
    pendingSplits.clear();
    buildGrid();

    // 右・左下・下・右下の 4 方向だけを見る。
    // 隣り合う 2 マスについて必ず片方だけが相手を調べる形になり、
    // 同じペアを 2 回処理してしまうのを防げる。
    static const int NEIGHBORS[4][2] = { {1, 0}, {-1, 1}, {0, 1}, {1, 1} };

    for (size_t k = 0; k < occupiedCells.size(); ++k) {
        int c  = occupiedCells[k];
        int cx = c % GRID_DIM;
        int cy = c / GRID_DIM;

        // 同じマスの中のペア。鎖の自分より後ろだけを見る
        for (int i = cellHead[c]; i != -1; i = nextBall[i])
            for (int j = nextBall[i]; j != -1; j = nextBall[j])
                testPair((size_t)i, (size_t)j);

        // 隣のマスとのペア
        for (int n = 0; n < 4; ++n) {
            int ncx = cx + NEIGHBORS[n][0];
            int ncy = cy + NEIGHBORS[n][1];
            if (ncx < 0 || ncx >= GRID_DIM || ncy < 0 || ncy >= GRID_DIM) continue;

            int nc = ncy * GRID_DIM + ncx;
            if (cellHead[nc] == -1) continue;

            for (int i = cellHead[c]; i != -1; i = nextBall[i])
                for (int j = cellHead[nc]; j != -1; j = nextBall[j])
                    testPair((size_t)i, (size_t)j);
        }
    }

    applySplits();
}

// 走査が完全に終わってから分裂を適用する。
// 走査中に push_back すると vector の再確保で Ball& が無効になり、
// さらに balls.size() が増えてループ条件まで壊れるため。
void Simulation::applySplits()
{
    for (size_t idx : pendingSplits) {
        Ball parent = balls[idx];      // 参照ではなくコピー

        float axis = angleDist(rng);   // 割れる向きは毎回変える
        float ox = std::cos(axis) * BALL_R;
        float oy = std::sin(axis) * BALL_R;

        Ball c1, c2;
        c1.x = parent.x + ox;  c1.y = parent.y + oy;
        c2.x = parent.x - ox;  c2.y = parent.y - oy;
        randomVelocity(c1.vx, c1.vy);
        randomVelocity(c2.vx, c2.vy);
        c1.cooldown = SPLIT_COOLDOWN;
        c2.cooldown = SPLIT_COOLDOWN;

        balls[idx] = c1;               // 親のスロットを子 1 で上書き
        balls.push_back(c2);           // 子 2 は末尾に追加
    }
}

// ---------------- 壁 ----------------

void Simulation::resolveWalls()
{
    const float limit = ARENA_R - BALL_R;   // ボール中心が進める限界の距離

    for (Ball& b : balls) {
        float dx = b.x - ARENA_CX;
        float dy = b.y - ARENA_CY;
        float d  = std::sqrt(dx * dx + dy * dy);

        if (d <= limit) continue;

        float nx, ny;
        if (d > 0.0001f) {
            nx = dx / d;
            ny = dy / d;
        } else {
            nx = 1.0f;
            ny = 0.0f;
        }

        b.x = ARENA_CX + nx * limit;
        b.y = ARENA_CY + ny * limit;

        // 鏡面反射 v' = v - 2(v・n)n。
        // 外向きに動いている時だけ反転させないと、壁に貼り付いて震える。
        float vn = b.vx * nx + b.vy * ny;
        if (vn > 0.0f) {
            b.vx -= 2.0f * vn * nx;
            b.vy -= 2.0f * vn * ny;
        }
    }
}

// ---------------- 検証 ----------------

void Simulation::diagnose(size_t& outside, size_t& overlapping, float& maxOverlap)
{
    outside = 0;
    overlapping = 0;
    maxOverlap  = 0.0f;

    const float limit = ARENA_R - BALL_R;
    for (const Ball& b : balls) {
        float dx = b.x - ARENA_CX;
        float dy = b.y - ARENA_CY;
        if (std::sqrt(dx * dx + dy * dy) > limit + 0.01f) ++outside;
    }

    buildGrid();
    static const int NEIGHBORS[4][2] = { {1, 0}, {-1, 1}, {0, 1}, {1, 1} };

    const float minDist   = BALL_R * 2.0f - 0.01f;   // 誤差ぶんの許容
    const float minDistSq = minDist * minDist;

    auto check = [&](int i, int j) {
        float dx = balls[j].x - balls[i].x;
        float dy = balls[j].y - balls[i].y;
        float dd = dx * dx + dy * dy;
        if (dd < minDistSq) {
            ++overlapping;
            float depth = BALL_R * 2.0f - std::sqrt(dd);
            if (depth > maxOverlap) maxOverlap = depth;
        }
    };

    for (size_t k = 0; k < occupiedCells.size(); ++k) {
        int c  = occupiedCells[k];
        int cx = c % GRID_DIM;
        int cy = c / GRID_DIM;

        for (int i = cellHead[c]; i != -1; i = nextBall[i])
            for (int j = nextBall[i]; j != -1; j = nextBall[j])
                check(i, j);

        for (int n = 0; n < 4; ++n) {
            int ncx = cx + NEIGHBORS[n][0];
            int ncy = cy + NEIGHBORS[n][1];
            if (ncx < 0 || ncx >= GRID_DIM || ncy < 0 || ncy >= GRID_DIM) continue;

            int nc = ncy * GRID_DIM + ncx;
            if (cellHead[nc] == -1) continue;

            for (int i = cellHead[c]; i != -1; i = nextBall[i])
                for (int j = cellHead[nc]; j != -1; j = nextBall[j])
                    check(i, j);
        }
    }
}
