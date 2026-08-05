#pragma once

#include "core/types.h"

#include <array>
#include <cmath>

namespace sillage {

// Constant-velocity Kalman filter, state [x, y, vx, vy].
// Hand-rolled 4x4 math: the structure of F/H lets everything stay explicit and
// cheap. Eigen replaces this when heavier models (IMM) arrive.
class KalmanCV {
public:
    struct Params {
        // Humans pivot hard: abrupt turnarounds reach several m/s^2. Too low a
        // value makes the steady-state gate so tight that a sharp turn throws
        // the measurement out of it (ghost births at every about-face).
        float processAccelNoise = 4.5f; // m/s^2, white-noise acceleration
        float measurementNoise = 0.05f; // m, std dev of cluster centroids
        float initialVelocityVar = 1.5f;
    };

    KalmanCV() = default;
    KalmanCV(Vec2 position, const Params& params) : params_(params) {
        x_ = {position.x, position.y, 0.0f, 0.0f};
        const float pv = params.measurementNoise * params.measurementNoise;
        p_ = {};
        p_[idx(0, 0)] = pv;
        p_[idx(1, 1)] = pv;
        p_[idx(2, 2)] = params.initialVelocityVar;
        p_[idx(3, 3)] = params.initialVelocityVar;
    }

    void predict(float dt) {
        // x = F x
        x_[0] += dt * x_[2];
        x_[1] += dt * x_[3];

        // P = F P F^T + Q, exploiting the block structure of F = [I, dt*I; 0, I].
        // Per axis (x with vx, y with vy):
        for (int a = 0; a < 2; ++a) {
            const int i = a;     // position index
            const int j = a + 2; // velocity index
            const float pii = p_[idx(i, i)];
            const float pij = p_[idx(i, j)];
            const float pjj = p_[idx(j, j)];
            p_[idx(i, i)] = pii + dt * (pij + pij) + dt * dt * pjj;
            p_[idx(i, j)] = pij + dt * pjj;
            p_[idx(j, i)] = p_[idx(i, j)];
            // Q, discretized white-noise acceleration:
            const float q = params_.processAccelNoise * params_.processAccelNoise;
            p_[idx(i, i)] += 0.25f * dt * dt * dt * dt * q;
            p_[idx(i, j)] += 0.5f * dt * dt * dt * q;
            p_[idx(j, i)] = p_[idx(i, j)];
            p_[idx(j, j)] += dt * dt * q;
        }
    }

    // Squared Mahalanobis distance of a measurement to the predicted position.
    float gateDistanceSq(Vec2 z) const {
        const float r = params_.measurementNoise * params_.measurementNoise;
        const float sxx = p_[idx(0, 0)] + r;
        const float syy = p_[idx(1, 1)] + r;
        const float sxy = p_[idx(0, 1)];
        const float det = sxx * syy - sxy * sxy;
        if (det <= 1e-12f) {
            return 1e9f;
        }
        const float dx = z.x - x_[0];
        const float dy = z.y - x_[1];
        return (dx * (syy * dx - sxy * dy) + dy * (sxx * dy - sxy * dx)) / det;
    }

    void update(Vec2 z) {
        const float r = params_.measurementNoise * params_.measurementNoise;
        // Innovation covariance S = HPH^T + R (2x2), H = [I2 0].
        const float sxx = p_[idx(0, 0)] + r;
        const float syy = p_[idx(1, 1)] + r;
        const float sxy = p_[idx(0, 1)];
        const float det = sxx * syy - sxy * sxy;
        if (det <= 1e-12f) {
            return;
        }
        const float ixx = syy / det, iyy = sxx / det, ixy = -sxy / det;

        // Kalman gain K = P H^T S^-1 (4x2).
        std::array<float, 8> k{};
        for (int row = 0; row < 4; ++row) {
            const float phx = p_[idx(row, 0)];
            const float phy = p_[idx(row, 1)];
            k[row * 2 + 0] = phx * ixx + phy * ixy;
            k[row * 2 + 1] = phx * ixy + phy * iyy;
        }

        const float dx = z.x - x_[0];
        const float dy = z.y - x_[1];
        for (int row = 0; row < 4; ++row) {
            x_[row] += k[row * 2 + 0] * dx + k[row * 2 + 1] * dy;
        }

        // P = (I - K H) P ; KH only touches the first two columns of the row space.
        std::array<float, 16> np{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                np[idx(row, col)] = p_[idx(row, col)] - k[row * 2 + 0] * p_[idx(0, col)] -
                                    k[row * 2 + 1] * p_[idx(1, col)];
            }
        }
        p_ = np;
    }

    Vec2 position() const { return {x_[0], x_[1]}; }
    Vec2 velocity() const { return {x_[2], x_[3]}; }
    float positionVariance() const { return 0.5f * (p_[idx(0, 0)] + p_[idx(1, 1)]); }

    // Clamps the state's position into the disc (center, maxRadius). Used on
    // tracks frozen inside a shared cluster: their filters receive no
    // measurement, and pure constant-velocity prediction walks them out of
    // the physical blob (crossing each other inside it, or exiting where no
    // person is). The displacement is admitted into the position covariance —
    // forcing x while leaving P intact would make the filter overconfident
    // about a position it was pushed to, and the next real measurement would
    // be under-weighted exactly when it matters most (at separation).
    void confineTo(Vec2 center, float maxRadius) {
        const float dx = x_[0] - center.x;
        const float dy = x_[1] - center.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= maxRadius || maxRadius < 0.0f || dist <= 1e-6f) {
            return;
        }
        const float scale = maxRadius / dist;
        const float jx = dx * (scale - 1.0f); // applied displacement
        const float jy = dy * (scale - 1.0f);
        x_[0] += jx;
        x_[1] += jy;
        p_[idx(0, 0)] += jx * jx;
        p_[idx(1, 1)] += jy * jy;
        // Also drop the OUTWARD radial velocity, admitting it into the
        // velocity covariance. The person this state stands for is inside the
        // blob — that is what made it shared — so an escape velocity is
        // fiction, and left in place it re-integrates the same escape every
        // tick, racing the clamp forever. The direction prior for
        // re-association at separation is not lost: it lives in the
        // continuity anchor (last MEASURED velocity), which confinement
        // never touches.
        const float ux = dx / dist, uy = dy / dist;
        const float vOut = x_[2] * ux + x_[3] * uy;
        if (vOut > 0.0f) {
            x_[2] -= vOut * ux;
            x_[3] -= vOut * uy;
            p_[idx(2, 2)] += vOut * vOut * ux * ux;
            p_[idx(3, 3)] += vOut * vOut * uy * uy;
        }
    }

private:
    static constexpr int idx(int row, int col) { return row * 4 + col; }

    Params params_{};
    std::array<float, 4> x_{};
    std::array<float, 16> p_{};
};

} // namespace sillage
