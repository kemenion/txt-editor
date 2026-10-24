#include <wx/init.h>

#include "app/Profile.h"

int main(int argc, char** argv) {
    txt::profile::Mark("process main");
    return wxEntry(argc, argv);
}
