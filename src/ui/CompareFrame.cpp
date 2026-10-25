#include "ui/CompareFrame.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/stc/stc.h>

#include "core/Diff.h"
#include "editor/EditorWidget.h"
#include "ui/DiffPane.h"

wxDEFINE_EVENT(EVT_COMPARE_REFRESH, wxCommandEvent);

namespace txt {
namespace {

enum {
    ID_COMPARE_REFRESH = wxID_HIGHEST + 100,
    ID_COMPARE_SYNC_DIFF,
    ID_COMPARE_SYNC_LINES,
};

constexpr int kMarkerRankSame = 0;
constexpr int kMarkerRankChanged = 1;
constexpr int kMarkerRankModified = 2;

int KindRank(diff::LineKind kind) {
    switch (kind) {
        case diff::LineKind::Modified:
            return kMarkerRankModified;
        case diff::LineKind::Added:
        case diff::LineKind::Removed:
            return kMarkerRankChanged;
        case diff::LineKind::Same:
            return kMarkerRankSame;
    }
    return kMarkerRankSame;
}

wxString DisplayTitle(const CompareDocument& document) {
    if (!document.title.empty()) return wxString::FromUTF8(document.title);
    if (!document.path.empty()) return wxString::FromUTF8(document.path);
    return "Untitled";
}

}  // namespace

CompareFrame::CompareFrame(wxWindow* parent, std::vector<CompareDocument> documents,
                           bool dark)
    : wxFrame(parent, wxID_ANY, "Compare Tabs", wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER),
      m_documents(std::move(documents)),
      m_dark(dark) {
    if (!m_documents.empty()) m_zoom = m_documents.front().zoom;

    SetMinSize(FromDIP(wxSize(760, 480)));
    SetSize(FromDIP(wxSize(1200, 820)));

    BuildUi();
    Bind(wxEVT_BUTTON, &CompareFrame::OnRefreshButton, this, ID_COMPARE_REFRESH);
    Bind(wxEVT_BUTTON, &CompareFrame::OnCloseButton, this, wxID_CLOSE);
    Bind(wxEVT_RADIOBUTTON, &CompareFrame::OnSyncModeChanged, this, ID_COMPARE_SYNC_DIFF);
    Bind(wxEVT_RADIOBUTTON, &CompareFrame::OnSyncModeChanged, this, ID_COMPARE_SYNC_LINES);
    Bind(wxEVT_CHAR_HOOK, &CompareFrame::OnCharHook, this);

    RebuildPanes(m_documents.empty() ? 0 : m_documents.front().firstVisibleLine);
}

void CompareFrame::BuildUi() {
    m_toolbar = new wxPanel(this, wxID_ANY);

    wxButton* refresh = new wxButton(m_toolbar, ID_COMPARE_REFRESH, "Refresh");
    refresh->SetToolTip("Re-read all compared tabs (F5)");

    m_syncLabel = new wxStaticText(m_toolbar, wxID_ANY, "Scroll sync:");
    m_syncDiffButton =
        new wxRadioButton(m_toolbar, ID_COMPARE_SYNC_DIFF, "Diff anchors",
                          wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    m_syncLineButton =
        new wxRadioButton(m_toolbar, ID_COMPARE_SYNC_LINES, "Line numbers");
    m_syncDiffButton->SetValue(true);
    const wxString syncTip =
        "Diff anchors follows inserted/removed lines; line numbers keeps identical "
        "line indices.";
    m_syncDiffButton->SetToolTip(syncTip);
    m_syncLineButton->SetToolTip(syncTip);

    wxButton* close = new wxButton(m_toolbar, wxID_CLOSE, "Close");

    wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
    const int gap = FromDIP(6);
    toolbarSizer->Add(refresh, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap);
    toolbarSizer->Add(m_syncLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
    toolbarSizer->Add(m_syncDiffButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
    toolbarSizer->Add(m_syncLineButton, 0,
                      wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, gap);
    toolbarSizer->AddStretchSpacer(1);
    toolbarSizer->Add(close, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap);
    m_toolbar->SetSizer(toolbarSizer);

    m_content = new wxPanel(this, wxID_ANY);
    m_contentSizer = new wxBoxSizer(wxVERTICAL);
    m_content->SetSizer(m_contentSizer);

    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_toolbar, 0, wxEXPAND);
    frameSizer->Add(m_content, 1, wxEXPAND);
    SetSizer(frameSizer);

    CreateStatusBar(1);
    ApplyChrome();
    Layout();
}

void CompareFrame::ApplyChrome() {
    if (m_toolbar == nullptr || m_content == nullptr) return;
    const wxColour toolbarColour =
        m_dark ? wxColour(34, 37, 43) : wxColour(246, 247, 249);
    const wxColour textColour =
        m_dark ? wxColour(226, 228, 232) : wxColour(32, 35, 40);
    const wxColour contentColour =
        m_dark ? wxColour(24, 26, 30) : wxColour(255, 255, 255);

    m_toolbar->SetBackgroundColour(toolbarColour);
    m_syncLabel->SetForegroundColour(textColour);
    m_syncDiffButton->SetForegroundColour(textColour);
    m_syncLineButton->SetForegroundColour(textColour);
    m_content->SetBackgroundColour(contentColour);
    m_toolbar->Refresh();
    m_content->Refresh();
}

std::vector<std::string> CompareFrame::DocumentIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_documents.size());
    for (const CompareDocument& document : m_documents) ids.push_back(document.id);
    return ids;
}

std::string CompareFrame::BaseId() const {
    return m_documents.empty() ? std::string() : m_documents.front().id;
}

void CompareFrame::SetDarkMode(bool dark) {
    m_dark = dark;
    ApplyChrome();
    for (DiffPane* pane : m_panes) pane->SetDarkMode(dark);
    Refresh();
}

void CompareFrame::MarkStale() {
    if (m_stale) return;
    m_stale = true;
    UpdateStatus();
}

bool CompareFrame::SetDocuments(std::vector<CompareDocument> documents) {
    if (documents.size() < 2) return false;

    int initialTop = documents.front().firstVisibleLine;
    const bool sameBase = m_basePane != nullptr && !m_documents.empty() &&
                          documents.front().id == m_documents.front().id;
    if (sameBase) {
        const int currentTop = m_basePane->Editor()->FirstVisibleLine();
        if (currentTop >= 0) initialTop = currentTop;
    }

    m_documents = std::move(documents);
    m_stale = false;
    m_limited = false;
    RebuildPanes(initialTop);
    return true;
}

void CompareFrame::RebuildPanes(int initialTop) {
    m_layoutBusy = true;
    m_syncing = true;
    m_contentSizer->Clear(true);
    m_panes.clear();
    m_basePane = nullptr;
    m_expectedTop.assign(m_documents.size(), -1);

    if (m_documents.empty()) {
        m_layoutBusy = false;
        m_syncing = false;
        UpdateStatus();
        return;
    }

    const int gap = FromDIP(2);
    if (m_documents.size() == 2) {
        wxSplitterWindow* splitter =
            new wxSplitterWindow(m_content, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
        splitter->SetMinimumPaneSize(FromDIP(140));
        splitter->SetSashGravity(0.5);

        DiffPane* base = new DiffPane(splitter, m_documents[0], true, m_dark);
        DiffPane* other = new DiffPane(splitter, m_documents[1], false, m_dark);
        splitter->SplitVertically(base, other, 0);
        m_panes.push_back(base);
        m_panes.push_back(other);
        m_basePane = base;
        m_contentSizer->Add(splitter, 1, wxEXPAND);
    } else {
        wxPanel* host = new wxPanel(m_content, wxID_ANY);
        host->SetBackgroundColour(m_dark ? wxColour(24, 26, 30) : wxColour(255, 255, 255));

        const int count = static_cast<int>(m_documents.size());
        const int columns = count <= 3 ? count : (count == 4 ? 2 : 3);
        const int rows = (count + columns - 1) / columns;
        wxFlexGridSizer* grid = new wxFlexGridSizer(columns, gap, gap);
        for (int column = 0; column < columns; ++column) grid->AddGrowableCol(column, 1);
        for (int row = 0; row < rows; ++row) grid->AddGrowableRow(row, 1);

        for (int index = 0; index < count; ++index) {
            DiffPane* pane = new DiffPane(host, m_documents[static_cast<std::size_t>(index)],
                                          index == 0, m_dark);
            if (index == 0) m_basePane = pane;
            m_panes.push_back(pane);
            grid->Add(pane, 1, wxEXPAND);
        }
        host->SetSizer(grid);
        m_contentSizer->Add(host, 1, wxEXPAND);
    }

    for (std::size_t index = 0; index < m_panes.size(); ++index) {
        DiffPane* pane = m_panes[index];
        pane->Editor()->SetZoom(m_zoom);
        pane->Editor()->Bind(wxEVT_STC_UPDATEUI,
                             [this, pane](wxStyledTextEvent& event) {
                                 OnPaneUpdate(pane, event);
                             });
        m_expectedTop[index] = -1;
    }

    BuildDiff();
    m_content->Layout();
    Layout();
    ApplyInitialTop(initialTop);
    m_layoutBusy = false;
    m_syncing = false;
    UpdateStatus();
}

void CompareFrame::BuildDiff() {
    if (m_panes.size() != m_documents.size() || m_panes.empty()) return;

    const std::vector<std::string_view> baseLines =
        diff::SplitLines(m_documents.front().text);
    std::vector<diff::LineKind> baseKinds(baseLines.size(), diff::LineKind::Same);
    bool limited = false;

    for (std::size_t index = 1; index < m_documents.size(); ++index) {
        const std::vector<std::string_view> paneLines =
            diff::SplitLines(m_documents[index].text);
        diff::Alignment alignment = diff::AlignToBase(baseLines, paneLines);
        if (alignment.limited) limited = true;

        if (alignment.baseKinds.size() == baseKinds.size()) {
            for (std::size_t line = 0; line < baseKinds.size(); ++line) {
                if (KindRank(alignment.baseKinds[line]) > KindRank(baseKinds[line])) {
                    baseKinds[line] = alignment.baseKinds[line];
                }
            }
        }
        m_panes[index]->SetAlignment(std::move(alignment));
    }

    diff::Stats baseStats;
    for (diff::LineKind kind : baseKinds) {
        if (kind == diff::LineKind::Removed) {
            ++baseStats.removed;
        } else if (kind == diff::LineKind::Modified) {
            ++baseStats.modified;
        }
    }
    m_panes.front()->SetDiff(std::move(baseKinds), baseStats, limited);
    m_limited = limited;
}

void CompareFrame::ApplyInitialTop(int top) {
    if (m_panes.empty()) return;
    const int baseLine = std::max(top, 0);
    m_syncing = true;
    for (std::size_t index = 0; index < m_panes.size(); ++index) {
        int line = baseLine;
        if (m_syncMode == SyncMode::DiffAnchors && index > 0) {
            line = MapBaseToPane(m_panes[index], baseLine);
        }
        EditorWidget* editor = m_panes[index]->Editor();
        editor->SetFirstVisibleLine(line);
        m_expectedTop[index] = editor->FirstVisibleLine();
    }
    m_syncing = false;
}

void CompareFrame::SyncFromPane(DiffPane* source, int sourceTop) {
    const int sourceIndex = PaneIndex(source);
    if (sourceIndex < 0) return;

    int baseLine = sourceTop;
    if (m_syncMode == SyncMode::DiffAnchors && source != m_basePane) {
        baseLine = MapPaneToBase(source, sourceTop);
    }

    m_syncing = true;
    for (std::size_t index = 0; index < m_panes.size(); ++index) {
        if (static_cast<int>(index) == sourceIndex) continue;

        int line = sourceTop;
        if (m_syncMode == SyncMode::DiffAnchors) {
            line = m_panes[index] == m_basePane
                       ? baseLine
                       : MapBaseToPane(m_panes[index], baseLine);
        }
        EditorWidget* editor = m_panes[index]->Editor();
        editor->SetFirstVisibleLine(line);
        m_expectedTop[index] = editor->FirstVisibleLine();
    }
    m_expectedTop[static_cast<std::size_t>(sourceIndex)] = -1;
    m_syncing = false;
}

int CompareFrame::PaneIndex(const DiffPane* pane) const {
    for (std::size_t index = 0; index < m_panes.size(); ++index) {
        if (m_panes[index] == pane) return static_cast<int>(index);
    }
    return -1;
}

int CompareFrame::MapBaseToPane(const DiffPane* pane, int baseLine) const {
    const std::vector<int>& mapping = pane->Alignment().baseToPane;
    int line = 0;
    if (!mapping.empty()) {
        const int index =
            std::clamp(baseLine, 0, static_cast<int>(mapping.size()) - 1);
        line = mapping[static_cast<std::size_t>(index)];
    }
    const int lineCount = pane->Editor()->GetLineCount();
    return std::clamp(line, 0, std::max(0, lineCount - 1));
}

int CompareFrame::MapPaneToBase(const DiffPane* pane, int paneLine) const {
    const std::vector<int>& mapping = pane->Alignment().paneToBase;
    int line = 0;
    if (!mapping.empty()) {
        const int index =
            std::clamp(paneLine, 0, static_cast<int>(mapping.size()) - 1);
        line = mapping[static_cast<std::size_t>(index)];
    }
    const int lineCount = m_basePane == nullptr ? 1 : m_basePane->Editor()->GetLineCount();
    return std::clamp(line, 0, std::max(0, lineCount - 1));
}

void CompareFrame::OnPaneUpdate(DiffPane* pane, wxStyledTextEvent& event) {
    event.Skip();
    if (m_layoutBusy || m_syncing) return;
    if ((event.GetUpdated() & wxSTC_UPDATE_V_SCROLL) == 0) return;

    const int index = PaneIndex(pane);
    if (index < 0) return;
    const int top = pane->Editor()->FirstVisibleLine();
    const int expected = m_expectedTop[static_cast<std::size_t>(index)];
    m_expectedTop[static_cast<std::size_t>(index)] = -1;
    if (expected == top) return;

    SyncFromPane(pane, top);
}

void CompareFrame::OnRefreshButton(wxCommandEvent& event) {
    (void)event;
    RequestRefresh();
}

void CompareFrame::OnCloseButton(wxCommandEvent& event) {
    (void)event;
    Close();
}

void CompareFrame::OnSyncModeChanged(wxCommandEvent& event) {
    (void)event;
    m_syncMode = m_syncLineButton->GetValue() ? SyncMode::LineNumbers
                                              : SyncMode::DiffAnchors;
    if (m_basePane != nullptr) {
        SyncFromPane(m_basePane, m_basePane->Editor()->FirstVisibleLine());
    }
    UpdateStatus();
}

void CompareFrame::OnCharHook(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_F5) {
        RequestRefresh();
        return;
    }
    if (event.ControlDown() && event.GetKeyCode() == 'W') {
        Close();
        return;
    }
    event.Skip();
}

void CompareFrame::RequestRefresh() {
    wxCommandEvent event(EVT_COMPARE_REFRESH);
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

void CompareFrame::UpdateStatus() {
    wxString status;
    if (m_stale) {
        status = "Sources changed - press Refresh (F5) to rebuild the comparison";
    } else {
        status = wxString::Format("%d tabs", static_cast<int>(m_documents.size()));
        status += m_syncMode == SyncMode::DiffAnchors
                      ? " - scroll synchronized by diff anchors"
                      : " - scroll synchronized by line numbers";
        if (m_limited) status += " - diff limited for a very large edit";
    }
    SetStatusText(status);

    wxString title = "Compare Tabs";
    if (!m_documents.empty()) title += " - " + DisplayTitle(m_documents.front());
    SetTitle(title);
}

}  // namespace txt

