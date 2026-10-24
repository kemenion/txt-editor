#pragma once

#include <vector>

#include <wx/app.h>
#include <wx/string.h>

namespace txt {

class App final : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;

    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    std::vector<wxString> m_files;
};

}  // namespace txt
