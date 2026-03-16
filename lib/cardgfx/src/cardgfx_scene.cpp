#include "cardgfx_scene.h"
#include <cstring>

namespace CardGFX {

// ── Scene ────────────────────────────────────────────────────────

Scene::Scene(const char* name) : m_name(name) {}

bool Scene::addWidget(Widget* widget, bool focusable) {
    if (!widget || m_widgetCount >= MAX_WIDGETS_PER_SCENE) return false;

    // Check for duplicates
    for (uint8_t i = 0; i < m_widgetCount; i++) {
        if (m_widgets[i] == widget) return false;
    }

    m_widgets[m_widgetCount++] = widget;

    if (focusable && widget->isFocusable()) {
        m_focusChain.add(widget);
    }
    return true;
}

void Scene::removeWidget(Widget* widget) {
    if (!widget) return;
    m_focusChain.remove(widget);

    for (uint8_t i = 0; i < m_widgetCount; i++) {
        if (m_widgets[i] == widget) {
            for (uint8_t j = i; j < m_widgetCount - 1; j++) {
                m_widgets[j] = m_widgets[j + 1];
            }
            m_widgetCount--;
            m_widgets[m_widgetCount] = nullptr;
            return;
        }
    }
}

Widget* Scene::widgetById(uint8_t id) {
    for (uint8_t i = 0; i < m_widgetCount; i++) {
        if (m_widgets[i]->id() == id) return m_widgets[i];
    }
    return nullptr;
}

void Scene::tickWidgets(uint32_t dt_ms) {
    for (uint8_t i = 0; i < m_widgetCount; i++) {
        if (m_widgets[i]->isVisible()) {
            m_widgets[i]->onTick(dt_ms);
        }
    }
}

void Scene::drawWidgets(Canvas& framebuffer, const Theme& theme) {
    // Pre-pass: detect widgets that just became hidden (dirty + not visible).
    // Clear their region and force visible widgets to redraw over it.
    bool anyHidden = false;
    for (uint8_t i = 0; i < m_widgetCount; i++) {
        Widget* w = m_widgets[i];
        if (!w->isVisible() && w->isDirty()) {
            const Bounds& b = w->bounds();
            if (b.w > 0 && b.h > 0) {
                framebuffer.fillRect(b.x, b.y, b.w, b.h, theme.bgPrimary);
            }
            w->clearDirty();
            anyHidden = true;
        }
    }
    if (anyHidden) {
        for (uint8_t i = 0; i < m_widgetCount; i++) {
            if (m_widgets[i]->isVisible()) {
                m_widgets[i]->markDirty();
            }
        }
    }

    // Draw visible dirty widgets into temporary canvases and blit to framebuffer.
    for (uint8_t i = 0; i < m_widgetCount; i++) {
        Widget* w = m_widgets[i];
        if (!w->isVisible()) continue;
        if (!w->isDirty()) continue;

        const Bounds& b = w->bounds();
        if (b.w == 0 || b.h == 0) continue;

        // Create a temporary canvas sized to the widget
        Canvas widgetCanvas;
        bool usePsram = ((uint32_t)b.w * b.h * 2) > 4096;
        if (!widgetCanvas.create(b.w, b.h, usePsram)) continue;

        // Fill with background
        widgetCanvas.fill(theme.bgPrimary);

        // Let the widget draw
        w->onDraw(widgetCanvas, theme);

        // Blit to framebuffer
        framebuffer.blit(widgetCanvas, b.x, b.y);

        w->clearDirty();
        // widgetCanvas automatically destroyed at scope exit
    }
}

bool Scene::routeInput(const InputEvent& event) {
    // Bare ;,./ → arrow keys (Cardputer has no hardware arrows).
    // Suppressed when a text-accepting widget is focused.
    InputEvent mapped = event;
    Widget* focused = m_focusChain.focused();
    if (!(focused && focused->acceptsText())) {
        switch (event.key) {
        case ';': mapped.key = Key::UP;    mapped.character = 0; break;
        case '.': mapped.key = Key::DOWN;  mapped.character = 0; break;
        case ',': mapped.key = Key::LEFT;  mapped.character = 0; break;
        case '/': mapped.key = Key::RIGHT; mapped.character = 0; break;
        }
    }

    // First, try the focused widget
    if (focused && focused->isVisible() && focused->onInput(mapped)) {
        return true;
    }

    // Tab to change focus
    if (mapped.isDown() && mapped.key == Key::TAB) {
        if (mapped.hasShift()) {
            m_focusChain.focusPrev();
        } else {
            m_focusChain.focusNext();
        }
        return true;
    }

    // Fall through to scene handler
    return onInput(mapped);
}

// ── Scene Manager ────────────────────────────────────────────────

SceneManager::SceneManager() = default;

bool SceneManager::registerScene(Scene* scene) {
    if (!scene || m_registeredCount >= MAX_SCENES_REGISTERED) return false;
    m_registered[m_registeredCount++] = scene;
    return true;
}

bool SceneManager::push(Scene* scene, Transition transition) {
    if (!scene || m_stackDepth >= MAX_SCENE_STACK) return false;

    // Exit current scene
    if (m_stackDepth > 0) {
        m_stack[m_stackDepth - 1]->onExit();
    }

    // Push new scene
    m_stack[m_stackDepth++] = scene;
    scene->onEnter();

    if (transition != Transition::None) {
        startTransition(transition);
    }
    return true;
}

bool SceneManager::pushByName(const char* name, Transition transition) {
    Scene* scene = findByName(name);
    if (!scene) return false;
    return push(scene, transition);
}

Scene* SceneManager::pop(Transition transition) {
    if (m_stackDepth == 0) return nullptr;

    Scene* popped = m_stack[--m_stackDepth];
    popped->onExit();
    m_stack[m_stackDepth] = nullptr;

    // Resume the scene underneath
    if (m_stackDepth > 0) {
        m_stack[m_stackDepth - 1]->onEnter();
    }

    if (transition != Transition::None) {
        startTransition(transition);
    }
    return popped;
}

bool SceneManager::replace(Scene* scene, Transition transition) {
    if (!scene) return false;
    if (m_stackDepth > 0) {
        m_stack[m_stackDepth - 1]->onExit();
        m_stack[m_stackDepth - 1] = scene;
    } else {
        m_stack[m_stackDepth++] = scene;
    }
    scene->onEnter();

    if (transition != Transition::None) {
        startTransition(transition);
    }
    return true;
}

Scene* SceneManager::active() const {
    if (m_stackDepth == 0) return nullptr;
    return m_stack[m_stackDepth - 1];
}

Scene* SceneManager::findByName(const char* name) const {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < m_registeredCount; i++) {
        if (strcmp(m_registered[i]->name(), name) == 0) {
            return m_registered[i];
        }
    }
    return nullptr;
}

void SceneManager::processFrame(uint32_t dt_ms, InputManager& input,
                                Canvas& fb, const Theme& theme) {
    Scene* current = active();
    if (!current) return;

    // Clear framebuffer when switching to a different scene
    if (current != m_lastRendered) {
        fb.fill(theme.bgPrimary);
        m_lastRendered = current;
    }

    // 1. Tick
    current->onTick(dt_ms);
    current->tickWidgets(dt_ms);

    // 2. Process input
    while (input.hasEvents()) {
        InputEvent event = input.nextEvent();

        // Scene-level bindings checked by the caller (or we could
        // integrate InputManager bindings here)
        if (input.processBindings(event)) continue;

        // Route to scene's widget focus chain
        current->routeInput(event);
    }

    // Re-check active scene — input handlers may have pushed/popped
    Scene* afterInput = active();
    if (afterInput != current) {
        current = afterInput;
        if (!current) return;
        fb.fill(theme.bgPrimary);
        m_lastRendered = current;
    }

    // 3. Draw
    current->drawWidgets(fb, theme);
    current->onDrawOverlay(fb, theme);

    // 4. Transition overlay
    if (m_transitioning) {
        updateTransition(dt_ms, fb);
    }

    // 5. Push framebuffer to screen
    fb.pushToScreen(0, 0, false);  // Full push for simplicity
}

void SceneManager::startTransition(Transition t) {
    m_transitioning = true;
    m_transitionType = t;
    m_transitionTime = 0;
}

void SceneManager::updateTransition(uint32_t dt_ms, Canvas& fb) {
    m_transitionTime += dt_ms;

    float progress = (float)m_transitionTime / (float)m_transitionDuration;
    if (progress >= 1.0f) {
        m_transitioning = false;
        return;
    }

    switch (m_transitionType) {
    case Transition::FadeBlack: {
        // Darken the framebuffer by blending toward black
        // In the first half, fade out; in the second half, the new scene
        // is already drawn so it fades in naturally.
        if (progress < 0.5f) {
            float alpha = progress * 2.0f;  // 0 → 1
            uint16_t* buf = fb.buffer();
            uint32_t count = (uint32_t)fb.width() * fb.height();
            uint8_t invAlpha = (uint8_t)((1.0f - alpha) * 31);
            for (uint32_t i = 0; i < count; i++) {
                uint16_t c = buf[i];
                uint8_t r = ((c >> 11) & 0x1F) * invAlpha / 31;
                uint8_t g = ((c >> 5) & 0x3F) * invAlpha / 31;
                uint8_t b = (c & 0x1F) * invAlpha / 31;
                buf[i] = (r << 11) | (g << 5) | b;
            }
            fb.markAllDirty();
        }
        break;
    }
    default:
        // Other transitions can be added later
        m_transitioning = false;
        break;
    }
}

} // namespace CardGFX
