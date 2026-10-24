#include "ui/MainFrame.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include <wx/config.h>
#include <wx/filehistory.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>

#include "app/Profile.h"
#include "core/Document.h"
#include "core/Encoding.h"
#include "core/Language.h"
#include "core/Session.h"
#include "core/TextTypes.h"
#include "editor/EditorWidget.h"
#include "ui/FindPanel.h"
#include "ui/SaveChangesDialog.h"

namespace txt {
namespace {

enum : int {
    ID_FIND_NEXT = wxID_HIGHEST + 200,
    ID_FIND_PREV,
    ID_TOGGLE_THEME,
    ID_ZOOM_IN,
    ID_ZOOM_OUT,
    ID_ZOOM_RESET,
};

wxString FileNameOf(const std::string& path) {
    if (path.empty()) return "Untitled";
    return wxFileName(wxString::FromUTF8(path)).GetFullName();
}

}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "txt-editor", wxDefaultPosition, wxSize(1100, 760)) {
    SetMinSize(wxSize(640, 400));

    m_notebook = new TabBook(this, wxID_ANY);
    m_findPanel = new FindPanel(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_notebook, 1, wxEXPAND);
    sizer->Add(m_findPanel, 0, wxEXPAND);
    SetSizer(sizer);

    CreateStatusBar(3);
    const int widths[] = {-3, 170, 300};
    SetStatusWidths(3, widths);

    m_fileHistory = new wxFileHistory(9);
    BuildMenu();
    m_findPanel->Hide();

    wxConfigBase* config = wxConfigBase::Get();
    m_dark = config != nullptr && config->ReadBool("/view/dark", false);
    m_notebook->SetDarkMode(m_dark);
    if (m_themeItem) m_themeItem->Check(m_dark);
    if (m_fileHistory && config) m_fileHistory->Load(*config);
    m_sessionDirectory = DefaultSessionDirectory();

    Bind(wxEVT_MENU, &MainFrame::OnNew, this, wxID_NEW);
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnSave, this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs, this, wxID_SAVEAS);
    Bind(wxEVT_MENU, &MainFrame::OnCloseTab, this, wxID_CLOSE);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnUndo, this, wxID_UNDO);
    Bind(wxEVT_MENU, &MainFrame::OnRedo, this, wxID_REDO);
    Bind(wxEVT_MENU, &MainFrame::OnCut, this, wxID_CUT);
    Bind(wxEVT_MENU, &MainFrame::OnCopy, this, wxID_COPY);
    Bind(wxEVT_MENU, &MainFrame::OnPaste, this, wxID_PASTE);
    Bind(wxEVT_MENU, &MainFrame::OnSelectAll, this, wxID_SELECTALL);
    Bind(wxEVT_MENU, &MainFrame::OnFind, this, wxID_FIND);
    Bind(wxEVT_MENU, &MainFrame::OnReplace, this, wxID_REPLACE);
    Bind(wxEVT_MENU, &MainFrame::OnFindNext, this, ID_FIND_NEXT);
    Bind(wxEVT_MENU, &MainFrame::OnFindPrev, this, ID_FIND_PREV);
    Bind(wxEVT_MENU, &MainFrame::OnToggleTheme, this, ID_TOGGLE_THEME);
    Bind(wxEVT_MENU, &MainFrame::OnZoomIn, this, ID_ZOOM_IN);
    Bind(wxEVT_MENU, &MainFrame::OnZoomOut, this, ID_ZOOM_OUT);
    Bind(wxEVT_MENU, &MainFrame::OnZoomReset, this, ID_ZOOM_RESET);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::OnRecent, this, wxID_FILE1, wxID_FILE9);

    Bind(wxEVT_BUTTON, &MainFrame::OnFindNext, this, FindPanel::ID_FIND_NEXT);
    Bind(wxEVT_BUTTON, &MainFrame::OnFindPrev, this, FindPanel::ID_FIND_PREV);
    Bind(wxEVT_BUTTON, &MainFrame::OnFindClose, this, FindPanel::ID_CLOSE);
    Bind(wxEVT_BUTTON, &MainFrame::OnReplaceOne, this, FindPanel::ID_REPLACE_ONE);
    Bind(wxEVT_BUTTON, &MainFrame::OnReplaceAll, this, FindPanel::ID_REPLACE_ALL);

    Bind(EVT_TAB_CHANGED, &MainFrame::OnPageChanged, this);
    Bind(EVT_TAB_CLOSE, &MainFrame::OnTabCloseRequest, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    NewDocument();
    Layout();
}

MainFrame::~MainFrame() {
    if (m_fileHistory) {
        if (wxConfigBase* config = wxConfigBase::Get()) {
            m_fileHistory->Save(*config);
            config->Flush();
        }
        delete m_fileHistory;
    }
}

void MainFrame::BuildMenu() {
    auto* bar = new wxMenuBar;

    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(wxID_SAVEAS, "Save &As...\tCtrl+Shift+S");
    file->AppendSeparator();
    m_fileHistory->UseMenu(file);
    file->AppendSeparator();
    file->Append(wxID_CLOSE, "&Close Tab\tCtrl+W");
    file->Append(wxID_EXIT, "&Quit\tCtrl+Q");
    bar->Append(file, "&File");

    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Shift+Z");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");
    edit->Append(wxID_SELECTALL, "Select &All\tCtrl+A");
    edit->AppendSeparator();
    edit->Append(wxID_FIND, "&Find...\tCtrl+F");
    edit->Append(wxID_REPLACE, "&Replace...\tCtrl+H");
    edit->Append(ID_FIND_NEXT, "Find &Next\tF3");
    edit->Append(ID_FIND_PREV, "Find &Previous\tShift+F3");
    bar->Append(edit, "&Edit");

    auto* view = new wxMenu;
    view->Append(ID_ZOOM_IN, "Zoom &In\tCtrl++");
    view->Append(ID_ZOOM_OUT, "Zoom &Out\tCtrl+-");
    view->Append(ID_ZOOM_RESET, "&Reset Zoom\tCtrl+0");
    view->AppendSeparator();
    m_themeItem = view->AppendCheckItem(ID_TOGGLE_THEME, "&Dark Mode\tF11");
    bar->Append(view, "&View");

    auto* help = new wxMenu;
    help->Append(wxID_ABOUT, "&About txt-editor");
    bar->Append(help, "&Help");

    SetMenuBar(bar);
}

void MainFrame::BindEditor(EditorWidget* editor) {
    editor->Bind(wxEVT_STC_UPDATEUI, [this, editor](wxStyledTextEvent& event) {
        if (editor == CurrentEditor()) UpdateStatus();
        event.Skip();
    });
    editor->Bind(wxEVT_STC_MODIFIED, [this, editor](wxStyledTextEvent& event) {
        RefreshTab(editor);
        if (editor == CurrentEditor()) UpdateTitle();
        event.Skip();
    });
}

EditorWidget* MainFrame::CurrentEditor() const {
    if (!m_notebook) return nullptr;
    return dynamic_cast<EditorWidget*>(m_notebook->GetCurrentPage());
}

int MainFrame::PageIndex(const EditorWidget* editor) const {
    const auto it = std::find(m_pages.begin(), m_pages.end(), editor);
    return it == m_pages.end() ? -1 : static_cast<int>(std::distance(m_pages.begin(), it));
}

MainFrame::TabState* MainFrame::StateAt(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabStates.size())) return nullptr;
    return &m_tabStates[static_cast<std::size_t>(index)];
}

const MainFrame::TabState* MainFrame::StateAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_tabStates.size())) return nullptr;
    return &m_tabStates[static_cast<std::size_t>(index)];
}

void MainFrame::AppendPage(EditorWidget* editor, const wxString& title, TabState state) {
    const bool previous = m_suppressPageChanged;
    m_suppressPageChanged = true;
    m_notebook->AddPage(editor, title, true);
    m_pages.push_back(editor);
    m_tabStates.push_back(std::move(state));
    BindEditor(editor);
    m_suppressPageChanged = previous;
}

void MainFrame::DeletePage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) return;
    const bool previous = m_suppressPageChanged;
    m_suppressPageChanged = true;
    m_notebook->DeletePage(static_cast<std::size_t>(index));
    m_pages.erase(m_pages.begin() + index);
    m_tabStates.erase(m_tabStates.begin() + index);
    m_suppressPageChanged = previous;
}

void MainFrame::CreatePage(const std::string& utf8, const DocumentMeta& meta,
                           const std::string& path) {
    // Reuse the pristine "Untitled" page when the first real file is opened:
    // launching with a file should not leave an extra empty tab behind.
    if (!path.empty() && m_pages.size() == 1) {
        EditorWidget* first = m_pages.front();
        if (first->Path().empty() && !first->IsModified()) {
            DeletePage(0);
        }
    }

    auto* editor = new EditorWidget(m_notebook->PageParent());
    editor->SetDarkMode(m_dark);
    editor->SetPath(path);
    editor->LoadText(utf8, meta);

    TabState state;
    state.id = MakeSessionId();
    state.loaded = true;
    AppendPage(editor, FileNameOf(path), std::move(state));
    UpdateTitle();
    UpdateStatus();
}

bool MainFrame::OpenPath(const std::string& path, bool quiet) {
    std::error_code ec;
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::path(path), ec).lexically_normal();
    const std::string normalized = ec ? path : absolute.string();

    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i]->Path() == normalized) {
            m_notebook->SetSelection(static_cast<std::size_t>(i));
            EnsureTabLoaded(static_cast<int>(i));
            UpdateTitle();
            UpdateStatus();
            return true;
        }
    }

    profile::Mark("load begin");
    const LoadResult loaded = LoadDocument(normalized);
    if (!loaded.ok) {
        if (!quiet) {
            wxMessageBox(wxString::FromUTF8(loaded.error), "Open failed",
                         wxOK | wxICON_ERROR, this);
        }
        return false;
    }
    profile::MarkBytes("load done", loaded.utf8.size());
    CreatePage(loaded.utf8, loaded.meta, normalized);
    m_fileHistory->AddFileToHistory(wxString::FromUTF8(normalized));
    if (!loaded.error.empty()) {
        SetStatusText(wxString::FromUTF8(loaded.error), 0);
    }
    return true;
}

void MainFrame::NewDocument() {
    DocumentMeta meta;
    CreatePage(std::string(), meta, std::string());
}

bool MainFrame::EnsureTabLoaded(int index) {
    TabState* state = StateAt(index);
    if (state == nullptr) return false;
    if (state->loaded) return true;
    if (index < 0 || index >= static_cast<int>(m_pages.size())) return false;

    EditorWidget* editor = m_pages[static_cast<std::size_t>(index)];
    std::string utf8;
    std::string error;
    DocumentMeta meta = editor->Meta();

    if (!state->contentFile.empty()) {
        if (m_sessionDirectory.empty() ||
            !ReadSessionContent(m_sessionDirectory, state->contentFile, &utf8, &error)) {
            if (error.empty()) error = "Session directory is not available.";
        }
    } else if (!editor->Path().empty()) {
        profile::Mark("session tab load begin");
        const LoadResult loaded = LoadDocument(editor->Path());
        if (loaded.ok) {
            utf8 = loaded.utf8;
            meta = loaded.meta;
        } else {
            error = loaded.error;
        }
    } else {
        error = "Session tab has no content.";
    }

    if (!error.empty()) {
        state->loadError = error;
        editor->SetReadOnly(true);
        if (editor == CurrentEditor()) UpdateStatus();
        return false;
    }

    state->loaded = true;
    state->loadError.clear();
    editor->LoadText(utf8, meta);
    if (state->recoveredModified) editor->MarkRecoveredUnsaved();
    editor->RestoreViewState(state->anchor, state->caret, state->firstVisibleLine,
                             state->zoom);
    profile::MarkBytes("session tab loaded", utf8.size());
    RefreshTab(editor);
    if (editor == CurrentEditor()) {
        UpdateTitle();
        UpdateStatus();
    }
    return true;
}

void MainFrame::CaptureViewState(EditorWidget* editor, TabState& state) const {
    if (!state.loaded || editor == nullptr) return;
    state.anchor = editor->AnchorPosition();
    state.caret = editor->CursorPosition();
    state.firstVisibleLine = editor->FirstVisibleLine();
    state.zoom = editor->GetZoom();
}

bool MainFrame::PersistSessionForRestart(std::string* error) {
    const std::string directory =
        m_sessionDirectory.empty() ? DefaultSessionDirectory() : m_sessionDirectory;
    if (directory.empty()) {
        if (error) *error = "Cannot determine the session directory.";
        return false;
    }

    SessionInfo info;
    const int current = m_notebook ? m_notebook->GetSelection() : wxNOT_FOUND;
    info.active = current >= 0 ? current : 0;

    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        EditorWidget* editor = m_pages[i];
        TabState& state = m_tabStates[i];
        if (state.id.empty()) state.id = MakeSessionId();

        SessionTabInfo tab;
        tab.id = state.id;
        tab.path = editor->Path();
        tab.meta = editor->Meta();
        tab.modified = state.loaded ? editor->IsModified() : state.recoveredModified;

        if (state.loaded) {
            state.recoveredModified = tab.modified;
            CaptureViewState(editor, state);
            if (tab.path.empty() || tab.modified) {
                if (!WriteSessionContent(directory, state.id, editor->GetUtf8(),
                                         &state.contentFile, error)) {
                    return false;
                }
            } else {
                state.contentFile.clear();
            }
        } else {
            if ((tab.path.empty() || tab.modified) && state.contentFile.empty()) {
                if (error) *error = "An unloaded tab has no recovery snapshot.";
                return false;
            }
            if (!tab.path.empty() && !tab.modified) state.contentFile.clear();
        }

        tab.anchor = state.anchor;
        tab.caret = state.caret;
        tab.firstVisibleLine = state.firstVisibleLine;
        tab.zoom = state.zoom;
        tab.contentFile = state.contentFile;
        info.tabs.push_back(std::move(tab));
    }

    if (!SaveSession(directory, info, error)) return false;
    m_sessionDirectory = directory;
    return true;
}

void MainFrame::RestoreSession() {
    if (m_sessionDirectory.empty()) m_sessionDirectory = DefaultSessionDirectory();
    if (m_sessionDirectory.empty()) return;

    profile::Mark("session restore begin");
    const SessionLoadResult result = LoadSession(m_sessionDirectory);
    if (!result.found) {
        profile::Mark("session restore none");
        return;
    }
    if (!result.ok) {
        SetStatusText(wxString::FromUTF8("Session restore failed: " + result.error), 0);
        profile::Mark("session restore failed");
        return;
    }
    if (result.session.tabs.empty()) return;

    m_suppressPageChanged = true;
    while (!m_pages.empty()) {
        const std::size_t last = m_pages.size() - 1;
        m_notebook->DeletePage(last);
        m_pages.pop_back();
        m_tabStates.pop_back();
    }

    for (const SessionTabInfo& tab : result.session.tabs) {
        auto* editor = new EditorWidget(m_notebook->PageParent());
        editor->SetDarkMode(m_dark);
        editor->SetPath(tab.path);
        editor->SetMeta(tab.meta);

        TabState state;
        state.id = tab.id;
        state.contentFile = tab.contentFile;
        state.loaded = false;
        state.recoveredModified = tab.modified;
        state.anchor = tab.anchor;
        state.caret = tab.caret;
        state.firstVisibleLine = tab.firstVisibleLine;
        state.zoom = tab.zoom;
        if (tab.modified) editor->MarkRecoveredUnsaved();

        wxString title = FileNameOf(tab.path);
        if (tab.modified) title += " *";
        m_notebook->AddPage(editor, title, false);
        m_pages.push_back(editor);
        m_tabStates.push_back(std::move(state));
        BindEditor(editor);
    }

    const int active = std::clamp(result.session.active, 0,
                                  static_cast<int>(m_pages.size()) - 1);
    m_notebook->SetSelection(static_cast<std::size_t>(active));
    m_suppressPageChanged = false;

    EnsureTabLoaded(active);
    UpdateTitle();
    UpdateStatus();
    profile::Mark("session restore done");
}

void MainFrame::RefreshTab(EditorWidget* editor) {
    const int index = PageIndex(editor);
    if (index < 0) return;
    wxString title = FileNameOf(editor->Path());
    if (editor->IsModified()) title += " *";
    m_notebook->SetPageText(static_cast<std::size_t>(index), title);
}

void MainFrame::UpdateTitle() {
    EditorWidget* editor = CurrentEditor();
    wxString title = editor ? FileNameOf(editor->Path()) : "txt-editor";
    if (editor && editor->IsModified()) title += " *";
    title += " - txt-editor";
    SetTitle(title);
    if (editor) RefreshTab(editor);
}

void MainFrame::UpdateStatus() {
    EditorWidget* editor = CurrentEditor();
    if (!editor) {
        SetStatusText(wxEmptyString, 0);
        SetStatusText(wxEmptyString, 1);
        SetStatusText(wxEmptyString, 2);
        return;
    }

    const int index = m_notebook->GetSelection();
    const TabState* state = StateAt(index);
    if (state != nullptr && !state->loadError.empty()) {
        SetStatusText(wxString::FromUTF8("Could not restore: " + state->loadError), 0);
    } else {
        SetStatusText(editor->Path().empty()
                          ? "Untitled"
                          : wxString::FromUTF8(editor->Path().c_str()),
                      0);
    }
    SetStatusText(wxString::Format("Ln %d, Col %d", editor->GetCurrentLine() + 1,
                                   editor->GetColumn(editor->GetCurrentPos()) + 1),
                  1);

    wxString meta = wxString::FromUTF8(std::string(EncodingName(editor->Meta().encoding)));
    meta += "  ";
    meta += wxString::FromUTF8(std::string(EolName(editor->Meta().eol)));
    meta += "  ";
    meta += wxString::FromUTF8(std::string(LanguageName(editor->Meta().language)));
    if (editor->Meta().largeFile) meta += "  [large file]";
    SetStatusText(meta, 2);
}

void MainFrame::ApplyTheme() {
    for (EditorWidget* editor : m_pages) editor->SetDarkMode(m_dark);
    m_notebook->SetDarkMode(m_dark);
    if (m_themeItem) m_themeItem->Check(m_dark);
    if (wxConfigBase* config = wxConfigBase::Get()) {
        config->Write("/view/dark", m_dark);
        config->Flush();
    }
}

void MainFrame::ShowFindPanel(bool show, bool replace) {
    if (!show) {
        m_findPanel->Hide();
        Layout();
        if (EditorWidget* editor = CurrentEditor()) editor->SetFocus();
        return;
    }

    m_findPanel->Show();
    m_findPanel->ShowReplace(replace);
    Layout();
    if (!replace) m_findPanel->FocusFind();
}

bool MainFrame::MaybeSave(EditorWidget* editor) {
    if (!editor || !editor->IsModified()) return true;

    const int index = PageIndex(editor);
    if (index >= 0 && !EnsureTabLoaded(index)) {
        const TabState* state = StateAt(index);
        const wxString detail =
            state != nullptr ? wxString::FromUTF8(state->loadError) : wxString();
        return ConfirmDiscardUnloadedTab(this, FileNameOf(editor->Path()), detail);
    }

    const wxString name = FileNameOf(editor->Path());
    const wxString path = wxString::FromUTF8(editor->Path());
    switch (ShowSaveChangesPrompt(this, name, path)) {
        case SaveDecision::Save:
            if (index >= 0) m_notebook->SetSelection(static_cast<std::size_t>(index));
            return SaveCurrent(false);
        case SaveDecision::Discard:
            return true;
        case SaveDecision::Cancel:
            return false;
    }
    return false;
}

bool MainFrame::MaybeSaveAll() {
    for (EditorWidget* editor : m_pages) {
        if (!MaybeSave(editor)) return false;
    }
    return true;
}

bool MainFrame::SaveCurrent(bool saveAs) {
    EditorWidget* editor = CurrentEditor();
    if (!editor) return false;

    const int editorIndex = PageIndex(editor);
    const TabState* editorState = StateAt(editorIndex);
    if (editorState != nullptr && !editorState->loaded &&
        !editorState->loadError.empty()) {
        wxMessageBox("This tab could not be loaded, so it cannot be saved yet:\n\n" +
                         wxString::FromUTF8(editorState->loadError),
                     "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }

    std::string path = editor->Path();
    if (saveAs || path.empty()) {
        const wxString defaultName =
            path.empty() ? "Untitled.txt" : wxString::FromUTF8(path.c_str());
        wxFileDialog dialog(
            this, "Save File", wxEmptyString, defaultName,
            "Text files (*.txt;*.md;*.py;*.c;*.cpp;*.h;*.hpp;*.json;*.yaml;*.yml;*.xml;*.sh)"
            "|*.txt;*.md;*.py;*.c;*.cpp;*.h;*.hpp;*.json;*.yaml;*.yml;*.xml;*.sh"
            "|All files (*.*)|*.*",
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) return false;
        path = dialog.GetPath().utf8_string();
        if (path.empty()) return false;
    }

    std::string error;
    if (!SaveDocument(path, editor->GetUtf8(), editor->Meta(), &error)) {
        wxMessageBox(wxString::FromUTF8(error), "Save failed", wxOK | wxICON_ERROR, this);
        return false;
    }

    editor->SetPath(path);
    editor->MarkSaved();
    if (TabState* state = StateAt(PageIndex(editor))) {
        state->recoveredModified = false;
        state->contentFile.clear();
        state->loadError.clear();
    }
    m_fileHistory->AddFileToHistory(wxString::FromUTF8(path.c_str()));
    RefreshTab(editor);
    UpdateTitle();
    UpdateStatus();
    return true;
}

void MainFrame::CloseCurrent() {
    CloseTab(m_notebook->GetSelection());
}

void MainFrame::CloseTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) return;

    EditorWidget* editor = m_pages[static_cast<std::size_t>(index)];
    // A clean inactive tab can be closed without loading it. Modified tabs are
    // made active first so the user sees what the save prompt refers to.
    if (editor->IsModified() && m_notebook->GetSelection() != index) {
        m_notebook->SetSelection(static_cast<std::size_t>(index));
    }
    if (!MaybeSave(editor)) return;

    DeletePage(index);
    if (m_pages.empty()) {
        NewDocument();
    } else {
        EnsureTabLoaded(m_notebook->GetSelection());
        UpdateTitle();
        UpdateStatus();
    }
    if (EditorWidget* current = CurrentEditor()) current->SetFocus();
}

void MainFrame::FindNext(bool forward) {
    EditorWidget* editor = CurrentEditor();
    if (!editor) return;

    const wxString query = m_findPanel->Query();
    if (query.empty()) {
        ShowFindPanel(true, false);
        return;
    }
    if (!m_findPanel->IsShown()) {
        m_findPanel->Show();
        Layout();
    }

    const bool found =
        editor->FindNext(query, m_findPanel->MatchCase(), m_findPanel->WholeWord(), forward);
    m_findPanel->SetStatus(found ? wxString() : wxString("Not found"));
    UpdateStatus();
}

void MainFrame::ReplaceOne() {
    EditorWidget* editor = CurrentEditor();
    if (!editor) return;

    const wxString query = m_findPanel->Query();
    const wxString replacement = m_findPanel->Replacement();
    if (query.empty()) return;

    if (editor->ReplaceCurrent(query, replacement, m_findPanel->MatchCase(),
                               m_findPanel->WholeWord())) {
        editor->FindNext(query, m_findPanel->MatchCase(), m_findPanel->WholeWord(), true);
        m_findPanel->SetStatus("Replaced");
    } else {
        const bool found = editor->FindNext(query, m_findPanel->MatchCase(),
                                            m_findPanel->WholeWord(), true);
        m_findPanel->SetStatus(found ? "Press Replace again" : "Not found");
    }
    UpdateStatus();
}

void MainFrame::ReplaceAll() {
    EditorWidget* editor = CurrentEditor();
    if (!editor) return;

    const wxString query = m_findPanel->Query();
    if (query.empty()) return;
    const int count = editor->ReplaceAll(query, m_findPanel->Replacement(),
                                         m_findPanel->MatchCase(), m_findPanel->WholeWord());
    m_findPanel->SetStatus(wxString::Format("Replaced %d occurrence(s)", count));
    UpdateTitle();
    UpdateStatus();
}

void MainFrame::OnNew(wxCommandEvent& event) {
    (void)event;
    NewDocument();
}

void MainFrame::OnOpen(wxCommandEvent& event) {
    (void)event;
    wxFileDialog dialog(this, "Open File", wxEmptyString, wxEmptyString,
                        "Text files (*.txt;*.md;*.py;*.c;*.cpp;*.h;*.hpp;*.json;*.yaml;*.yml;"
                        "*.xml;*.sh)|*.txt;*.md;*.py;*.c;*.cpp;*.h;*.hpp;*.json;*.yaml;*.yml;"
                        "*.xml;*.sh|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    OpenPath(dialog.GetPath().utf8_string());
}

void MainFrame::OnSave(wxCommandEvent& event) {
    (void)event;
    SaveCurrent(false);
}

void MainFrame::OnSaveAs(wxCommandEvent& event) {
    (void)event;
    SaveCurrent(true);
}

void MainFrame::OnCloseTab(wxCommandEvent& event) {
    (void)event;
    CloseCurrent();
}

void MainFrame::OnTabCloseRequest(wxBookCtrlEvent& event) {
    CloseTab(event.GetSelection());
}

void MainFrame::OnQuit(wxCommandEvent& event) {
    (void)event;
    Close(false);
}

void MainFrame::OnUndo(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->Undo();
}

void MainFrame::OnRedo(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->Redo();
}

void MainFrame::OnCut(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->Cut();
}

void MainFrame::OnCopy(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->Copy();
}

void MainFrame::OnPaste(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->Paste();
}

void MainFrame::OnSelectAll(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->SelectAll();
}

void MainFrame::OnFind(wxCommandEvent& event) {
    (void)event;
    ShowFindPanel(true, false);
}


void MainFrame::OnReplace(wxCommandEvent& event) {
    (void)event;
    ShowFindPanel(true, true);
}

void MainFrame::OnFindNext(wxCommandEvent& event) {
    (void)event;
    FindNext(true);
}

void MainFrame::OnFindPrev(wxCommandEvent& event) {
    (void)event;
    FindNext(false);
}

void MainFrame::OnFindClose(wxCommandEvent& event) {
    (void)event;
    ShowFindPanel(false, false);
}

void MainFrame::OnReplaceOne(wxCommandEvent& event) {
    (void)event;
    ReplaceOne();
}

void MainFrame::OnReplaceAll(wxCommandEvent& event) {
    (void)event;
    ReplaceAll();
}

void MainFrame::OnToggleTheme(wxCommandEvent& event) {
    (void)event;
    m_dark = !m_dark;
    ApplyTheme();
}

void MainFrame::OnZoomIn(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->ZoomIn();
}

void MainFrame::OnZoomOut(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->ZoomOut();
}

void MainFrame::OnZoomReset(wxCommandEvent& event) {
    (void)event;
    if (EditorWidget* editor = CurrentEditor()) editor->SetZoom(0);
}

void MainFrame::OnRecent(wxCommandEvent& event) {
    const int index = event.GetId() - wxID_FILE1;
    const wxString path = m_fileHistory->GetHistoryFile(index);
    if (!path.empty()) OpenPath(path.utf8_string());
}

void MainFrame::OnAbout(wxCommandEvent& event) {
    (void)event;
    const wxString text = wxString::Format(
        "txt-editor 0.1.0\n\n"
        "A small, fast Linux text editor.\n"
        "C++20, wxWidgets %s, Scintilla/Lexilla.\n\n"
        "UTF-8/UTF-16/ISO-8859-1, LF/CRLF/CR, atomic saves.",
        wxVERSION_STRING);
    wxMessageBox(text, "About txt-editor", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnPageChanged(wxBookCtrlEvent& event) {
    if (!m_suppressPageChanged) {
        const int selection = event.GetSelection();
        if (selection != wxNOT_FOUND) EnsureTabLoaded(selection);
    }
    UpdateTitle();
    UpdateStatus();
    event.Skip();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    std::string sessionError;
    if (!PersistSessionForRestart(&sessionError)) {
        const wxString message =
            "Could not save the session for the next start:\n\n" +
            wxString::FromUTF8(sessionError) +
            "\n\nYou will be asked to save modified tabs instead.";
        wxMessageBox(message, "Session not saved", wxOK | wxICON_WARNING, this);
        if (!MaybeSaveAll()) {
            event.Veto();
            return;
        }
        // Never restore a stale session after the user saved or discarded the
        // tabs explicitly.
        std::string ignored;
        ClearSession(m_sessionDirectory.empty() ? DefaultSessionDirectory()
                                                : m_sessionDirectory,
                     &ignored);
    }
    if (m_fileHistory) {
        if (wxConfigBase* config = wxConfigBase::Get()) {
            m_fileHistory->Save(*config);
            config->Flush();
        }
    }
    event.Skip();
}

}  // namespace txt

