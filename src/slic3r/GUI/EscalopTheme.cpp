#include "EscalopTheme.hpp"

#include <algorithm>
#include <cmath>

#include <boost/filesystem.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/colordlg.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/window.h>

#include "libslic3r/libslic3r.h" // ESCALOP_DATA_KEY
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"

namespace Slic3r { namespace GUI { namespace Escalop {

namespace {

const char* const DEFAULT_HEX = "#FF2E8A";

// <the app's settings folder>/escalop_theme.txt (the same folder as the app config). Found without wx, because
// static initializers ask for the accent before the app starts.
std::string settings_file()
{
#ifdef _WIN32
    const char* base = boost::nowide::getenv("APPDATA");
    return base ? std::string(base) + "\\" ESCALOP_DATA_KEY "\\escalop_theme.txt" : std::string();
#elif defined(__APPLE__)
    const char* home = boost::nowide::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/" ESCALOP_DATA_KEY "/escalop_theme.txt" : std::string();
#else
    const char* xdg  = boost::nowide::getenv("XDG_CONFIG_HOME");
    const char* home = boost::nowide::getenv("HOME");
    if (xdg && *xdg) return std::string(xdg) + "/" ESCALOP_DATA_KEY "/escalop_theme.txt";
    return home ? std::string(home) + "/.config/" ESCALOP_DATA_KEY "/escalop_theme.txt" : std::string();
#endif
}

bool parse_hex(const std::string& s, unsigned char rgb[3])
{
    if (s.size() != 7 || s[0] != '#') return false;
    for (int i = 0; i < 3; ++i) {
        unsigned v = 0;
        for (int k = 1; k <= 2; ++k) {
            const char c = s[2 * i + k];
            v *= 16;
            if (c >= '0' && c <= '9') v += unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v += unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v += unsigned(c - 'A' + 10);
            else return false;
        }
        rgb[i] = (unsigned char) v;
    }
    return true;
}

std::string to_hex(const wxColour& c)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.Red(), c.Green(), c.Blue());
    return buf;
}

wxColour mix(const wxColour& c, const wxColour& with, double t)
{
    auto m = [t](unsigned char a, unsigned char b) { return (unsigned char) std::lround(a + (b - a) * t); };
    return wxColour(m(c.Red(), with.Red()), m(c.Green(), with.Green()), m(c.Blue(), with.Blue()));
}

struct Colors
{
    wxColour accent, hover, dark, pale;
};

const Colors& colors()
{
    static const Colors c = [] {
        std::string hex;
        const std::string file = settings_file();
        if (!file.empty()) {
            boost::nowide::ifstream in(file);
            if (in) std::getline(in, hex);
            while (!hex.empty() && (hex.back() == '\r' || hex.back() == ' ')) hex.pop_back();
        }
        unsigned char rgb[3];
        Colors r;
        if (!parse_hex(hex, rgb) || to_hex(wxColour(rgb[0], rgb[1], rgb[2])) == DEFAULT_HEX) {
            // Escaloplab pink with its hand-picked shades
            r.accent = wxColour(0xFF, 0x2E, 0x8A);
            r.hover  = wxColour(0xFF, 0x5C, 0xA3);
            r.dark   = wxColour(0xC7, 0x0F, 0x62);
            r.pale   = wxColour(0xFF, 0xB8, 0xD6);
        } else {
            r.accent = wxColour(rgb[0], rgb[1], rgb[2]);
            r.hover  = mix(r.accent, wxColour(255, 255, 255), 0.25);
            r.dark   = mix(r.accent, wxColour(0, 0, 0), 0.22);
            r.pale   = mix(r.accent, wxColour(255, 255, 255), 0.65);
        }
        return r;
    }();
    return c;
}

bool save(const wxColour& c)
{
    const std::string file = settings_file();
    if (file.empty()) return false;
    boost::system::error_code ec;
    boost::filesystem::create_directories(boost::filesystem::path(file).parent_path(), ec);
    boost::nowide::ofstream out(file, std::ios::trunc);
    out << to_hex(c) << "\n";
    return bool(out);
}

// A filled circle with soft edges, for the menu
wxBitmap swatch(const wxColour& c, int px)
{
    wxImage img(px, px);
    img.InitAlpha();
    const double r = px / 2.0 - 0.5;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const double d = std::hypot(x + 0.5 - px / 2.0, y + 0.5 - px / 2.0);
            img.SetRGB(x, y, c.Red(), c.Green(), c.Blue());
            img.SetAlpha(x, y, (unsigned char) std::lround(255.0 * std::clamp(r - d + 0.5, 0.0, 1.0)));
        }
    return wxBitmap(img);
}

} // namespace

const wxColour& accent() { return colors().accent; }
const wxColour& accent_hover() { return colors().hover; }
const wxColour& accent_dark() { return colors().dark; }
const wxColour& accent_pale() { return colors().pale; }

std::string accent_hex() { return to_hex(accent()); }
std::string accent_hover_hex() { return to_hex(accent_hover()); }
std::string accent_dark_hex() { return to_hex(accent_dark()); }
std::string accent_pale_hex() { return to_hex(accent_pale()); }

wxBitmap circle_bitmap(int px) { return swatch(accent(), px); }

std::vector<std::pair<std::string, wxString>> palette()
{
    return {
        {"#FF2E8A", _L("Escaloplab pink")},
        {"#2A9DF4", _L("Blue")},
        {"#22B573", _L("Green")},
        {"#00A79D", _L("Teal")},
        {"#8E5CFF", _L("Purple")},
        {"#E5383B", _L("Red")},
        {"#FF8A00", _L("Orange")},
        {"#F2B705", _L("Yellow")},
    };
}

void choose_accent(wxWindow* parent)
{
    if (!parent) return;
    wxMenu      menu;
    const auto  colors_list = palette();
    const int   first_id    = wxWindow::NewControlId(int(colors_list.size()) + 1);
    const int   custom_id   = first_id + int(colors_list.size());
    const int   px          = parent->FromDIP(16);
    for (size_t i = 0; i < colors_list.size(); ++i) {
        unsigned char rgb[3];
        parse_hex(colors_list[i].first, rgb);
        const wxColour c(rgb[0], rgb[1], rgb[2]);
        const bool     current = to_hex(c) == accent_hex();
        auto*          item    = new wxMenuItem(&menu, first_id + int(i), colors_list[i].second + (current ? wxString("  ✓") : wxString()));
        item->SetBitmap(swatch(c, px));
        menu.Append(item);
    }
    menu.AppendSeparator();
    menu.Append(custom_id, _L("Custom color..."));

    const int sel = parent->GetPopupMenuSelectionFromUser(menu);
    if (sel == wxID_NONE) return;
    wxColour chosen;
    if (sel == custom_id) {
        wxColourData data;
        data.SetColour(accent());
        data.SetChooseFull(true);
        wxColourDialog dlg(parent, &data);
        dlg.SetTitle(_L("Theme color"));
        if (dlg.ShowModal() != wxID_OK) return;
        chosen = dlg.GetColourData().GetColour();
    } else if (sel >= first_id && sel < custom_id) {
        unsigned char rgb[3];
        parse_hex(colors_list[size_t(sel - first_id)].first, rgb);
        chosen = wxColour(rgb[0], rgb[1], rgb[2]);
    } else {
        return;
    }
    if (!chosen.IsOk() || to_hex(chosen) == accent_hex()) return;
    if (!save(chosen)) {
        show_error(parent, _L("The theme color could not be saved."));
        return;
    }
    MessageDialog dlg(parent, _L("Escalop Workshop restarts to use the new color. Your project is kept if you save it when asked."),
                      _L("Theme color"), wxOK | wxCANCEL | wxICON_INFORMATION);
    dlg.SetButtonLabel(wxID_OK, _L("Restart now"));
    dlg.SetButtonLabel(wxID_CANCEL, _L("Later"));
    if (dlg.ShowModal() != wxID_OK) return;
    request_restart();
}

namespace {
bool s_restart = false;
}

void request_restart()
{
    s_restart = true;
    if (wxGetApp().mainframe && !wxGetApp().mainframe->Close())
        s_restart = false; // the user cancelled closing (unsaved project)
}

bool restart_requested() { return s_restart; }

}}} // namespace Slic3r::GUI::Escalop
