#pragma once

#include <wx/string.h>

class wxWindow;

namespace txt {

enum class SaveDecision {
    Save,
    Discard,
    Cancel,
};

// Shows the three-way "save before closing" prompt for one tab. The dialog
// uses standard button placement and makes Cancel the safe exit path.
SaveDecision ShowSaveChangesPrompt(wxWindow* parent, const wxString& displayName,
                                   const wxString& path);

// Shows the warning used when a recovered tab cannot be loaded. It offers only
// discarding the recovered contents or cancelling the close.
bool ConfirmDiscardUnloadedTab(wxWindow* parent, const wxString& displayName,
                               const wxString& details);

}  // namespace txt
