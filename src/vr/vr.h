// VR mode (2026-11-03, user: "using the player hand and gun along with the crosshair shown"; docs/vr.md).
//
// The game stays the arcade game: its camera is authoritative (on rails), the player's head turns and leans on top of
// it, the right controller is the light gun (its ray picks the arcade screen point the game shoots at), the 2D HUD
// sits on the arcade's own screen plane in front of the player. This header is the backend-independent part:
//  - the per-frame data a headset runtime hands over (head pose, two eyes with their own frusta and render targets,
//    controller poses and buttons), in the runtime's tracking space: OpenXR's convention (right-handed, metres,
//    +x right, +y up, -z ahead; quaternions x, y, z, w);
//  - Backend: "none" (VR off: nothing of this runs), "preview" (a desktop simulation: side-by-side stereo in the window,
//    the head on keys, the gun on the mouse; vr_preview.cpp) and "openxr" (vr_openxr.cpp: a headset; built when CMake
//    finds the OpenXR SDK: HOTD_OPENXR);
//  - Settings (comfort, scale, aim, the gun / hand models: data/vr/vr.ini).
// The game world is left-handed (+z ahead of the camera, game units): vr_math.h converts.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

union SDL_Event;

namespace hotd::vr {

struct Pose {
    float pos[3] = {0, 0, 0};       // metres, tracking space
    float rot[4] = {0, 0, 0, 1};    // unit quaternion x, y, z, w (tracking space)
    bool valid = false;
};

// an eye's frustum: the tangents of its edge angles (OpenXR XrFovf: angleLeft / angleDown negative)
struct Fov {
    float tanLeft = -1.0f, tanRight = 1.0f, tanUp = 1.0f, tanDown = -1.0f;
};

struct Eye {
    Pose pose;                  // the eye in tracking space (the head pose + half the IPD)
    Fov fov;
    int width = 0, height = 0;  // its render target's size (pixels)
};

struct Controller {
    bool active = false;
    Pose grip;                  // OpenXR /input/grip/pose: the hand's palm (the gun is held there)
    Pose aim;                   // OpenXR /input/aim/pose: the pointing ray (-z); the preview sets it = the gun's barrel
    float trigger = 0.0f;       // 0..1
    float squeeze = 0.0f;       // 0..1 (the grip button)
    bool primary = false, secondary = false, menu = false, stickClick = false;   // A / B (X / Y), menu, thumbstick press
    float stick[2] = {0, 0};
};

struct FrameState {
    bool shouldRender = true;   // OpenXR xrWaitFrame's shouldRender (false: the headset is not showing us; submit empty)
    int64_t displayTime = 0;    // the predicted display time (OpenXR XrTime; the preview: SDL nanoseconds)
    Pose head;
    Eye eye[2];                 // 0 left, 1 right
    Controller hand[2];         // 0 left, 1 right (the gun)
};

// comfort and play settings (data/vr/vr.ini [vr]; docs/vr.md "Comfort")
struct Settings {
    // how the player's space follows the game camera: 0 follow (the game camera's full orientation, the head on top;
    // the arcade's framing), 1 level horizon (the game camera's position and heading only: its pitch is not forced on
    // the player's head; comfortable on the arcade's steep looks up / down)
    int follow = 0;
    bool vignette = true;           // darken the view's edges while the game camera turns / moves fast
    float vignetteStrength = 0.7f;  // 0..1 at the fastest
    float vignetteTurn = 60.0f;     // degrees / second where the vignette starts
    float unitsPerMetre = 10.0f;    // game units per metre (the world's scale; docs/vr.md "Scale")
    float panelDistance = 3.0f;     // metres: the HUD's screen plane (the arcade screen) in front of the player
    bool panelDepthTest = false;    // the HUD hidden behind nearer walls (off: always on top, the usual VR HUD)
    float aimDistance = 8.0f;       // metres: the aim's distance when the ray hits nothing the eye's depth knows
    bool laser = true;              // a faint laser from the muzzle to the crosshair
    float crosshairDegrees = 2.5f;  // the 3D crosshair's angular size (the same size at any distance)
    bool reloadDown = true;         // pointing the gun steeply down reloads (as a shot off the screen)
    float reloadPitch = 55.0f;      // degrees below the horizon for reloadDown / the off-screen state
    bool leftHand = true;           // draw the left hand (when tracked)
    // the game's hand models on the controllers (vr_scene.cpp's anchors) moved / turned from their default place:
    // metres along the pose's axes (x right, y up, -z ahead), degrees pitch / yaw / roll (vr.ini gun_offset,
    // gun_rotate, left_hand_offset, left_hand_rotate: "x y z")
    float gunOffset[3] = {0, 0, 0}, gunRotate[3] = {0, 0, 0};
    float leftOffset[3] = {0, 0, 0}, leftRotate[3] = {0, 0, 0};
    float ipd = 0.064f;             // the preview's eye distance (metres; a headset reports its own)
    // quest (2026-11-03, docs/quest.md): the flat screens (setup, menus, the end screen) on a big screen in VR, the
    // right controller's ray as the mouse (trigger = click; B / Y / menu = Esc; A = Enter). Default on Android only
    // (a PC player has the desktop window and the mouse)
#if defined(__ANDROID__)
    bool menuPanel = true;
#else
    bool menuPanel = false;
#endif
    float menuDistance = 2.5f;      // metres ahead of the start pose
    float menuWidth = 3.2f;         // metres (the height from the window's shape)
    // performance (the Quest 3: 72 Hz): the eyes' resolution x this (0.5..1.5 of the runtime's recommended size);
    // the HUD panel's lines (960: sharp; 720 / 540 cheaper: it is mipmapped every frame); the flat screens' 3D with the
    // arcade's polygon sort (the Draw Depth setting; off: the depth buffer, cheaper on OpenGL ES where the sort's keys
    // are a float texture updated per frame; VR's eyes never sort); srgb_swapchain off: an RGBA8 swapchain (a check
    // on the headset if the colours look wrong)
    float resolutionScale = 1.0f;
    int hudLines = 960;
#if defined(__ANDROID__)
    bool arcadeSort = false;
#else
    bool arcadeSort = true;
#endif
    bool srgbSwapchain = true;
    bool load(const std::string& path);   // missing keys keep their defaults; false: no file
};

// the per-frame interface every backend implements (GL context current on the calling thread)
class Backend {
public:
    virtual ~Backend() = default;
    virtual const char* name() const = 0;
    // after the GL context exists
    virtual bool init(const Settings& s, std::string& err) = 0;
    virtual void shutdown() = 0;
    // window events (the preview's keys / mouse); true: used (not passed on to the game)
    virtual bool event(const SDL_Event&) { return false; }
    // the poses for the frame about to be drawn (OpenXR: xrWaitFrame + xrBeginFrame + xrLocateViews + xrSyncActions)
    virtual bool beginFrame(FrameState& fs) = 0;
    // the GL framebuffer (colour + depth) to draw eye `i` into, sized fs.eye[i].width x height (OpenXR: the acquired
    // swapchain image wrapped in an FBO)
    virtual unsigned eyeFramebuffer(int i) = 0;
    virtual unsigned eyeTexture(int i) = 0;   // its colour texture (the desktop mirror)
    // the eyes are drawn: hand them over (OpenXR: release the images, xrEndFrame with a projection layer)
    virtual void endFrame(const FrameState& fs) = 0;
    // the desktop window's picture (the preview: the stereo pair; OpenXR: a mirror of one eye) into framebuffer `fbo`
    virtual void present(unsigned fbo, int width, int height) = 0;
    // openxr: a controller's rumble (hand 0 left, 1 right; amplitude 0..1): the gun's kick on a shot
    virtual void haptic(int, float, float) {}
    // openxr: the runtime ended the session for good (EXITING / LOSS_PENDING: quit from its dashboard, the headset
    // unplugged): the game drops VR and goes on flat (main.cpp vrFrame)
    virtual bool lost() const { return false; }

    // ---- the desktop preview's extras (a headset backend leaves them alone) ----
    // the window's size before beginFrame (the preview sizes its eyes from it)
    virtual void setOutputSize(int, int) {}
    // the mouse as a ray from the eye it is over (tracking space, unit dir); the scene measures how far it reaches in
    // that eye's depth and hands it back (cursorDistance), so the preview's gun aims at what the cursor shows
    virtual bool desktopCursorRay(float[3], float[3]) { return false; }
    virtual void cursorDistance(float) {}
    // tests: the cursor put at (u, v) of eye `eye`'s picture (0..1 from the top-left); the head held at yaw / pitch /
    // roll (degrees, right-handed: +yaw turns left) and leaned by (x, y, z) metres
    virtual void setCursor(int, float, float) {}
    virtual void scriptHead(const float[3], const float[3]) {}
    int display = 2;   // the desktop picture: 0 the left eye, 1 the right eye, 2 both side by side
};

std::unique_ptr<Backend> makePreview();
std::unique_ptr<Backend> makeOpenXr();   // null when built without HOTD_OPENXR
std::unique_ptr<Backend> make(const std::string& name);   // "preview" / "openxr"; null: unknown / not built

} // namespace hotd::vr
