#include "ui/TabBook.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <wx/control.h>
#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>

namespace txt {

wxDEFINE_EVENT(EVT_TAB_CHANGED, wxBookCtrlEvent);
wxDEFINE_EVENT(EVT_TAB_CLOSE, wxBookCtrlEvent);

namespace {

constexpr int kBarHeightDip = 38;
constexpr int kTopMarginDip = 6;
constexpr int kSideMarginDip = 6;
constexpr int kTabGapDip = 2;
constexpr int kTextPaddingDip = 12;
constexpr int kCloseGapDip = 4;
constexpr int kCloseDiameterDip = 20;
constexpr int kCloseRightDip = 7;
constexpr int kMinTabWidthDip = 108;
constexpr int kMaxTabWidthDip = 300;
constexpr int kCornerRadiusDip = 7;

struct TabPalette {
    wxColour barBg;
    wxColour activeBg;
    wxColour hoverBg;
    wxColour border;
    wxColour text;
    wxColour inactiveText;
    wxColour close;
    wxColour inactiveClose;
    wxColour closeHoverBg;
    wxColour closeHoverFg;
    wxColour accent;
};

TabPalette MakeTabPalette(bool dark) {
    const wxColour accent = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    if (dark) {
        return {
            wxColour(22, 22, 22),    wxColour(30, 30, 30),
            wxColour(45, 45, 45),    wxColour(52, 52, 52),
            wxColour(232, 232, 232), wxColour(158, 158, 158),
            wxColour(190, 190, 190), wxColour(135, 135, 135),
            wxColour(70, 70, 70),    wxColour(245, 245, 245),
            accent.IsOk() ? accent : wxColour(86, 156, 214),
        };
    }
    return {
        wxColour(238, 240, 243), wxColour(255, 255, 255),
        wxColour(225, 228, 232), wxColour(205, 208, 213),
        wxColour(28, 28, 30),    wxColour(100, 102, 106),
        wxColour(100, 102, 106), wxColour(145, 147, 151),
        wxColour(210, 214, 219), wxColour(28, 28, 30),
        accent.IsOk() ? accent : wxColour(0, 103, 192),
    };
}

int TabTextWidth(const wxString& title, const wxDC& dc) {
    return dc.GetTextExtent(title).GetWidth();
}

}  // namespace

// ---------------------------------------------------------------------------
// TabBar
// ---------------------------------------------------------------------------

class TabBar final : public wxControl {
public:
    explicit TabBar(TabBook* owner);

    void AddTab(const wxString& title);
    void RemoveTab(std::size_t index);
    void SetTabTitle(std::size_t index, const wxString& title);
    void SetSelection(int index);
    void SetDarkMode(bool dark);

private:
    struct Hit {
        int index = -1;
        bool close = false;
    };

    wxSize DoGetBestClientSize() const override;

    void Relayout();
    void EnsureTabVisible(int index);
    int MaxScroll() const;
    int Padding() const;
    int CloseGap() const;
    int CloseSize() const;
    Hit HitTest(const wxPoint& point) const;
    void DrawTab(wxGraphicsContext& gc, wxDC& dc, int index, bool selected,
                 const TabPalette& palette);

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMiddleUp(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnDpiChanged(wxDPIChangedEvent& event);
    void OnSysColourChanged(wxSysColourChangedEvent& event);

    TabBook* m_owner = nullptr;
    std::vector<wxString> m_titles;
    std::vector<wxRect> m_rects;
    std::vector<wxRect> m_closeRects;
    int m_selected = -1;
    int m_hover = -1;
    bool m_closeHover = false;
    int m_pressedClose = -1;
    int m_scroll = 0;
    int m_contentWidth = 0;
    bool m_layoutValid = false;
    bool m_dark = false;
};

TabBar::TabBar(TabBook* owner)
    : wxControl(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE,
                wxDefaultValidator, "TabBar"),
      m_owner(owner) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(kBarHeightDip)));

    Bind(wxEVT_PAINT, &TabBar::OnPaint, this);
    Bind(wxEVT_SIZE, &TabBar::OnSize, this);
    Bind(wxEVT_MOTION, &TabBar::OnMouseMove, this);
    Bind(wxEVT_LEAVE_WINDOW, &TabBar::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &TabBar::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &TabBar::OnLeftUp, this);
    Bind(wxEVT_MIDDLE_UP, &TabBar::OnMiddleUp, this);
    Bind(wxEVT_MOUSEWHEEL, &TabBar::OnMouseWheel, this);
    Bind(wxEVT_DPI_CHANGED, &TabBar::OnDpiChanged, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &TabBar::OnSysColourChanged, this);
}

wxSize TabBar::DoGetBestClientSize() const {
    return wxSize(FromDIP(160), FromDIP(kBarHeightDip));
}

int TabBar::Padding() const {
    return FromDIP(kTextPaddingDip);
}

int TabBar::CloseGap() const {
    return FromDIP(kCloseGapDip);
}

int TabBar::CloseSize() const {
    return FromDIP(kCloseDiameterDip);
}

void TabBar::AddTab(const wxString& title) {
    m_titles.push_back(title);
    m_layoutValid = false;
    Refresh();
}

void TabBar::RemoveTab(std::size_t index) {
    if (index >= m_titles.size()) return;

    m_titles.erase(m_titles.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_selected == static_cast<int>(index)) {
        m_selected = -1;
    } else if (m_selected > static_cast<int>(index)) {
        --m_selected;
    }
    m_hover = -1;
    m_closeHover = false;
    m_pressedClose = -1;
    m_layoutValid = false;
    Refresh();
}

void TabBar::SetTabTitle(std::size_t index, const wxString& title) {
    if (index >= m_titles.size() || m_titles[index] == title) return;
    m_titles[index] = title;
    m_layoutValid = false;
    EnsureTabVisible(m_selected);
    Refresh();
}

void TabBar::SetSelection(int index) {
    if (index < 0 || index >= static_cast<int>(m_titles.size())) index = -1;
    if (m_selected == index) {
        EnsureTabVisible(index);
        return;
    }
    m_selected = index;
    EnsureTabVisible(index);
    Refresh();
}

void TabBar::SetDarkMode(bool dark) {
    if (m_dark == dark) return;
    m_dark = dark;
    Refresh();
}


void TabBar::Relayout() {
    const int count = static_cast<int>(m_titles.size());
    m_rects.assign(m_titles.size(), wxRect());
    m_closeRects.assign(m_titles.size(), wxRect());

    const int clientWidth = GetClientSize().x;
    const int clientHeight = GetClientSize().y;
    if (clientWidth <= 0 || clientHeight <= 0) {
        m_layoutValid = true;
        return;
    }

    const int margin = FromDIP(kSideMarginDip);
    const int gap = FromDIP(kTabGapDip);
    const int padding = Padding();
    const int closeGap = CloseGap();
    const int closeSize = CloseSize();
    const int closeRight = FromDIP(kCloseRightDip);
    const int minWidth = FromDIP(kMinTabWidthDip);
    const int maxWidth = FromDIP(kMaxTabWidthDip);
    const int top = FromDIP(kTopMarginDip);
    const int tabHeight = std::max(1, clientHeight - top);

    wxBitmap measureBitmap(1, 1);
    wxMemoryDC measureDc(measureBitmap);
    measureDc.SetFont(GetFont());

    std::vector<int> widths(static_cast<std::size_t>(count));
    int contentWidth = margin * 2;
    for (int i = 0; i < count; ++i) {
        const int textWidth =
            TabTextWidth(m_titles[static_cast<std::size_t>(i)], measureDc);
        int width = padding * 2 + textWidth + closeGap + closeSize;
        width = std::clamp(width, minWidth, maxWidth);
        widths[static_cast<std::size_t>(i)] = width;
        if (i > 0) contentWidth += gap;
        contentWidth += width;
    }

    m_contentWidth = contentWidth;
    m_scroll = std::clamp(m_scroll, 0, MaxScroll());

    int x = margin - m_scroll;
    for (int i = 0; i < count; ++i) {
        const int width = widths[static_cast<std::size_t>(i)];
        const wxRect tab(x, top, width, tabHeight);
        const wxRect close(x + width - closeRight - closeSize,
                           top + (tabHeight - closeSize) / 2, closeSize, closeSize);
        m_rects[static_cast<std::size_t>(i)] = tab;
        m_closeRects[static_cast<std::size_t>(i)] = close;
        x += width + gap;
    }
    m_layoutValid = true;
}

void TabBar::EnsureTabVisible(int index) {
    if (!m_layoutValid) Relayout();
    if (index < 0 || index >= static_cast<int>(m_rects.size())) return;

    const int width = GetClientSize().x;
    const int margin = FromDIP(kSideMarginDip);
    if (width <= 0) return;

    const wxRect tab = m_rects[static_cast<std::size_t>(index)];
    int delta = 0;
    if (tab.x < margin) {
        delta = tab.x - margin;
    } else if (tab.GetRight() > width - margin) {
        delta = tab.GetRight() - (width - margin);
    }
    if (delta == 0) return;

    m_scroll = std::clamp(m_scroll + delta, 0, MaxScroll());
    m_layoutValid = false;
    Relayout();
    Refresh();
}

int TabBar::MaxScroll() const {
    return std::max(0, m_contentWidth - GetClientSize().x);
}

TabBar::Hit TabBar::HitTest(const wxPoint& point) const {
    if (!m_layoutValid) {
        const_cast<TabBar*>(this)->Relayout();
    }
    for (std::size_t i = 0; i < m_rects.size(); ++i) {
        if (!m_rects[i].Contains(point)) continue;
        const bool close = i < m_closeRects.size() && m_closeRects[i].Contains(point);
        return {static_cast<int>(i), close};
    }
    return {};
}


void TabBar::DrawTab(wxGraphicsContext& gc, wxDC& dc, int index, bool selected,
                     const TabPalette& palette) {
    const auto tabIndex = static_cast<std::size_t>(index);
    const wxRect& tab = m_rects[tabIndex];
    const wxRect& close = m_closeRects[tabIndex];
    const double radius = FromDIP(kCornerRadiusDip);

    if (selected) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(palette.activeBg));
        gc.DrawRoundedRectangle(tab.x, tab.y, tab.width, tab.height, radius);
        // Square off the bottom so the selected tab joins the editor area and
        // hides the strip's separator line.
        gc.DrawRectangle(tab.x, tab.y + radius, tab.width, tab.height - radius);
        gc.SetBrush(wxBrush(palette.accent));
        gc.DrawRectangle(tab.x + radius, tab.y, tab.width - radius * 2, FromDIP(2));
    } else if (m_hover == index && !m_closeHover) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(palette.hoverBg));
        gc.DrawRoundedRectangle(tab.x, tab.y + FromDIP(2), tab.width,
                                tab.height - FromDIP(2), radius);
    }

    const int textWidth = tab.width - Padding() * 2 - CloseGap() - CloseSize();
    wxString title = wxControl::Ellipsize(m_titles[tabIndex], dc, wxELLIPSIZE_MIDDLE,
                                          std::max(1, textWidth),
                                          wxELLIPSIZE_FLAGS_NONE);
    dc.SetFont(GetFont());
    const wxSize extent = dc.GetTextExtent(title);
    gc.SetFont(GetFont(), selected ? palette.text : palette.inactiveText);
    gc.DrawText(title, tab.x + Padding(),
                tab.y + (tab.height - extent.y) / 2.0);

    const bool hot = m_hover == index && m_closeHover;
    if (hot) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(palette.closeHoverBg));
        gc.DrawEllipse(close.x, close.y, close.width, close.height);
    }

    const wxColour closeColour =
        hot ? palette.closeHoverFg : (selected ? palette.close : palette.inactiveClose);
    gc.SetPen(wxPen(closeColour, std::max(1, FromDIP(1))));
    const double centerX = close.x + close.width / 2.0;
    const double centerY = close.y + close.height / 2.0;
    const double arm = FromDIP(3);
    gc.StrokeLine(centerX - arm, centerY - arm, centerX + arm, centerY + arm);
    gc.StrokeLine(centerX - arm, centerY + arm, centerX + arm, centerY - arm);
}

void TabBar::OnPaint(wxPaintEvent& event) {
    (void)event;
    wxAutoBufferedPaintDC dc(this);
    if (!m_layoutValid) Relayout();

    const TabPalette palette = MakeTabPalette(m_dark);
    dc.SetBackground(wxBrush(palette.barBg));
    dc.Clear();
    dc.SetFont(GetFont());

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    for (int i = 0; i < static_cast<int>(m_rects.size()); ++i) {
        if (i == m_selected) continue;
        DrawTab(*gc, dc, i, false, palette);
    }

    const wxSize size = GetClientSize();
    if (size.y > 0) {
        gc->SetPen(wxPen(palette.border, std::max(1, FromDIP(1))));
        gc->StrokeLine(0, size.y - 0.5, size.x, size.y - 0.5);
    }

    if (m_selected >= 0 && m_selected < static_cast<int>(m_rects.size())) {
        DrawTab(*gc, dc, m_selected, true, palette);
    }
}


void TabBar::OnSize(wxSizeEvent& event) {
    m_layoutValid = false;
    Relayout();
    EnsureTabVisible(m_selected);
    Refresh();
    event.Skip();
}

void TabBar::OnMouseMove(wxMouseEvent& event) {
    const Hit hit = HitTest(event.GetPosition());
    const bool closeHover = hit.index >= 0 && hit.close;
    if (hit.index != m_hover || closeHover != m_closeHover) {
        m_hover = hit.index;
        m_closeHover = closeHover;
        Refresh();
    }
    SetCursor(wxCursor(closeHover ? wxCURSOR_HAND : wxCURSOR_ARROW));
    event.Skip();
}

void TabBar::OnMouseLeave(wxMouseEvent& event) {
    if (m_hover != -1 || m_closeHover) {
        m_hover = -1;
        m_closeHover = false;
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
    event.Skip();
}

void TabBar::OnLeftDown(wxMouseEvent& event) {
    const Hit hit = HitTest(event.GetPosition());
    m_pressedClose = hit.index >= 0 && hit.close ? hit.index : -1;
    if (hit.index >= 0 && !hit.close) {
        m_owner->SelectFromUser(hit.index);
    }
    Refresh();
    event.Skip();
}

void TabBar::OnLeftUp(wxMouseEvent& event) {
    const Hit hit = HitTest(event.GetPosition());
    const int pressed = m_pressedClose;
    m_pressedClose = -1;

    if (pressed >= 0 && hit.index == pressed && hit.close) {
        m_owner->RequestClose(pressed);
    } else if (hit.index >= 0 && !hit.close && event.ControlDown()) {
        m_owner->RequestClose(hit.index);
    }
    Refresh();
    event.Skip();
}

void TabBar::OnMiddleUp(wxMouseEvent& event) {
    const Hit hit = HitTest(event.GetPosition());
    if (hit.index >= 0) m_owner->RequestClose(hit.index);
    event.Skip();
}

void TabBar::OnMouseWheel(wxMouseEvent& event) {
    if (!m_layoutValid) Relayout();
    const int maxScroll = MaxScroll();
    if (maxScroll <= 0) {
        event.Skip();
        return;
    }

    int steps = event.GetWheelRotation();
    const int delta = event.GetWheelDelta();
    if (delta != 0) {
        steps /= delta;
    }
    if (steps == 0) steps = event.GetWheelRotation() > 0 ? 1 : -1;

    m_scroll = std::clamp(m_scroll - steps * FromDIP(60), 0, maxScroll);
    m_layoutValid = false;
    Refresh();
}

void TabBar::OnDpiChanged(wxDPIChangedEvent& event) {
    SetMinSize(wxSize(-1, FromDIP(kBarHeightDip)));
    m_layoutValid = false;
    if (wxWindow* parent = GetParent()) parent->Layout();
    Refresh();
    event.Skip();
}

void TabBar::OnSysColourChanged(wxSysColourChangedEvent& event) {
    Refresh();
    event.Skip();
}


// ---------------------------------------------------------------------------
// TabBook
// ---------------------------------------------------------------------------

TabBook::TabBook(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id) {
    m_tabs = new TabBar(this);
    m_book = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxBORDER_NONE);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_tabs, 0, wxEXPAND);
    sizer->Add(m_book, 1, wxEXPAND);
    SetSizer(sizer);
}

void TabBook::AddPage(wxWindow* page, const wxString& title, bool select) {
    if (page == nullptr) return;
    if (page->GetParent() != m_book) page->Reparent(m_book);

    const int index = static_cast<int>(m_book->GetPageCount());
    if (!m_book->AddPage(page, title, false)) return;
    m_tabs->AddTab(title);

    if (select || m_selection < 0) {
        const int oldSelection = m_selection;
        m_selection = index;
        m_tabs->SetSelection(index);
        m_book->ChangeSelection(static_cast<std::size_t>(index));
        if (select) NotifyPageChanged(oldSelection, index);
    }
}

void TabBook::DeletePage(std::size_t index) {
    const std::size_t count = m_book->GetPageCount();
    if (index >= count) return;

    const int oldSelection = m_selection;
    const int removed = static_cast<int>(index);
    const bool wasSelected = oldSelection == removed;
    const bool beforeSelection = removed < oldSelection;

    m_tabs->RemoveTab(index);
    m_book->DeletePage(index);

    const int newCount = static_cast<int>(m_book->GetPageCount());
    int next = oldSelection;
    if (newCount == 0) {
        next = -1;
    } else if (wasSelected) {
        next = std::min(removed, newCount - 1);
    } else if (beforeSelection) {
        --next;
    }
    if (next < 0 && newCount > 0) next = 0;

    m_selection = next;
    if (next >= 0) {
        m_tabs->SetSelection(next);
        m_book->ChangeSelection(static_cast<std::size_t>(next));
    }
}

std::size_t TabBook::GetPageCount() const {
    return m_book->GetPageCount();
}

wxWindow* TabBook::GetPage(std::size_t index) const {
    if (index >= m_book->GetPageCount()) return nullptr;
    return m_book->GetPage(index);
}

wxWindow* TabBook::GetCurrentPage() const {
    return m_book->GetCurrentPage();
}

wxWindow* TabBook::PageParent() const {
    return m_book;
}

int TabBook::GetSelection() const {
    return m_selection;
}

int TabBook::SetSelection(std::size_t index) {
    const int oldSelection = m_selection;
    if (index >= m_book->GetPageCount()) return oldSelection;

    m_selection = static_cast<int>(index);
    m_tabs->SetSelection(m_selection);
    m_book->ChangeSelection(index);
    if (oldSelection != m_selection) NotifyPageChanged(oldSelection, m_selection);
    return oldSelection;
}

int TabBook::ChangeSelection(std::size_t index) {
    const int oldSelection = m_selection;
    if (index >= m_book->GetPageCount()) return oldSelection;

    m_selection = static_cast<int>(index);
    m_tabs->SetSelection(m_selection);
    m_book->ChangeSelection(index);
    return oldSelection;
}

bool TabBook::SetPageText(std::size_t index, const wxString& title) {
    if (index >= m_book->GetPageCount()) return false;
    m_book->SetPageText(index, title);
    m_tabs->SetTabTitle(index, title);
    return true;
}

wxString TabBook::GetPageText(std::size_t index) const {
    if (index >= m_book->GetPageCount()) return wxString();
    return m_book->GetPageText(index);
}

void TabBook::SetDarkMode(bool dark) {
    m_tabs->SetDarkMode(dark);
}

void TabBook::SelectFromUser(int index) {
    if (index < 0 || index >= static_cast<int>(m_book->GetPageCount())) return;

    if (index != m_selection) {
        const int oldSelection = m_selection;
        m_selection = index;
        m_tabs->SetSelection(index);
        m_book->ChangeSelection(static_cast<std::size_t>(index));
        NotifyPageChanged(oldSelection, index);
    }
    if (wxWindow* page = m_book->GetPage(static_cast<std::size_t>(index))) {
        page->SetFocus();
    }
}

void TabBook::RequestClose(int index) {
    if (index < 0 || index >= static_cast<int>(m_book->GetPageCount())) return;

    wxBookCtrlEvent event(EVT_TAB_CLOSE, GetId(), index, m_selection);
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

void TabBook::NotifyPageChanged(int oldSelection, int newSelection) {
    wxBookCtrlEvent event(EVT_TAB_CHANGED, GetId(), newSelection, oldSelection);
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

}  // namespace txt

