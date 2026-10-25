#include <cstddef>
#include <string>
#include <vector>

#include <wx/init.h>
#include <wx/strconv.h>
#include <wx/string.h>

#include "app/Profile.h"

int main(int argc, char** argv) {
    txt::profile::Mark("process main");

    // The narrow wxEntry() overload decodes argv with wxConvLocal.  This wx
    // build has wxUSE_INTL disabled, so wxConvLocal is ISO-8859-1 and a UTF-8
    // path such as "中文.txt" would become "ä¸­æ–‡.txt" before wxApp sees it.
    // Decode the raw bytes ourselves (UTF-8 first, as wx does elsewhere) and
    // use the wide-character entry point instead.
    std::vector<std::wstring> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        arguments.push_back(wxString(argv[i], wxConvWhateverWorks).ToStdWstring());
    }

    std::vector<wxChar*> wideArgv;
    wideArgv.reserve(arguments.size() + 1);
    for (std::wstring& argument : arguments) wideArgv.push_back(argument.data());
    wideArgv.push_back(nullptr);

    int wideArgc = argc;
    return wxEntry(wideArgc, wideArgv.data());
}
