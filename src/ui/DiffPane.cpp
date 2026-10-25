#include "ui/DiffPane.h"

#include <algorithm>
#include <string>
#include <utility>

#include <wx/font.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>

#include "core/Language.h"
#include "editor/EditorWidget.h"

namespace txt {
namespace {

constexpr int kMarkerAdded = 10;
constexpr int kMarkerRemoved = 11;
constexpr int kMarkerModified = 12;
constexpr int kMarkerMask =
    (1 << kMarkerAdded) | (1 << kMarkerRemoved) | (1 << kMarkerModified);

wxString Utf8(const std::string& value) {
    return wxString::FromUTF8(value);
}

std::string DisplayTitle(const CompareDocument& document) {
    if (!document.title.empty()) return document.title;
    if (!document.path.empty()) return document.path;
    return "Untitled";
}

struct PanePalette {
    wxColour background;
    wxColour header;
    wxColour text;
    wxColour muted;
    wxColour added;
    wxColour removed;
    wxColour modified;
};

PanePalette PaletteFor(bool dark) {
    if (dark) {
        return {wxColour(24, 26, 30), wxColour(34, 37, 43), wxColour(226, 228, 232),
                wxColour(158, 164, 174), wxColour(42, 78, 54), wxColour(92, 42, 50),
                wxColour(92, 78, 36)};
    }
    return {wxColour(255, 255, 255), wxColour(246, 247, 249), wxColour(32, 35, 40),
            wxColour(100, 106, 116), wxColour(198, 239, 206), wxColour(255, 199, 206),
            wxColour(255, 235, 156)};
}

}  // namespace

DiffPane::DiffPane(wxWindow* parent, const CompareDocument& document, bool base, bool dark)
    : wxPanel(parent, wxID_ANY), m_document(document), m_base(base), m_dark(dark) {
    BuildHeader();
    m_editor = new EditorWidget(this);
    m_editor->SetPath(m_document.path);
    m_editor->LoadText(m_document.text, m_document.meta);
    m_editor->SetReadOnly(true);
    m_editor->SetUseHorizontalScrollBar(true);
    m_editor->SetWordWrap(false);
    m_editor->SetWrapMode(wxSTC_WRAP_NONE);
    m_editor->SetCaretLineVisible(false);
    m_editor->SetMarginWidth(1, 0);
    m_editor->SetMarginType(2, wxSTC_MARGIN_SYMBOL);
    m_editor->SetMarginMask(2, kMarkerMask);
    m_editor->SetMarginWidth(2, 0);
    m_editor->SetMarginSensitive(2, false);
    for (int marker : {kMarkerAdded, kMarkerRemoved, kMarkerModified}) {
        m_editor->MarkerDefine(marker, wxSTC_MARK_BACKGROUND);
        m_editor->MarkerSetAlpha(marker, wxSTC_ALPHA_NOALPHA);
    }
    m_editor->SetZoom(std::clamp(m_document.zoom, -10, 40));

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_header, 0, wxEXPAND);
    sizer->Add(m_editor, 1, wxEXPAND);
    SetSizer(sizer);

    ApplyTheme();
    UpdateStats();
}

void DiffPane::BuildHeader() {
    const int padding = FromDIP(6);
    m_header = new wxPanel(this, wxID_ANY);
    m_header->SetMinSize(wxSize(-1, FromDIP(60)));

    wxString title = Utf8(DisplayTitle(m_document));
    if (m_base) title = "Base: " + title;
    if (m_document.modified) title += " *";
    m_title = new wxStaticText(m_header, wxID_ANY, title, wxDefaultPosition,
                               wxDefaultSize, wxST_ELLIPSIZE_END);
    wxFont titleFont = m_title->GetFont();
    titleFont.MakeBold();
    m_title->SetFont(titleFont);

    const std::string meta = std::string(EncodingName(m_document.meta.encoding)) + "  " +
                             std::string(EolName(m_document.meta.eol)) + "  " +
                             std::string(LanguageName(m_document.meta.language));
    m_meta = new wxStaticText(m_header, wxID_ANY, Utf8(meta), wxDefaultPosition,
                              wxDefaultSize, wxST_ELLIPSIZE_END);

    m_stats = new wxStaticText(m_header, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxALIGN_RIGHT);

    wxBoxSizer* labels = new wxBoxSizer(wxVERTICAL);
    labels->Add(m_title, 0, wxEXPAND);
    labels->Add(m_meta, 0, wxEXPAND);

    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    headerSizer->Add(labels, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, padding);
    headerSizer->Add(m_stats, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, padding);
    m_header->SetSizer(headerSizer);

    m_header->SetToolTip(Utf8(m_document.path.empty() ? "Unsaved tab" : m_document.path));
}

void DiffPane::SetAlignment(diff::Alignment alignment) {
    m_alignment = std::move(alignment);
    SetDiff(m_alignment.paneKinds, m_alignment.stats, m_alignment.limited);
}

void DiffPane::SetDiff(std::vector<diff::LineKind> kinds, diff::Stats stats, bool limited) {
    m_kinds = std::move(kinds);
    m_diffStats = stats;
    m_limited = limited;
    UpdateStats();
    ApplyMarkers();
}

void DiffPane::SetDarkMode(bool dark) {
    m_dark = dark;
    ApplyTheme();
}

void DiffPane::ApplyTheme() {
    const PanePalette palette = PaletteFor(m_dark);

    SetBackgroundColour(palette.background);
    m_header->SetBackgroundColour(palette.header);
    m_title->SetForegroundColour(palette.text);
    m_meta->SetForegroundColour(palette.muted);
    m_stats->SetForegroundColour(palette.text);

    m_editor->SetDarkMode(m_dark);
    m_editor->SetCaretLineVisible(false);
    m_editor->SetReadOnly(true);

    m_editor->MarkerSetBackground(kMarkerAdded, palette.added);
    m_editor->MarkerSetBackground(kMarkerRemoved, palette.removed);
    m_editor->MarkerSetBackground(kMarkerModified, palette.modified);
    m_editor->MarkerSetAlpha(kMarkerAdded, wxSTC_ALPHA_NOALPHA);
    m_editor->MarkerSetAlpha(kMarkerRemoved, wxSTC_ALPHA_NOALPHA);
    m_editor->MarkerSetAlpha(kMarkerModified, wxSTC_ALPHA_NOALPHA);

    ApplyMarkers();
    m_header->Refresh();
    m_editor->Refresh();
    Refresh();
}

void DiffPane::ApplyMarkers() {
    if (m_editor == nullptr) return;
    for (int marker : {kMarkerAdded, kMarkerRemoved, kMarkerModified}) {
        m_editor->MarkerDeleteAll(marker);
    }
    if (m_limited) return;

    const int lineCount = m_editor->GetLineCount();
    const std::size_t count =
        std::min(m_kinds.size(), static_cast<std::size_t>(std::max(lineCount, 0)));
    for (std::size_t line = 0; line < count; ++line) {
        int marker = 0;
        switch (m_kinds[line]) {
            case diff::LineKind::Added:
                marker = kMarkerAdded;
                break;
            case diff::LineKind::Removed:
                marker = kMarkerRemoved;
                break;
            case diff::LineKind::Modified:
                marker = kMarkerModified;
                break;
            case diff::LineKind::Same:
                break;
        }
        if (marker != 0) m_editor->MarkerAdd(static_cast<int>(line), marker);
    }
}

void DiffPane::UpdateStats() {
    if (m_stats == nullptr) return;

    wxString stats;
    const auto append = [&stats](const wxString& part) {
        if (!stats.empty()) stats += "  ";
        stats += part;
    };

    if (m_base) {
        if (m_diffStats.removed > 0) {
            append(wxString::Format("-%d", static_cast<int>(m_diffStats.removed)));
        }
        if (m_diffStats.modified > 0) {
            append(wxString::Format("~%d", static_cast<int>(m_diffStats.modified)));
        }
    } else {
        if (m_diffStats.added > 0) {
            append(wxString::Format("+%d", static_cast<int>(m_diffStats.added)));
        }
        if (m_diffStats.modified > 0) {
            append(wxString::Format("~%d", static_cast<int>(m_diffStats.modified)));
        }
    }

    if (m_limited) {
        append("diff limited");
    } else if (stats.empty()) {
        stats = m_base ? "base" : "no changes";
    }
    m_stats->SetLabel(stats);
    m_header->Layout();
}

}  // namespace txt

