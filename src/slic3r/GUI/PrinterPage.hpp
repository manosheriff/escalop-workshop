#pragma once

#include <wx/panel.h>

class Button;
class Label;
class wxSimplebook;

namespace Slic3r {
namespace GUI {

class FirmwarePanel;
class PrinterWebView;

// Escaloplab: the "Printer" tab, Orca's "Device" tab for printers that aren't Bambu Lab's. It has two sections:
// updating the printer's firmware over USB (FirmwarePanel) and controlling a printer on the network through its
// own web page (Orca's PrinterWebView: OctoPrint, Klipper, PrusaLink...).
class PrinterPage : public wxPanel
{
public:
    enum class Section { Firmware, Control };

    // Without firmware (G-code viewer) the page is only the printer's web page
    PrinterPage(wxWindow* parent, bool with_firmware);

    PrinterWebView* web_view() const { return m_web_view; }
    FirmwarePanel*  firmware() const { return m_firmware; }

    void show_section(Section section);
    // A printer address was set: show its web page, unless the user is updating the firmware right now
    void on_printer_url_loaded();
    // When the tab is opened
    void on_activate();
    void msw_rescale();
    void on_sys_color_changed();

private:
    void apply_colors();
    void update_hint();

    Section         m_section      = Section::Control;
    wxPanel*        m_header       = nullptr;
    Label*          m_title        = nullptr;
    Label*          m_intro        = nullptr;
    Label*          m_hint         = nullptr;
    Button*         m_firmware_btn = nullptr;
    Button*         m_control_btn  = nullptr;
    wxSimplebook*   m_book         = nullptr;
    FirmwarePanel*  m_firmware     = nullptr;
    PrinterWebView* m_web_view     = nullptr;
};

} // namespace GUI
} // namespace Slic3r
