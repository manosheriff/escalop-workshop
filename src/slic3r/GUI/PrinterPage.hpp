#pragma once

#include <memory>
#include <string>
#include <vector>

#include <wx/panel.h>

class Button;
class Label;
class TextInput;
class wxSimplebook;
class wxBoxSizer;
class wxScrolledWindow;

namespace Slic3r {

class Bonjour;

namespace GUI {

class FirmwarePanel;
class PrinterWebView;

// Escaloplab: the "Printer" tab (Orca's "Device" tab for printers that aren't Bambu Lab's). Shows the printer's own
// web page (the Escaloplab Wi-Fi module: status, control, files, firmware updates). Without a printer address, or when
// the printer can't be reached, it looks for Escaloplab printers on the network and lets the user pick one by its
// serial number, or type the address shown on the printer. Printers without the Wi-Fi module can get their firmware
// over USB (FirmwarePanel), from a link on that page.
class PrinterPage : public wxPanel
{
public:
    // Without firmware (G-code viewer) there's no USB update link
    PrinterPage(wxWindow* parent, bool with_firmware);
    ~PrinterPage() override;

    PrinterWebView* web_view() const { return m_web_view; }

    // Puts the connected printer's address (app config "escalop_printer_host") into every Escaloplab printer preset in
    // memory, the built-in ones too: those can't be saved, and the printer should work with them, not only with a
    // "- Copy". User presets keep an address of their own (anything but `previous`, the address before this one).
    // Called whenever presets are (re)loaded and when a printer is connected.
    static void apply_saved_host(const std::string& previous = {});
    FirmwarePanel*  firmware() const { return m_firmware; }

    // The printer's address changed (MainFrame's EVT_LOAD_PRINTER_URL): show its page, or look for printers
    void load(const wxString& url, const wxString& apikey);
    // When the tab is opened
    void on_activate();
    void msw_rescale();
    void on_sys_color_changed();

private:
    struct Found
    {
        std::string serial; // "00003"
        std::string host;   // IP address
    };
    enum class Section { Find, Web, Usb };

    void show(Section section);
    void build_find_page();
    void refresh_find_page();
    void start_search();
    void connect(const std::string& host);
    void on_page_error();

    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    Section              m_section = Section::Find;
    wxSimplebook*        m_book     = nullptr;
    PrinterWebView*      m_web_view = nullptr;
    FirmwarePanel*       m_firmware = nullptr;
    wxPanel*             m_usb_page = nullptr;

    // The "find your printer" page
    wxScrolledWindow* m_find      = nullptr;
    Label*            m_title     = nullptr;
    Label*            m_status    = nullptr;
    wxBoxSizer*       m_list      = nullptr;
    wxPanel*          m_manual    = nullptr;
    TextInput*        m_ip_input  = nullptr;
    Button*           m_again_btn = nullptr;

    std::shared_ptr<Bonjour> m_bonjour;
    bool                     m_searching = false;
    std::vector<Found>       m_found;
    wxString                 m_problem;  // why the page is shown ("can't reach ...")
    wxString                 m_url;      // the printer's page, as last loaded
};

} // namespace GUI
} // namespace Slic3r
