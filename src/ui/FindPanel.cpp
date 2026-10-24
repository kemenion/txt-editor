#include "ui/FindPanel.h"

#include <wx/sizer.h>

namespace txt {

FindPanel::FindPanel(wxWindow* parent, wxWindowID id) : wxPanel(parent, id) {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* findRow = new wxBoxSizer(wxHORIZONTAL);
    findRow->Add(new wxStaticText(this, wxID_ANY, "Find:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_find = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(320, -1), wxTE_PROCESS_ENTER);
    findRow->Add(m_find, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_matchCase = new wxCheckBox(this, wxID_ANY, "Match case");
    m_wholeWord = new wxCheckBox(this, wxID_ANY, "Whole word");
    findRow->Add(m_matchCase, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    findRow->Add(m_wholeWord, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    findRow->Add(new wxButton(this, ID_FIND_PREV, "Previous"), 0, wxRIGHT, 4);
    findRow->Add(new wxButton(this, ID_FIND_NEXT, "Next"), 0, wxRIGHT, 4);
    findRow->Add(new wxButton(this, ID_CLOSE, "Close"), 0);

    auto* replaceRow = new wxBoxSizer(wxHORIZONTAL);
    replaceRow->Add(new wxStaticText(this, wxID_ANY, "Replace:"), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_replace = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxSize(320, -1), wxTE_PROCESS_ENTER);
    replaceRow->Add(m_replace, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_replaceOne = new wxButton(this, ID_REPLACE_ONE, "Replace");
    m_replaceAll = new wxButton(this, ID_REPLACE_ALL, "Replace All");
    replaceRow->Add(m_replaceOne, 0, wxRIGHT, 4);
    replaceRow->Add(m_replaceAll, 0);
    m_replaceRow = replaceRow;

    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    wxFont statusFont = m_status->GetFont();
    statusFont.SetPointSize(9);
    m_status->SetFont(statusFont);

    outer->Add(findRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    outer->Add(replaceRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    outer->Add(m_status, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 6);
    SetSizer(outer);

    replaceRow->Show(false);
    Layout();

    m_find->Bind(wxEVT_TEXT_ENTER, &FindPanel::OnFindEnter, this);
    m_find->Bind(wxEVT_KEY_DOWN, &FindPanel::OnFindKeyDown, this);
    m_replace->Bind(wxEVT_TEXT_ENTER, &FindPanel::OnFindEnter, this);
    m_replace->Bind(wxEVT_KEY_DOWN, &FindPanel::OnFindKeyDown, this);
}

void FindPanel::Send(int id) {
    wxCommandEvent event(wxEVT_BUTTON, id);
    event.SetEventObject(this);
    if (GetParent()) GetParent()->GetEventHandler()->ProcessEvent(event);
}

void FindPanel::OnFindEnter(wxCommandEvent& event) {
    (void)event;
    Send(wxGetKeyState(WXK_SHIFT) ? ID_FIND_PREV : ID_FIND_NEXT);
}

void FindPanel::OnFindKeyDown(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_ESCAPE) {
        Send(ID_CLOSE);
        return;
    }
    event.Skip();
}

void FindPanel::ShowReplace(bool show) {
    m_replaceRow->Show(show);
    Layout();
    if (show) {
        m_replace->SetFocus();
        m_replace->SelectAll();
    }
}

void FindPanel::FocusFind() {
    m_find->SetFocus();
    m_find->SelectAll();
}

}  // namespace txt
