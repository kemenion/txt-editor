#pragma once

#include <string>
#include <vector>

#include <wx/panel.h>

#include "core/Diff.h"
#include "ui/CompareTypes.h"

class wxStaticText;

namespace txt {

class EditorWidget;

// Read-only Scintilla pane used by CompareFrame.  It keeps the original text
// (so line numbers, search and copy behave normally) and paints line-level
// diff markers using background markers in a hidden symbol margin.
class DiffPane final : public wxPanel {
public:
    DiffPane(wxWindow* parent, const CompareDocument& document, bool base, bool dark);

    EditorWidget* Editor() const { return m_editor; }
    const CompareDocument& Document() const { return m_document; }
    const diff::Alignment& Alignment() const { return m_alignment; }
    const std::string& Id() const { return m_document.id; }

    void SetAlignment(diff::Alignment alignment);
    void SetDiff(std::vector<diff::LineKind> kinds, diff::Stats stats, bool limited);
    void SetDarkMode(bool dark);

private:
    void BuildHeader();
    void ApplyTheme();
    void ApplyMarkers();
    void UpdateStats();

    CompareDocument m_document;
    EditorWidget* m_editor = nullptr;
    wxPanel* m_header = nullptr;
    wxStaticText* m_title = nullptr;
    wxStaticText* m_meta = nullptr;
    wxStaticText* m_stats = nullptr;
    diff::Alignment m_alignment;
    std::vector<diff::LineKind> m_kinds;
    diff::Stats m_diffStats;
    bool m_base = false;
    bool m_dark = false;
    bool m_limited = false;
};

}  // namespace txt
