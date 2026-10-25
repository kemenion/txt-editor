#pragma once

#include <string>
#include <vector>

#include <wx/event.h>
#include <wx/frame.h>

#include "ui/CompareTypes.h"

class wxBoxSizer;
class wxCommandEvent;
class wxKeyEvent;
class wxPanel;
class wxRadioButton;
class wxStaticText;
class wxStyledTextEvent;

wxDECLARE_EVENT(EVT_COMPARE_REFRESH, wxCommandEvent);

namespace txt {

class DiffPane;

// A separate top-level window showing two or more tab snapshots side by side.
// The first snapshot is the base; all other snapshots are compared against it.
class CompareFrame final : public wxFrame {
public:
    CompareFrame(wxWindow* parent, std::vector<CompareDocument> documents, bool dark);

    // Replaces all snapshots.  The current base id is preserved when possible.
    bool SetDocuments(std::vector<CompareDocument> documents);
    void SetDarkMode(bool dark);

    // Marks the read-only snapshots as out of date.  The comparison is only
    // rebuilt when the user asks for Refresh.
    void MarkStale();
    bool IsStale() const { return m_stale; }

    std::vector<std::string> DocumentIds() const;
    std::string BaseId() const;

private:
    enum class SyncMode {
        DiffAnchors,
        LineNumbers,
    };

    void BuildUi();
    void ApplyChrome();
    void RebuildPanes(int initialTop);
    void BuildDiff();
    void ApplyInitialTop(int top);
    void SyncFromPane(DiffPane* source, int sourceTop);
    void UpdateStatus();

    void OnRefreshButton(wxCommandEvent& event);
    void OnCloseButton(wxCommandEvent& event);
    void OnSyncModeChanged(wxCommandEvent& event);
    void OnCharHook(wxKeyEvent& event);
    void OnPaneUpdate(DiffPane* pane, wxStyledTextEvent& event);
    void RequestRefresh();

    int PaneIndex(const DiffPane* pane) const;
    int MapBaseToPane(const DiffPane* pane, int baseLine) const;
    int MapPaneToBase(const DiffPane* pane, int paneLine) const;

    std::vector<CompareDocument> m_documents;
    std::vector<DiffPane*> m_panes;
    DiffPane* m_basePane = nullptr;

    wxPanel* m_toolbar = nullptr;
    wxPanel* m_content = nullptr;
    wxBoxSizer* m_contentSizer = nullptr;
    wxRadioButton* m_syncDiffButton = nullptr;
    wxRadioButton* m_syncLineButton = nullptr;
    wxStaticText* m_syncLabel = nullptr;

    std::vector<int> m_expectedTop;
    int m_zoom = 0;
    SyncMode m_syncMode = SyncMode::DiffAnchors;
    bool m_dark = false;
    bool m_layoutBusy = false;
    bool m_syncing = false;
    bool m_stale = false;
    bool m_limited = false;
};

}  // namespace txt
