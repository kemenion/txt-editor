#pragma once

#include <cstddef>

#include <wx/bookctrl.h>
#include <wx/panel.h>
#include <wx/string.h>

class wxSimplebook;

namespace txt {

class TabBar;

// Tab container with an owner-drawn strip and a close button on every tab.
// wxNotebook has no per-tab close button on GTK and AUI is intentionally not
// used, so pages live in a wxSimplebook and the strip is painted with wx.
class TabBook final : public wxPanel {
public:
    explicit TabBook(wxWindow* parent, wxWindowID id = wxID_ANY);

    void AddPage(wxWindow* page, const wxString& title, bool select = true);
    void DeletePage(std::size_t index);

    std::size_t GetPageCount() const;
    wxWindow* GetPage(std::size_t index) const;
    wxWindow* GetCurrentPage() const;

    // Parent for page widgets; pass this to the page constructors.
    wxWindow* PageParent() const;

    int GetSelection() const;
    int SetSelection(std::size_t index);
    int ChangeSelection(std::size_t index);

    bool SetPageText(std::size_t index, const wxString& title);
    wxString GetPageText(std::size_t index) const;

    void SetDarkMode(bool dark);

private:
    friend class TabBar;

    void SelectFromUser(int index);
    void RequestClose(int index);
    void NotifyPageChanged(int oldSelection, int newSelection);

    TabBar* m_tabs = nullptr;
    wxSimplebook* m_book = nullptr;
    int m_selection = -1;
};

// Selection changed by the user or by SetSelection().
wxDECLARE_EVENT(EVT_TAB_CHANGED, wxBookCtrlEvent);
// A tab close button or middle click requested closing one tab.
wxDECLARE_EVENT(EVT_TAB_CLOSE, wxBookCtrlEvent);

}  // namespace txt
