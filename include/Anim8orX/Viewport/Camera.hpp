#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace anim8orx {

constexpr float AX_PI = 3.14159265358979323846f;
constexpr float AX_DEG_TO_RAD = AX_PI / 180.0f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float px, float py, float pz) : x(px), y(py), z(pz) {}

    Vec3 operator+(const Vec3& r) const { return {x + r.x, y + r.y, z + r.z}; }
    Vec3 operator-(const Vec3& r) const { return {x - r.x, y - r.y, z - r.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }

    Vec3& operator+=(const Vec3& r) {
        x += r.x; y += r.y; z += r.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& r) {
        x -= r.x; y -= r.y; z -= r.z;
        return *this;
    }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float Length(const Vec3& v) {
    return std::sqrt(Dot(v, v));
}

inline Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    if (len <= 0.000001f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return v / len;
}

inline float Clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

// Column-major matrix for direct upload to GLSL/HLSL style column-vector shaders.
// Access is still written as (row, column) to keep math readable.
struct Mat4 {
    float m[16] = {};

    float& operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }

    static Mat4 Identity() {
        Mat4 r;
        r(0, 0) = 1.0f;
        r(1, 1) = 1.0f;
        r(2, 2) = 1.0f;
        r(3, 3) = 1.0f;
        return r;
    }

    static Mat4 LookAtRH(const Vec3& eye, const Vec3& target, const Vec3& worldUp) {
        const Vec3 f = Normalize(target - eye);
        const Vec3 s = Normalize(Cross(f, worldUp));
        const Vec3 u = Cross(s, f);

        Mat4 r = Identity();
        r(0, 0) = s.x;  r(0, 1) = s.y;  r(0, 2) = s.z;  r(0, 3) = -Dot(s, eye);
        r(1, 0) = u.x;  r(1, 1) = u.y;  r(1, 2) = u.z;  r(1, 3) = -Dot(u, eye);
        r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z; r(2, 3) = Dot(f, eye);
        r(3, 0) = 0.0f; r(3, 1) = 0.0f; r(3, 2) = 0.0f; r(3, 3) = 1.0f;
        return r;
    }

    // Right-handed Vulkan projection. Depth is 0..1. Y is flipped for Vulkan NDC
    // when using a normal positive-height viewport.
    static Mat4 PerspectiveVulkanRH(float fovYRadians, float aspect, float zNear, float zFar) {
        aspect = std::max(aspect, 0.0001f);
        zNear = std::max(zNear, 0.0001f);
        zFar = std::max(zFar, zNear + 0.001f);

        const float f = 1.0f / std::tan(fovYRadians * 0.5f);

        Mat4 r;
        r(0, 0) = f / aspect;
        r(1, 1) = -f;
        r(2, 2) = zFar / (zNear - zFar);
        r(2, 3) = (zFar * zNear) / (zNear - zFar);
        r(3, 2) = -1.0f;
        return r;
    }
};

struct ViewportCameraInput {
    bool viewportHovered = false;

    bool rightMouseDown = false;
    bool leftMouseDown = false;
    bool middleMouseDown = false;
    bool altDown = false;
    bool shiftDown = false;

    bool keyW = false;
    bool keyA = false;
    bool keyS = false;
    bool keyD = false;
    bool keyQ = false;
    bool keyE = false;
    bool focusPressed = false; // One-frame edge, not "held".

    float mouseDeltaX = 0.0f; // Pixels since previous frame.
    float mouseDeltaY = 0.0f;
    float wheelDelta = 0.0f;  // Positive means wheel moved forward/up.
    float deltaSeconds = 1.0f / 60.0f;

    int viewportWidth = 1280;
    int viewportHeight = 720;

    bool hasSelection = false;
    Vec3 selectionCenter = {0.0f, 0.0f, 0.0f};
    float selectionRadius = 1.0f;
};

class ViewportCamera {
public:
    Vec3 position = {0.0f, 1.5f, 6.0f};
    Vec3 focus = {0.0f, 0.0f, 0.0f};

    float yawRadians = 0.0f;
    float pitchRadians = -10.0f * AX_DEG_TO_RAD;
    float orbitDistance = 6.0f;

    float verticalFovRadians = 60.0f * AX_DEG_TO_RAD;
    float nearPlane = 0.01f;
    float farPlane = 10000.0f;

    float lookSensitivity = 0.0030f;
    float orbitSensitivity = 0.0045f;
    float flySpeed = 5.0f;
    float fastFlyMultiplier = 4.0f;
    float scrollDollySensitivity = 0.12f;

    ViewportCamera() {
        SetLookAt(position, focus);
    }

    Vec3 Forward() const {
        const float cp = std::cos(pitchRadians);
        return Normalize({
            cp * std::sin(yawRadians),
            std::sin(pitchRadians),
            -cp * std::cos(yawRadians)
        });
    }

    Vec3 Right() const {
        return Normalize(Cross(Forward(), WorldUp()));
    }

    Vec3 Up() const {
        return Normalize(Cross(Right(), Forward()));
    }

    Mat4 ViewMatrix() const {
        return Mat4::LookAtRH(position, position + Forward(), WorldUp());
    }

    Mat4 ProjectionMatrix(float aspect) const {
        return Mat4::PerspectiveVulkanRH(verticalFovRadians, aspect, nearPlane, farPlane);
    }

    Mat4 ProjectionMatrix(int viewportWidth, int viewportHeight) const {
        const float aspect = viewportHeight > 0
            ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
            : 1.0f;
        return ProjectionMatrix(aspect);
    }

    void SetLookAt(const Vec3& eye, const Vec3& target) {
        position = eye;
        focus = target;
        orbitDistance = std::max(Length(target - eye), 0.05f);

        const Vec3 dir = Normalize(target - eye);
        pitchRadians = std::asin(Clamp(dir.y, -1.0f, 1.0f));
        yawRadians = std::atan2(dir.x, -dir.z);
        ClampPitch();
    }

    void FocusOn(const Vec3& center, float radius) {
        focus = center;
        orbitDistance = std::max(radius * 2.5f, 0.35f);
        position = focus - Forward() * orbitDistance;
    }

    void Update(const ViewportCameraInput& in) {
        if (in.viewportHovered && in.focusPressed && in.hasSelection) {
            FocusOn(in.selectionCenter, std::max(in.selectionRadius, 0.05f));
        }

        if (!in.viewportHovered && !in.rightMouseDown && !in.leftMouseDown && !in.middleMouseDown) {
            return;
        }

        if (in.altDown && in.leftMouseDown) {
            OrbitPixels(in.mouseDeltaX, in.mouseDeltaY);
        } else if (in.altDown && in.middleMouseDown) {
            PanPixels(in.mouseDeltaX, in.mouseDeltaY, in.viewportHeight);
        } else if (in.rightMouseDown) {
            LookPixels(in.mouseDeltaX, in.mouseDeltaY);
            Fly(in);
        }

        if (in.viewportHovered && std::abs(in.wheelDelta) > 0.0001f) {
            DollyWheel(in.wheelDelta);
        }
    }

private:
    static Vec3 WorldUp() { return {0.0f, 1.0f, 0.0f}; }

    void ClampPitch() {
        const float limit = 89.0f * AX_DEG_TO_RAD;
        pitchRadians = Clamp(pitchRadians, -limit, limit);
    }

    void LookPixels(float dx, float dy) {
        yawRadians += dx * lookSensitivity;
        pitchRadians -= dy * lookSensitivity;
        ClampPitch();

        // Preserve the current orbit distance so F-focus and Alt-orbit remain coherent
        // after a first-person fly rotation.
        focus = position + Forward() * orbitDistance;
    }

    void OrbitPixels(float dx, float dy) {
        yawRadians += dx * orbitSensitivity;
        pitchRadians -= dy * orbitSensitivity;
        ClampPitch();
        position = focus - Forward() * orbitDistance;
    }

    void PanPixels(float dx, float dy, int viewportHeight) {
        const float h = static_cast<float>(std::max(viewportHeight, 1));
        const float worldUnitsPerPixel =
            (2.0f * std::tan(verticalFovRadians * 0.5f) * orbitDistance) / h;

        const Vec3 delta =
            Right() * (-dx * worldUnitsPerPixel) +
            Up() * (dy * worldUnitsPerPixel);

        position += delta;
        focus += delta;
    }

    void DollyWheel(float wheelDelta) {
        const float scaled = std::max(orbitDistance, 0.25f) * scrollDollySensitivity;
        orbitDistance = std::max(0.05f, orbitDistance - wheelDelta * scaled);
        position = focus - Forward() * orbitDistance;
    }

    void Fly(const ViewportCameraInput& in) {
        Vec3 move;
        if (in.keyW) move += Forward();
        if (in.keyS) move -= Forward();
        if (in.keyD) move += Right();
        if (in.keyA) move -= Right();
        if (in.keyE) move += WorldUp();
        if (in.keyQ) move -= WorldUp();

        if (Length(move) <= 0.0001f) {
            return;
        }

        const float multiplier = in.shiftDown ? fastFlyMultiplier : 1.0f;
        const float distance = flySpeed * multiplier * std::max(in.deltaSeconds, 0.0f);
        const Vec3 delta = Normalize(move) * distance;
        position += delta;
        focus = position + Forward() * orbitDistance;
    }
};

} // namespace anim8orx
