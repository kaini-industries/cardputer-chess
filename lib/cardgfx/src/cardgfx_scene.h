#ifndef CARDGFX_SCENE_H
#define CARDGFX_SCENE_H

#include "cardgfx_config.h"
#include "cardgfx_canvas.h"
#include "cardgfx_widget.h"
#include "cardgfx_input.h"
#include "cardgfx_theme.h"
#include <cstdint>

namespace CardGFX {

/**
 * A Scene is a self-contained screen with its own widget set,
 * focus chain, and input bindings.
 *
 * Subclass Scene and override the lifecycle methods.
 */
class Scene {
public:
    Scene(const char* name = "unnamed");
    virtual ~Scene() = default;

    const char* name() const { return m_name; }

    // ── Widget Management ────────────────────────────────────────

    /**
     * Add a widget to this scene.
     * @param widget    Pointer to widget (scene does NOT own it).
     * @param focusable If true, also add to the focus chain.
     * @return true if added.
     */
    bool addWidget(Widget* widget, bool focusable = false);

    /**
     * Remove a widget from this scene.
     */
    void removeWidget(Widget* widget);

    /**
     * Get a widget by ID. Returns nullptr if not found.
     */
    Widget* widgetById(uint8_t id);

    /** Focus chain for this scene. */
    FocusChain& focusChain() { return m_focusChain; }

    // ── Lifecycle (override these) ───────────────────────────────

    /** Called when this scene becomes active (pushed or uncovered). */
    virtual void onEnter() {}

    /** Called when this scene is deactivated (popped or covered). */
    virtual void onExit() {}

    /** Called every frame. Process game logic here. */
    virtual void onTick(uint32_t dt_ms) {}

    /**
     * Called after all widgets have drawn.
     * Use for overlay drawing (cursors, debug info, etc.)
     */
    virtual void onDrawOverlay(Canvas& framebuffer, const Theme& theme) {}

    /**
     * Handle input that wasn't consumed by widgets or bindings.
     * @return true if consumed.
     */
    virtual bool onInput(const InputEvent& event) { return false; }

    // ── Internal (called by SceneManager) ────────────────────────

    void tickWidgets(uint32_t dt_ms);
    void drawWidgets(Canvas& framebuffer, const Theme& theme);
    bool routeInput(const InputEvent& event);

protected:
    const char* m_name;
    Widget*     m_widgets[MAX_WIDGETS_PER_SCENE] = {};
    uint8_t     m_widgetCount = 0;
    FocusChain  m_focusChain;
};

// ── Transition ───────────────────────────────────────────────────

enum class Transition : uint8_t {
    None,       // Instant switch
    FadeBlack,  // Fade to black, then fade in
    SlideLeft,  // Slide outgoing left, incoming from right
    SlideRight, // Slide outgoing right, incoming from left
};

// ── Scene Manager ────────────────────────────────────────────────

/**
 * Manages a stack of scenes with push/pop semantics.
 *
 * Pushing a scene (e.g., a modal dialog) pauses the scene underneath
 * without destroying it. Popping returns to exactly where you left off.
 *
 * Only the top scene receives input and tick events.
 * Drawing can optionally render the scene below (for transparent overlays).
 */
class SceneManager {
public:
    SceneManager();

    /**
     * Register a scene so it can be referenced by name.
     * The SceneManager does NOT own the scene pointer.
     */
    bool registerScene(Scene* scene);

    /**
     * Push a scene onto the stack by pointer.
     * Calls onExit() on the current scene and onEnter() on the new one.
     */
    bool push(Scene* scene, Transition transition = Transition::None);

    /**
     * Push a registered scene by name.
     */
    bool pushByName(const char* name, Transition transition = Transition::None);

    /**
     * Pop the top scene. Returns to the scene underneath.
     * @return The popped scene pointer, or nullptr if stack was empty.
     */
    Scene* pop(Transition transition = Transition::None);

    /**
     * Replace the top scene (pop + push in one operation).
     */
    bool replace(Scene* scene, Transition transition = Transition::None);

    /**
     * Get the currently active (top) scene.
     */
    Scene* active() const;

    /**
     * Get a registered scene by name.
     */
    Scene* findByName(const char* name) const;

    /** Is the scene stack empty? */
    bool empty() const { return m_stackDepth == 0; }

    /** Current stack depth. */
    uint8_t depth() const { return m_stackDepth; }

    // ── Frame Processing ─────────────────────────────────────────

    /**
     * Process one frame: tick, input routing, drawing.
     * @param dt_ms     Delta time in milliseconds.
     * @param input     Input manager to pull events from.
     * @param fb        Framebuffer canvas to draw into.
     * @param theme     Active theme.
     */
    void processFrame(uint32_t dt_ms, InputManager& input,
                      Canvas& fb, const Theme& theme);

    /**
     * Check if a transition animation is in progress.
     */
    bool isTransitioning() const { return m_transitioning; }

private:
    // Registered scenes (for lookup by name)
    Scene*  m_registered[MAX_SCENES_REGISTERED] = {};
    uint8_t m_registeredCount = 0;

    // Scene stack
    Scene*  m_stack[MAX_SCENE_STACK] = {};
    uint8_t m_stackDepth = 0;

    // Last rendered scene (to detect scene changes and clear framebuffer)
    Scene*     m_lastRendered = nullptr;

    // Transition state
    bool       m_transitioning = false;
    Transition m_transitionType = Transition::None;
    uint32_t   m_transitionTime = 0;
    uint32_t   m_transitionDuration = 200; // ms

    void startTransition(Transition t);
    void updateTransition(uint32_t dt_ms, Canvas& fb);
};

} // namespace CardGFX

#endif // CARDGFX_SCENE_H
