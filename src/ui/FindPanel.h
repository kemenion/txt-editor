#pragma once

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/stattext.h>

class wxSizer;

namespace txt {

class FindPanel final : public wxPanel {
public:
    enum : int {
        ID_FIND_NEXT = wxID_HIGHEST + 100,
        ID_FIND_PREV,
        ID_REPLACE_ONE,
        ID_REPLACE_ALL,
        ID_CLOSE,
    };

    explicit FindPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

    wxString Query() const { return m_find->GetValue(); }
    wxString Replacement() const { return m_replace->GetValue(); }
    bool MatchCase() const { return m_matchCase->GetValue(); }
    bool WholeWord() const { return m_wholeWord->GetValue(); }

    void ShowReplace(bool show);
    void FocusFind();
    void SetStatus(const wxString& text) { m_status->SetLabel(text); }

private:
    void Send(int id);
    void OnFindEnter(wxCommandEvent& event);
    void OnFindKeyDown(wxKeyEvent& event);

    wxTextCtrl* m_find = nullptr;
    wxTextCtrl* m_replace = nullptr;
    wxCheckBox* m_matchCase = nullptr;
    wxCheckBox* m_wholeWord = nullptr;
    wxButton* m_replaceOne = nullptr;
    wxButton* m_replaceAll = nullptr;
    wxSizer* m_replaceRow = nullptr;
    wxStaticText* m_status = nullptr;
};

}  // namespace txt
