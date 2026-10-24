#include "app/App.h"

#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/frame.h>

#include "app/Profile.h"
#include "ui/MainFrame.h"

namespace txt {

void App::OnInitCmdLine(wxCmdLineParser& parser) {
    wxApp::OnInitCmdLine(parser);
    parser.AddParam("file", wxCMD_LINE_VAL_STRING,
                    wxCMD_LINE_PARAM_OPTIONAL | wxCMD_LINE_PARAM_MULTIPLE);
}

bool App::OnCmdLineParsed(wxCmdLineParser& parser) {
    if (!wxApp::OnCmdLineParsed(parser)) return false;
    for (std::size_t i = 0; i < parser.GetParamCount(); ++i) {
        m_files.push_back(parser.GetParam(i));
    }
    return true;
}

bool App::OnInit() {
    profile::Mark("OnInit begin");
    if (!wxApp::OnInit()) return false;

    SetAppName("txt-editor");
    SetAppDisplayName("txt-editor");
    SetVendorName("kemenion");
    wxConfigBase::Set(new wxConfig());

    auto* frame = new MainFrame();
    frame->Show();
    profile::Mark("window shown");

    const std::vector<wxString> files = m_files;
    frame->CallAfter([frame, files]() {
        frame->RestoreSession();
        for (const wxString& file : files) {
            frame->OpenPath(file.utf8_string());
        }
        frame->SetFocus();
    });
    return true;
}

int App::OnExit() {
    if (wxConfigBase* config = wxConfigBase::Set(nullptr)) delete config;
    return wxApp::OnExit();
}

}  // namespace txt

wxIMPLEMENT_APP_NO_MAIN(txt::App);
