#ifndef CARDGFX_WIDGET_GRID_H
#define CARDGFX_WIDGET_GRID_H

#include "../cardgfx_widget.h"
#include <functional>

namespace CardGFX {

/**
 * Grid: an interactive 2D grid with cursor navigation.
 *
 * This is the core widget for chessboards, tile-based UIs, settings grids,
 * and any 2D navigable surface. Each cell can be rendered with custom content
 * via a callback.
 *
 * Usage:
 *   Grid board;
 *   board.setGridSize(8, 8);
 *   board.setCellSize(16, 16);
 *   board.setCellRenderer([](Canvas& c, uint8_t col, uint8_t row,
 *                            uint8_t cellW, uint8_t cellH, bool selected,
 *                            const Theme& theme, void* ctx) {
 *       // Draw cell content
 *   });
 */
class Grid : public Widget {
public:
    static constexpr uint8_t MAX_COLS = 16;
    static constexpr uint8_t MAX_ROWS = 16;

    /**
     * Cell renderer callback.
     *
     * @param canvas   Canvas to draw into (full widget canvas, NOT clipped to cell).
     * @param col, row Cell coordinates.
     * @param cx, cy   Pixel position of the cell's top-left within the canvas.
     * @param cellW, cellH  Cell dimensions in pixels.
     * @param state    Cell state flags.
     * @param theme    Active theme.
     * @param context  User-provided context pointer.
     */
    struct CellState {
        bool cursor    : 1;  // Cursor is on this cell
        bool selected  : 1;  // Cell is selected/picked up
        bool highlight : 1;  // Secondary highlight (valid moves, etc.)
        bool marked    : 1;  // Tertiary mark (last move, etc.)
    };

    using CellRenderer = std::function<void(
        Canvas& canvas, uint8_t col, uint8_t row,
        int16_t cx, int16_t cy, uint8_t cellW, uint8_t cellH,
        CellState state, const Theme& theme, void* context
    )>;

    using CellAction = std::function<void(uint8_t col, uint8_t row)>;

    Grid() { m_focusable = true; }

    // ── Configuration ────────────────────────────────────────────

    void setGridSize(uint8_t cols, uint8_t rows) {
        m_cols = (cols <= MAX_COLS) ? cols : MAX_COLS;
        m_rows = (rows <= MAX_ROWS) ? rows : MAX_ROWS;
        // Auto-set bounds from grid size if not set
        if (m_bounds.w == 0) m_bounds.w = m_cols * m_cellW;
        if (m_bounds.h == 0) m_bounds.h = m_rows * m_cellH;
        markDirty();
    }

    void setCellSize(uint8_t w, uint8_t h) {
        m_cellW = w;
        m_cellH = h;
        markDirty();
    }

    void setCellRenderer(CellRenderer renderer) { m_renderer = renderer; }
    void setOnAction(CellAction action) { m_onAction = action; }
    void setContext(void* ctx) { m_context = ctx; }

    void setDrawBorder(bool draw) { m_drawBorder = draw; }
    void setDrawGrid(bool draw) { m_drawGridLines = draw; }

    // ── Cursor ───────────────────────────────────────────────────

    uint8_t cursorCol() const { return m_cursorCol; }
    uint8_t cursorRow() const { return m_cursorRow; }

    void setCursor(uint8_t col, uint8_t row) {
        m_cursorCol = (col < m_cols) ? col : m_cols - 1;
        m_cursorRow = (row < m_rows) ? row : m_rows - 1;
        markDirty();
    }

    // ── Cell State ───────────────────────────────────────────────

    void setSelected(uint8_t col, uint8_t row, bool selected) {
        if (col < m_cols && row < m_rows) {
            m_cellFlags[row][col].selected = selected;
            markDirty();
        }
    }

    void setHighlighted(uint8_t col, uint8_t row, bool highlight) {
        if (col < m_cols && row < m_rows) {
            m_cellFlags[row][col].highlight = highlight;
            markDirty();
        }
    }

    void setMarked(uint8_t col, uint8_t row, bool marked) {
        if (col < m_cols && row < m_rows) {
            m_cellFlags[row][col].marked = marked;
            markDirty();
        }
    }

    void clearAllSelected() {
        for (uint8_t r = 0; r < MAX_ROWS; r++)
            for (uint8_t c = 0; c < MAX_COLS; c++)
                m_cellFlags[r][c].selected = false;
        markDirty();
    }

    void clearAllHighlights() {
        for (uint8_t r = 0; r < MAX_ROWS; r++)
            for (uint8_t c = 0; c < MAX_COLS; c++)
                m_cellFlags[r][c].highlight = false;
        markDirty();
    }

    void clearAllMarks() {
        for (uint8_t r = 0; r < MAX_ROWS; r++)
            for (uint8_t c = 0; c < MAX_COLS; c++)
                m_cellFlags[r][c].marked = false;
        markDirty();
    }

    void clearAllFlags() {
        memset(m_cellFlags, 0, sizeof(m_cellFlags));
        markDirty();
    }

    CellState getCellState(uint8_t col, uint8_t row) const {
        if (col >= m_cols || row >= m_rows) return {};
        CellState state = m_cellFlags[row][col];
        state.cursor = (col == m_cursorCol && row == m_cursorRow);
        return state;
    }

    // ── Lifecycle ────────────────────────────────────────────────

    void onDraw(Canvas& canvas, const Theme& theme) override {
        // Background
        canvas.fill(theme.bgPrimary);

        // Draw each cell
        for (uint8_t r = 0; r < m_rows; r++) {
            for (uint8_t c = 0; c < m_cols; c++) {
                int16_t cx = c * m_cellW;
                int16_t cy = r * m_cellH;

                CellState state = getCellState(c, r);

                // Default cell background (checkerboard pattern)
                if (!m_renderer) {
                    bool lightCell = ((c + r) % 2 == 0);
                    uint16_t cellBg = lightCell ? theme.gridCellA : theme.gridCellB;

                    if (state.highlight) cellBg = theme.gridHighlight;
                    if (state.marked)    cellBg = theme.accentMuted;
                    if (state.selected)  cellBg = theme.accentActive;

                    canvas.fillRect(cx, cy, m_cellW, m_cellH, cellBg);

                    // Cursor outline
                    if (state.cursor && m_focused) {
                        canvas.drawRect(cx, cy, m_cellW, m_cellH,
                                        theme.gridCursor);
                        // Inner outline for visibility
                        if (m_cellW > 4 && m_cellH > 4) {
                            canvas.drawRect(cx + 1, cy + 1,
                                            m_cellW - 2, m_cellH - 2,
                                            theme.gridCursor);
                        }
                    }
                } else {
                    // Custom renderer handles everything
                    m_renderer(canvas, c, r, cx, cy,
                               m_cellW, m_cellH, state, theme, m_context);
                }
            }
        }

        // Grid lines
        if (m_drawGridLines) {
            for (uint8_t c = 0; c <= m_cols; c++) {
                canvas.drawVLine(c * m_cellW, 0,
                                 m_rows * m_cellH, theme.border);
            }
            for (uint8_t r = 0; r <= m_rows; r++) {
                canvas.drawHLine(0, r * m_cellH,
                                 m_cols * m_cellW, theme.border);
            }
        }

        // Outer border
        if (m_drawBorder) {
            canvas.drawRect(0, 0, m_bounds.w, m_bounds.h,
                            m_focused ? theme.borderFocus : theme.border);
        }
    }

    bool onInput(const InputEvent& event) override {
        if (!event.isDown() && !event.isRepeat()) return false;

        switch (event.key) {
        case Key::UP:
            if (m_cursorRow > 0) { m_cursorRow--; markDirty(); }
            return true;
        case Key::DOWN:
            if (m_cursorRow < m_rows - 1) { m_cursorRow++; markDirty(); }
            return true;
        case Key::LEFT:
            if (m_cursorCol > 0) { m_cursorCol--; markDirty(); }
            return true;
        case Key::RIGHT:
            if (m_cursorCol < m_cols - 1) { m_cursorCol++; markDirty(); }
            return true;
        case Key::ENTER:
        case Key::SPACE:
            if (m_onAction) {
                m_onAction(m_cursorCol, m_cursorRow);
            }
            return true;
        default:
            return false;
        }
    }

    void onFocus(bool gained) override {
        markDirty();  // Redraw cursor visibility
    }

private:
    uint8_t m_cols = 8;
    uint8_t m_rows = 8;
    uint8_t m_cellW = 16;
    uint8_t m_cellH = 16;

    uint8_t m_cursorCol = 0;
    uint8_t m_cursorRow = 0;

    bool m_drawBorder = false;
    bool m_drawGridLines = false;

    CellState m_cellFlags[MAX_ROWS][MAX_COLS] = {};

    CellRenderer m_renderer = nullptr;
    CellAction   m_onAction = nullptr;
    void*        m_context  = nullptr;
};

} // namespace CardGFX

#endif // CARDGFX_WIDGET_GRID_H
