#pragma once

// Escaloplab: the app's accent color ("theme color"), Escaloplab neon pink #FF2E8A unless the user picked another
// one with the color circle in the top bar. It is read once at start-up (also from static initializers, so it
// comes from a small file of its own, not from the app config), and changing it restarts the app.

#include <string>
#include <utility>
#include <vector>

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/string.h>

class wxWindow;

namespace Slic3r { namespace GUI { namespace Escalop {

const wxColour& accent();       // buttons, selection, links (was #FF2E8A)
const wxColour& accent_hover(); // lighter, for hover (was #FF5CA3)
const wxColour& accent_dark();  // darker, for dark mode and pressed (was #C70F62)
const wxColour& accent_pale();  // very light, for disabled accent buttons (was #FFB8D6)

std::string accent_hex();       // "#RRGGBB"
std::string accent_hover_hex();
std::string accent_dark_hex();
std::string accent_pale_hex();

// The colors offered by the color circle: (hex, name)
std::vector<std::pair<std::string, wxString>> palette();

// A filled circle in the accent color (the top bar's color button)
wxBitmap circle_bitmap(int px);

// Opens the color menu at the mouse; after a choice asks to restart the app
void choose_accent(wxWindow* parent);

// Closes the app and starts it again (GUI_App::OnExit starts the new one)
void request_restart();
bool restart_requested();

}}} // namespace Slic3r::GUI::Escalop

#define ESCALOP_ACCENT          (Slic3r::GUI::Escalop::accent())
#define ESCALOP_ACCENT_HOVER    (Slic3r::GUI::Escalop::accent_hover())
#define ESCALOP_ACCENT_DARK     (Slic3r::GUI::Escalop::accent_dark())
#define ESCALOP_ACCENT_PALE     (Slic3r::GUI::Escalop::accent_pale())
#define ESCALOP_ACCENT_HEX      (Slic3r::GUI::Escalop::accent_hex())
#define ESCALOP_ACCENT_HOVER_HEX (Slic3r::GUI::Escalop::accent_hover_hex())
#define ESCALOP_ACCENT_DARK_HEX (Slic3r::GUI::Escalop::accent_dark_hex())
#define ESCALOP_ACCENT_PALE_HEX (Slic3r::GUI::Escalop::accent_pale_hex())
