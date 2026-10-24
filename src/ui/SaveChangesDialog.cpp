#include "ui/SaveChangesDialog.h"

#include <cstddef>

#include <wx/arrstr.h>
#include <wx/artprov.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/control.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>

namespace txt {
namespace {

constexpr int kTextColumnDip = 400;
constexpr int kMinDialogWidthDip = 520;
constexpr int kIconSizeDip = 48;

wxString EllipsizeLines(const wxString& text, const wxFont& font, int width) {
    wxBitmap bitmap(1, 1);
    wxMemoryDC dc(bitmap);
    dc.SetFont(font);

    const wxArrayString lines = wxSplit(text, '\n', '\0');
    wxString result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) result += '\n';
        result += wxControl::Ellipsize(lines[i], dc, wxELLIPSIZE_MIDDLE, width,
                                       wxELLIPSIZE_FLAGS_NONE);
    }
    return result;
}

class SavePromptDialog final : public wxDialog {
public:
    SavePromptDialog(wxWindow* parent, const wxString& title, const wxString& heading,
                     const wxString& body, const wxString& detail, bool untitled,
                     bool canSave);

private:
    void BuildLayout(const wxString& heading, const wxString& body,
                     const wxString& detail, bool untitled, bool canSave);
};

SavePromptDialog::SavePromptDialog(wxWindow* parent, const wxString& title,
                                   const wxString& heading, const wxString& body,
                                   const wxString& detail, bool untitled, bool canSave)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE) {
    BuildLayout(heading, body, detail, untitled, canSave);
}

void SavePromptDialog::BuildLayout(const wxString& heading, const wxString& body,
                                   const wxString& detail, bool untitled,
                                   bool canSave) {
    const int border = FromDIP(20);
    const int textWidth = FromDIP(kTextColumnDip);
    const int gapSmall = FromDIP(8);
    const int gapMedium = FromDIP(12);

    auto* icon = new wxStaticBitmap(
        this, wxID_ANY,
        wxArtProvider::GetBitmap(wxART_WARNING, wxART_MESSAGE_BOX,
                                 FromDIP(wxSize(kIconSizeDip, kIconSizeDip))));

    auto* headingText = new wxStaticText(this, wxID_ANY, heading);
    wxFont headingFont = headingText->GetFont();
    headingFont.SetPointSize(headingFont.GetPointSize() + 2);
    headingFont.SetWeight(wxFONTWEIGHT_BOLD);
    headingText->SetFont(headingFont);
    headingText->Wrap(textWidth);

    auto* bodyText = new wxStaticText(this, wxID_ANY, body);
    bodyText->Wrap(textWidth);

    auto* textColumn = new wxBoxSizer(wxVERTICAL);
    textColumn->Add(headingText, 0, wxBOTTOM, gapSmall);
    textColumn->Add(bodyText, 0, wxBOTTOM, detail.empty() ? 0 : gapMedium);

    if (!detail.empty()) {
        const wxString shown = EllipsizeLines(detail, bodyText->GetFont(), textWidth);
        auto* detailText = new wxStaticText(this, wxID_ANY, shown);
        detailText->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        detailText->SetMinSize(wxSize(textWidth, -1));
        detailText->SetMaxSize(wxSize(textWidth, -1));
        detailText->SetToolTip(detail);
        textColumn->Add(detailText, 0, wxEXPAND);
    }

    auto* content = new wxBoxSizer(wxHORIZONTAL);
    content->Add(icon, 0, wxALIGN_TOP | wxRIGHT, FromDIP(16));
    content->Add(textColumn, 1, wxEXPAND);

    auto* buttons = new wxStdDialogButtonSizer;
    wxButton* saveButton = nullptr;
    if (canSave) {
        saveButton =
            new wxButton(this, wxID_SAVE, untitled ? "Save &As..." : "&Save");
        buttons->AddButton(saveButton);
        saveButton->Bind(wxEVT_BUTTON,
                         [this](wxCommandEvent&) { EndModal(wxID_SAVE); });
    }
    auto* discardButton =
        new wxButton(this, wxID_NO, canSave ? "Do&n't Save" : "&Discard Changes");
    auto* cancelButton = new wxButton(this, wxID_CANCEL, "&Cancel");
    buttons->AddButton(discardButton);
    buttons->AddButton(cancelButton);
    buttons->Realize();

    discardButton->Bind(wxEVT_BUTTON,
                        [this](wxCommandEvent&) { EndModal(wxID_NO); });
    cancelButton->Bind(wxEVT_BUTTON,
                       [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    Bind(wxEVT_CLOSE_WINDOW,
         [this](wxCloseEvent&) { EndModal(wxID_CANCEL); });

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(content, 0, wxEXPAND | wxALL, border);
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, border);
    outer->Add(buttons, 0, wxEXPAND | wxALL, border);
    SetSizerAndFit(outer);

    const int minWidth = FromDIP(kMinDialogWidthDip);
    if (GetSize().x < minWidth) SetSize(wxSize(minWidth, GetSize().y));
    SetMinSize(wxSize(minWidth, GetSize().y));

    SetEscapeId(wxID_CANCEL);
    if (saveButton != nullptr) {
        SetAffirmativeId(wxID_SAVE);
        saveButton->SetDefault();
        saveButton->SetFocus();
    } else {
        SetAffirmativeId(wxID_CANCEL);
        cancelButton->SetDefault();
        cancelButton->SetFocus();
    }
    CentreOnParent();
}

}  // namespace

SaveDecision ShowSaveChangesPrompt(wxWindow* parent, const wxString& displayName,
                                   const wxString& path) {
    const wxString heading =
        wxS("Save changes to \u201C") + displayName + wxS("\u201D?");
    const wxString body =
        wxS("Your changes will be lost if you close this tab without saving them.");
    const wxString detail =
        path.empty() ? wxS("This tab has not been saved to disk yet.") : path;

    SavePromptDialog dialog(parent, wxS("Save Changes"), heading, body, detail,
                            path.empty(), true);
    const int result = dialog.ShowModal();
    if (result == wxID_SAVE) return SaveDecision::Save;
    if (result == wxID_NO) return SaveDecision::Discard;
    return SaveDecision::Cancel;
}

bool ConfirmDiscardUnloadedTab(wxWindow* parent, const wxString& displayName,
                               const wxString& details) {
    const wxString heading =
        wxS("Could not restore \u201C") + displayName + wxS("\u201D");
    const wxString body =
        wxS("The recovery snapshot for this tab could not be loaded. Closing the "
            "tab will discard the recovered changes.");
    SavePromptDialog dialog(parent, wxS("Discard Recovered Changes"), heading, body,
                            details, false, false);
    return dialog.ShowModal() == wxID_NO;
}

}  // namespace txt

