#pragma once

#include <string>
#include <vector>

#include <wx/frame.h>

#include "core/TextTypes.h"
#include "ui/CompareTypes.h"
#include "ui/TabBook.h"

class wxFileHistory;
class wxMenuItem;
class wxUpdateUIEvent;

namespace txt {

class CompareFrame;
class EditorWidget;
class FindPanel;

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

    bool OpenPath(const std::string& path, bool quiet = false);
    void NewDocument();
    void RestoreSession();

private:
    struct TabState {
        std::string id;
        std::string contentFile;
        // Directory that owns contentFile. Empty means this window's own
        // instance directory; a restored tab first points at its source.
        std::string contentDirectory;
        // File identity at load/save time; invalid when it cannot be checked.
        FileStamp stamp;
        std::string loadError;
        bool loaded = true;
        bool recoveredModified = false;
        int anchor = 0;
        int caret = 0;
        int firstVisibleLine = 0;
        int zoom = 0;
    };

    void BuildMenu();
    void BindEditor(EditorWidget* editor);
    EditorWidget* CurrentEditor() const;
    int PageIndex(const EditorWidget* editor) const;
    TabState* StateAt(int index);
    const TabState* StateAt(int index) const;

    void CreatePage(const std::string& utf8, const DocumentMeta& meta,
                    const std::string& path, const FileStamp& stamp);
    void AppendPage(EditorWidget* editor, const wxString& title, TabState state);
    void DeletePage(int index);
    bool EnsureTabLoaded(int index);
    bool ReloadTabFromDisk(int index);
    void CaptureViewState(EditorWidget* editor, TabState& state) const;
    bool EnsureSessionInstance(std::string* error);
    bool PersistSessionForRestart(std::string* error);
    void RemoveRestoredSessions();
    void DiscardSessionState();
    void ShowFindPanel(bool show, bool replace);
    bool MaybeSave(EditorWidget* editor);
    bool MaybeSaveAll();
    bool SaveCurrent(bool saveAs);
    void CloseCurrent();
    void CloseTab(int index);
    void RefreshTab(EditorWidget* editor);
    void UpdateTitle();
    void UpdateStatus();
    void ApplyTheme();
    void ApplyWordWrap();
    void FindNext(bool forward);
    void ReplaceOne();
    void ReplaceAll();

    bool MakeCompareDocument(int index, CompareDocument* document, std::string* error);
    std::vector<CompareDocument> CollectCompareDocuments(
        const std::vector<std::string>& ids, const std::string& baseId,
        std::vector<std::string>* skipped);

    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnCloseTab(wxCommandEvent& event);
    void OnTabCloseRequest(wxBookCtrlEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);
    void OnCut(wxCommandEvent& event);
    void OnCopy(wxCommandEvent& event);
    void OnPaste(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnFind(wxCommandEvent& event);
    void OnReplace(wxCommandEvent& event);
    void OnFindNext(wxCommandEvent& event);
    void OnFindPrev(wxCommandEvent& event);
    void OnFindClose(wxCommandEvent& event);
    void OnReplaceOne(wxCommandEvent& event);
    void OnReplaceAll(wxCommandEvent& event);
    void OnToggleTheme(wxCommandEvent& event);
    void OnToggleWordWrap(wxCommandEvent& event);
    void OnZoomIn(wxCommandEvent& event);
    void OnZoomOut(wxCommandEvent& event);
    void OnZoomReset(wxCommandEvent& event);
    void OnRecent(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnCompareTabs(wxCommandEvent& event);
    void OnUpdateCompareMenu(wxUpdateUIEvent& event);
    void OnCompareRefresh(wxCommandEvent& event);
    void OnPageChanged(wxBookCtrlEvent& event);
    void OnClose(wxCloseEvent& event);

    TabBook* m_notebook = nullptr;
    FindPanel* m_findPanel = nullptr;
    CompareFrame* m_compareFrame = nullptr;
    wxFileHistory* m_fileHistory = nullptr;
    wxMenuItem* m_themeItem = nullptr;
    wxMenuItem* m_wrapItem = nullptr;
    std::vector<EditorWidget*> m_pages;
    std::vector<TabState> m_tabStates;
    std::string m_sessionRoot;
    std::string m_instanceDirectory;
    // Session directories merged into this window at start; removed once their
    // tabs are persisted here.
    std::vector<std::string> m_restoredDirectories;
    bool m_suppressPageChanged = false;
    bool m_dark = false;
    bool m_wordWrap = false;
};

}  // namespace txt
