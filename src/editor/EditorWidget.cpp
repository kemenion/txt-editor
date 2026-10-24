#include "editor/EditorWidget.h"

#include <algorithm>

#include <wx/font.h>

namespace txt {
namespace {

struct Palette {
    wxColour bg;
    wxColour fg;
    wxColour gutterBg;
    wxColour gutterFg;
    wxColour caret;
    wxColour currentLine;
    wxColour selectionBg;
    wxColour selectionFg;
    wxColour comment;
    wxColour keyword;
    wxColour string;
    wxColour number;
    wxColour function;
    wxColour type;
    wxColour preprocessor;
    wxColour punctuation;
};

Palette MakePalette(bool dark) {
    if (dark) {
        return {
            wxColour(30, 30, 30),    wxColour(220, 220, 220),
            wxColour(37, 37, 37),    wxColour(125, 125, 125),
            wxColour(235, 235, 235), wxColour(40, 44, 52),
            wxColour(38, 79, 120),   wxColour(240, 240, 240),
            wxColour(106, 153, 85),  wxColour(86, 156, 214),
            wxColour(206, 145, 120), wxColour(181, 206, 168),
            wxColour(220, 220, 170), wxColour(78, 201, 176),
            wxColour(197, 134, 192), wxColour(200, 200, 200),
        };
    }
    return {
        wxColour(255, 255, 255), wxColour(30, 30, 30),
        wxColour(247, 247, 247), wxColour(140, 140, 140),
        wxColour(30, 30, 30),    wxColour(245, 248, 252),
        wxColour(180, 210, 255), wxColour(0, 0, 0),
        wxColour(0, 128, 0),     wxColour(0, 0, 200),
        wxColour(163, 21, 21),   wxColour(128, 0, 128),
        wxColour(121, 94, 38),   wxColour(43, 145, 175),
        wxColour(128, 64, 0),    wxColour(100, 100, 100),
    };
}

int ToStcEol(Eol eol) {
    switch (eol) {
        case Eol::Lf: return wxSTC_EOL_LF;
        case Eol::CrLf: return wxSTC_EOL_CRLF;
        case Eol::Cr: return wxSTC_EOL_CR;
    }
    return wxSTC_EOL_LF;
}

}  // namespace

EditorWidget::EditorWidget(wxWindow* parent, wxWindowID id)
    : wxStyledTextCtrl(parent, id) {
    ConfigureBase();
    ApplyPalette();
    Bind(wxEVT_STC_UPDATEUI, &EditorWidget::OnUpdateUI, this);
    Bind(wxEVT_STC_MODIFIED, &EditorWidget::OnModified, this);
}

void EditorWidget::ConfigureBase() {
    SetCodePage(wxSTC_CP_UTF8);
    SetLexer(wxSTC_LEX_NULL);
    SetWrapMode(wxSTC_WRAP_NONE);
    SetTabWidth(4);
    SetIndent(4);
    SetUseTabs(false);
    SetTabIndents(true);
    SetBackSpaceUnIndents(true);
    SetIndentationGuides(wxSTC_IV_LOOKBOTH);
    SetViewWhiteSpace(wxSTC_WS_INVISIBLE);
    SetBufferedDraw(true);
    SetScrollWidthTracking(true);
    SetScrollWidth(1);
    SetEdgeMode(wxSTC_EDGE_NONE);

    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(0, 48);
    SetMarginLeft(4);

    SetMarginType(1, wxSTC_MARGIN_SYMBOL);
    SetMarginWidth(1, 16);
    SetMarginMask(1, wxSTC_MASK_FOLDERS);
    SetMarginSensitive(1, true);
    SetProperty("fold", "1");
    SetProperty("fold.compact", "1");
    SetProperty("fold.comment", "1");
    SetAutomaticFold(wxSTC_AUTOMATICFOLD_CLICK);
    DefineFoldMarkers();

    SetCaretLineVisible(true);
    SetEOLMode(wxSTC_EOL_LF);
}

void EditorWidget::DefineFoldMarkers() {
    MarkerDefine(wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS);
    MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARK_BOXMINUS);
    MarkerDefine(wxSTC_MARKNUM_FOLDERSUB, wxSTC_MARK_VLINE);
    MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARK_LCORNER);
    MarkerDefine(wxSTC_MARKNUM_FOLDEREND, wxSTC_MARK_BOXPLUSCONNECTED);
    MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED);
    MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER);
}

void EditorWidget::ApplyPalette() {
    const Palette p = MakePalette(m_dark);
    const wxFont font(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE));

    StyleSetFont(wxSTC_STYLE_DEFAULT, font);
    StyleSetForeground(wxSTC_STYLE_DEFAULT, p.fg);
    StyleSetBackground(wxSTC_STYLE_DEFAULT, p.bg);
    StyleClearAll();

    for (int style = 0; style <= wxSTC_STYLE_MAX; ++style) {
        StyleSetForeground(style, p.fg);
        StyleSetBackground(style, p.bg);
    }

    StyleSetForeground(wxSTC_STYLE_LINENUMBER, p.gutterFg);
    StyleSetBackground(wxSTC_STYLE_LINENUMBER, p.gutterBg);
    StyleSetForeground(wxSTC_STYLE_INDENTGUIDE, p.gutterFg);
    StyleSetForeground(wxSTC_STYLE_BRACELIGHT, p.keyword);
    StyleSetBackground(wxSTC_STYLE_BRACELIGHT, p.currentLine);
    StyleSetForeground(wxSTC_STYLE_BRACEBAD, wxColour(220, 50, 47));
    StyleSetBackground(wxSTC_STYLE_BRACEBAD, p.currentLine);

    SetCaretForeground(p.caret);
    SetCaretLineBackground(p.currentLine);
    SetCaretLineVisible(true);
    SetSelBackground(true, p.selectionBg);
    SetSelForeground(true, p.selectionFg);
    SetWhitespaceForeground(true, p.gutterFg);
    SetFoldMarginColour(true, p.gutterBg);
    SetFoldMarginHiColour(true, p.gutterBg);

    for (int marker = wxSTC_MARKNUM_FOLDEREND; marker <= wxSTC_MARKNUM_FOLDEROPEN; ++marker) {
        MarkerSetBackground(marker, p.gutterBg);
        MarkerSetForeground(marker, p.gutterFg);
    }

    ApplyLexerColors();
}


void EditorWidget::ApplyLexerColors() {
    const Palette p = MakePalette(m_dark);
    switch (m_meta.language) {
        case LanguageId::Python:
            StyleSetForeground(wxSTC_P_COMMENTLINE, p.comment);
            StyleSetForeground(wxSTC_P_COMMENTBLOCK, p.comment);
            StyleSetForeground(wxSTC_P_NUMBER, p.number);
            StyleSetForeground(wxSTC_P_STRING, p.string);
            StyleSetForeground(wxSTC_P_CHARACTER, p.string);
            StyleSetForeground(wxSTC_P_TRIPLE, p.string);
            StyleSetForeground(wxSTC_P_TRIPLEDOUBLE, p.string);
            StyleSetForeground(wxSTC_P_WORD, p.keyword);
            StyleSetBold(wxSTC_P_WORD, true);
            StyleSetForeground(wxSTC_P_DEFNAME, p.function);
            StyleSetForeground(wxSTC_P_CLASSNAME, p.type);
            StyleSetForeground(wxSTC_P_OPERATOR, p.punctuation);
            break;
        case LanguageId::C:
        case LanguageId::Cpp:
            StyleSetForeground(wxSTC_C_COMMENT, p.comment);
            StyleSetForeground(wxSTC_C_COMMENTLINE, p.comment);
            StyleSetForeground(wxSTC_C_COMMENTDOC, p.comment);
            StyleSetForeground(wxSTC_C_NUMBER, p.number);
            StyleSetForeground(wxSTC_C_STRING, p.string);
            StyleSetForeground(wxSTC_C_CHARACTER, p.string);
            StyleSetForeground(wxSTC_C_WORD, p.keyword);
            StyleSetBold(wxSTC_C_WORD, true);
            StyleSetForeground(wxSTC_C_WORD2, p.type);
            StyleSetForeground(wxSTC_C_PREPROCESSOR, p.preprocessor);
            StyleSetForeground(wxSTC_C_OPERATOR, p.punctuation);
            break;
        case LanguageId::Json:
            StyleSetForeground(wxSTC_JSON_PROPERTYNAME, p.function);
            StyleSetForeground(wxSTC_JSON_STRING, p.string);
            StyleSetForeground(wxSTC_JSON_NUMBER, p.number);
            StyleSetForeground(wxSTC_JSON_KEYWORD, p.keyword);
            StyleSetForeground(wxSTC_JSON_OPERATOR, p.punctuation);
            StyleSetForeground(wxSTC_JSON_ERROR, wxColour(220, 50, 47));
            break;
        case LanguageId::Markdown:
        case LanguageId::Shell:
        case LanguageId::Xml:
        case LanguageId::Yaml:
        case LanguageId::Plain:
            break;
    }
}

void EditorWidget::ApplyLanguage(LanguageId language) {
    m_meta.language = language;
    if (m_meta.largeFile) {
        SetLexer(wxSTC_LEX_NULL);
        return;
    }

    switch (language) {
        case LanguageId::Python:
            SetLexer(wxSTC_LEX_PYTHON);
            SetKeyWords(0,
                        "False None True and as assert async await break class continue def del "
                        "elif else except finally for from global if import in is lambda nonlocal "
                        "not or pass raise return try while with yield match case");
            break;
        case LanguageId::C:
        case LanguageId::Cpp:
            SetLexer(wxSTC_LEX_CPP);
            SetKeyWords(0,
                        "alignas alignof and and_eq asm auto bitand bitor bool break case catch "
                        "char char8_t char16_t char32_t class compl concept const consteval "
                        "constexpr constinit const_cast continue co_await co_return co_yield "
                        "decltype default delete do double dynamic_cast else enum explicit export "
                        "extern false float for friend goto if inline int long mutable namespace "
                        "new noexcept not not_eq nullptr operator or or_eq private protected "
                        "public register reinterpret_cast requires return short signed sizeof "
                        "static static_assert static_cast struct switch template this thread_local "
                        "throw true try typedef typeid typename union unsigned using virtual void "
                        "volatile wchar_t while xor xor_eq");
            break;
        case LanguageId::Json:
            SetLexer(wxSTC_LEX_JSON);
            break;
        case LanguageId::Markdown:
            SetLexer(wxSTC_LEX_MARKDOWN);
            break;
        case LanguageId::Shell:
            SetLexer(wxSTC_LEX_BASH);
            SetKeyWords(0, "if then else elif fi for while in do done case esac function select "
                           "until time coproc");
            break;
        case LanguageId::Xml:
            SetLexer(wxSTC_LEX_HTML);
            break;
        case LanguageId::Yaml:
            SetLexer(wxSTC_LEX_YAML);
            break;
        case LanguageId::Plain:
            SetLexer(wxSTC_LEX_NULL);
            break;
    }
    ApplyPalette();
}

void EditorWidget::ApplyLargeFileMode(bool large) {
    m_meta.largeFile = large;
    if (large) {
        SetLexer(wxSTC_LEX_NULL);
        SetMarginWidth(1, 0);
        SetProperty("fold", "0");
        SetUndoCollection(false);
        return;
    }
    SetUndoCollection(true);
    SetMarginWidth(1, 16);
    SetProperty("fold", "1");
    ApplyLanguage(m_meta.language);
}

void EditorWidget::SetDarkMode(bool dark) {
    if (m_dark == dark) return;
    m_dark = dark;
    ApplyPalette();
}

void EditorWidget::LoadText(const std::string& utf8, const DocumentMeta& meta) {
    m_loading = true;
    m_forceModified = false;
    SetReadOnly(false);
    ClearAll();
    AddTextRaw(utf8.data(), static_cast<int>(utf8.size()));
    m_meta = meta;
    SetEOLMode(ToStcEol(meta.eol));

    ApplyLanguage(meta.language);
    ApplyLargeFileMode(meta.largeFile);

    EmptyUndoBuffer();
    SetSavePoint();
    SetSelection(0, 0);
    GotoPos(0);
    m_loading = false;
    UpdateLineNumberMargin();
}

std::string EditorWidget::GetUtf8() const {
    const wxCharBuffer buffer = const_cast<EditorWidget*>(this)->GetTextRaw();
    return std::string(buffer.data(), buffer.length());
}

int EditorWidget::CursorPosition() const {
    return const_cast<EditorWidget*>(this)->GetCurrentPos();
}

int EditorWidget::AnchorPosition() const {
    return const_cast<EditorWidget*>(this)->GetAnchor();
}

int EditorWidget::FirstVisibleLine() const {
    return const_cast<EditorWidget*>(this)->GetFirstVisibleLine();
}

void EditorWidget::RestoreViewState(int anchor, int caret, int firstVisibleLine,
                                    int zoom) {
    const int length = GetTextLength();
    caret = std::clamp(caret, 0, length);
    anchor = std::clamp(anchor, 0, length);
    const int lastLine = std::max(0, GetLineCount() - 1);
    firstVisibleLine = std::clamp(firstVisibleLine, 0, lastLine);

    SetZoom(std::clamp(zoom, -10, 40));
    SetCurrentPos(caret);
    SetAnchor(anchor);
    SetFirstVisibleLine(firstVisibleLine);
}

void EditorWidget::UpdateLineNumberMargin() {
    const int lines = std::max(1, GetLineCount());
    const int textWidth =
        TextWidth(wxSTC_STYLE_LINENUMBER, wxString::Format("%d", lines));
    SetMarginWidth(0, std::max(36, textWidth + 12));
}

void EditorWidget::OnUpdateUI(wxStyledTextEvent& event) {
    UpdateLineNumberMargin();
    event.Skip();
}

void EditorWidget::OnModified(wxStyledTextEvent& event) {
    if (!m_loading &&
        (event.GetModificationType() & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) != 0) {
        UpdateLineNumberMargin();
    }
    event.Skip();
}

bool EditorWidget::FindFirstInRange(int from, int to, const wxString& needle,
                                    int needleBytes) {
    const int length = GetTextLength();
    from = std::clamp(from, 0, length);
    to = std::clamp(to, 0, length);
    if (from >= to) return false;

    SetTargetStart(from);
    SetTargetEnd(to);
    const int pos = SearchInTarget(needle);
    if (pos < 0) return false;
    SelectMatch(pos, needleBytes);
    return true;
}

int EditorWidget::FindLastInRange(int from, int to, const wxString& needle) {
    const int length = GetTextLength();
    from = std::clamp(from, 0, length);
    to = std::clamp(to, 0, length);
    if (from >= to) return -1;

    int last = -1;
    int cursor = from;
    while (cursor < to) {
        SetTargetStart(cursor);
        SetTargetEnd(to);
        const int pos = SearchInTarget(needle);
        if (pos < 0 || pos >= to) break;
        last = pos;
        cursor = pos + 1;
    }
    return last;
}

void EditorWidget::SelectMatch(int pos, int bytes) {
    SetSelection(pos, pos + bytes);
    EnsureCaretVisible();
}

bool EditorWidget::FindNext(const wxString& needle, bool matchCase, bool wholeWord,
                            bool forward) {
    if (needle.empty()) return false;

    const wxCharBuffer needleUtf8 = needle.utf8_str();
    const int needleBytes = static_cast<int>(needleUtf8.length());
    int flags = 0;
    if (matchCase) flags |= wxSTC_FIND_MATCHCASE;
    if (wholeWord) flags |= wxSTC_FIND_WHOLEWORD;
    SetSearchFlags(flags);

    const int length = GetTextLength();
    const int selStart = GetSelectionStart();
    const int selEnd = GetSelectionEnd();

    if (forward) {
        if (FindFirstInRange(selEnd, length, needle, needleBytes)) return true;
        return FindFirstInRange(0, selEnd, needle, needleBytes);
    }

    int found = FindLastInRange(0, selStart, needle);
    if (found < 0) found = FindLastInRange(selEnd, length, needle);
    if (found < 0) return false;
    SelectMatch(found, needleBytes);
    return true;
}

bool EditorWidget::SelectionMatches(const wxString& needle, bool matchCase) {
    const wxString selected = GetSelectedText();
    return matchCase ? selected == needle : selected.CmpNoCase(needle) == 0;
}

bool EditorWidget::ReplaceCurrent(const wxString& needle, const wxString& replacement,
                                  bool matchCase, bool wholeWord) {
    (void)wholeWord;
    if (!SelectionMatches(needle, matchCase)) return false;
    ReplaceSelection(replacement);
    return true;
}

int EditorWidget::ReplaceAll(const wxString& needle, const wxString& replacement,
                             bool matchCase, bool wholeWord) {
    if (needle.empty()) return 0;

    int flags = 0;
    if (matchCase) flags |= wxSTC_FIND_MATCHCASE;
    if (wholeWord) flags |= wxSTC_FIND_WHOLEWORD;
    SetSearchFlags(flags);

    int count = 0;
    int searchFrom = 0;
    BeginUndoAction();
    for (;;) {
        const int length = GetTextLength();
        if (searchFrom > length) break;
        SetTargetStart(searchFrom);
        SetTargetEnd(length);
        const int pos = SearchInTarget(needle);
        if (pos < 0) break;
        const int foundBytes = GetTargetEnd() - GetTargetStart();
        SetTargetStart(pos);
        SetTargetEnd(pos + foundBytes);
        const int inserted = ReplaceTarget(replacement);
        ++count;
        searchFrom = pos + inserted;
    }
    EndUndoAction();

    if (count > 0) {
        SetSelection(searchFrom, searchFrom);
        EnsureCaretVisible();
    }
    return count;
}

}  // namespace txt

