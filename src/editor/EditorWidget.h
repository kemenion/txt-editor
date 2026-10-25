#pragma once

#include <string>

#include <wx/stc/stc.h>

#include "core/TextTypes.h"

namespace txt {

// Thin wxSTC adapter. All Scintilla calls for a document live here.
class EditorWidget final : public wxStyledTextCtrl {
public:
    explicit EditorWidget(wxWindow* parent, wxWindowID id = wxID_ANY);

    void LoadText(const std::string& utf8, const DocumentMeta& meta);
    std::string GetUtf8() const;

    void SetPath(const std::string& path) { m_path = path; }
    const std::string& Path() const { return m_path; }

    const DocumentMeta& Meta() const { return m_meta; }
    void SetMeta(const DocumentMeta& meta) { m_meta = meta; }

    bool IsLoading() const { return m_loading; }
    bool IsModified() const { return m_forceModified || GetModify(); }
    void MarkRecoveredUnsaved() { m_forceModified = true; }
    void MarkSaved() {
        m_forceModified = false;
        SetSavePoint();
    }

    int CursorPosition() const;
    int AnchorPosition() const;
    int FirstVisibleLine() const;
    void RestoreViewState(int anchor, int caret, int firstVisibleLine, int zoom);

    void ApplyLanguage(LanguageId language);
    void ApplyLargeFileMode(bool large);
    void SetDarkMode(bool dark);
    bool IsDarkMode() const { return m_dark; }
    void SetWordWrap(bool wrap);
    bool IsWordWrap() const { return m_wordWrap; }

    bool FindNext(const wxString& needle, bool matchCase, bool wholeWord, bool forward);
    bool ReplaceCurrent(const wxString& needle, const wxString& replacement,
                        bool matchCase, bool wholeWord);
    int ReplaceAll(const wxString& needle, const wxString& replacement,
                   bool matchCase, bool wholeWord);

    void UpdateLineNumberMargin();

private:
    void ConfigureBase();
    void DefineFoldMarkers();
    void ApplyPalette();
    void ApplyLexerColors();
    bool FindFirstInRange(int from, int to, const wxString& needle, int needleBytes);
    int FindLastInRange(int from, int to, const wxString& needle);
    void SelectMatch(int pos, int bytes);
    bool SelectionMatches(const wxString& needle, bool matchCase);

    void OnUpdateUI(wxStyledTextEvent& event);
    void OnModified(wxStyledTextEvent& event);

    std::string m_path;
    DocumentMeta m_meta;
    bool m_dark = false;
    bool m_wordWrap = false;
    bool m_loading = false;
    bool m_forceModified = false;
};

}  // namespace txt
