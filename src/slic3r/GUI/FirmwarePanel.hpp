#pragma once

#include <memory>
#include <string>
#include <vector>

#include <boost/optional.hpp>
#include <wx/scrolwin.h>

#include "slic3r/Utils/AvrFlasher.hpp"

class Button;
class ComboBox;
class Label;
class ProgressBar;

namespace Slic3r {
namespace GUI {

class HyperLink;
class FirmwareStepBadge;

// Escaloplab: "Update firmware" on the Printer tab (see PrinterPage). Installs the newest Escaloplab firmware (published as a GitHub release
// tagged "firmware-vX.Y.Z" with one .hex per printer model) on a printer connected over USB.
class FirmwarePanel : public wxScrolledWindow
{
public:
    explicit FirmwarePanel(wxWindow* parent);
    ~FirmwarePanel() override = default;

    // When the tab is opened: looks for the newest firmware (once) and for connected printers
    void on_activate();
    bool is_busy() const { return m_busy; }

    void msw_rescale() { rebuild(); }
    void on_sys_color_changed() { rebuild(); }

private:
    struct Release
    {
        std::string version;  // "1.0.0"
        std::string page_url; // the release page, for "What's new"
        std::vector<std::pair<std::string, std::string>> hex_files; // name, download URL
    };
    struct Port
    {
        std::string port; // COM5, /dev/cu.usbserial-1410
        std::string name; // what the user sees
        bool        printer_usb = false;
    };
    enum class Fetch { NotStarted, Loading, Failed, Done };
    enum class Tone { Normal, Error, Success };

    void rebuild();
    void update_ui();
    void refresh_ports();
    void fetch_latest();
    void check_printer();
    void start_update();
    void install_from_file();
    void flash(Utils::FirmwareImage image, const wxString& what);
    void set_status(const wxString& text, Tone tone);
    std::string hex_url_for_model() const;

    // Background work reaches the panel only while this lives (see run_on_ui)
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);

    // State, kept across rebuilds (theme or DPI changes)
    Fetch                              m_fetch = Fetch::NotStarted;
    boost::optional<Release>           m_release;
    std::vector<std::string>           m_models;
    std::string                        m_model;
    std::vector<Port>                  m_ports;
    std::string                        m_port;
    wxString                           m_printer_text;
    Tone                               m_printer_tone = Tone::Normal;
    bool                               m_checking     = false;
    bool                               m_busy         = false;
    bool                               m_done         = false;
    int                                m_percent      = 0;
    wxString                           m_stage;
    wxString                           m_status;
    Tone                               m_status_tone = Tone::Normal;

    // Widgets
    ComboBox*          m_model_combo   = nullptr;
    Label*             m_latest        = nullptr;
    HyperLink*         m_whats_new     = nullptr;
    ComboBox*          m_port_combo    = nullptr;
    Button*            m_refresh_btn   = nullptr;
    Button*            m_check_btn     = nullptr;
    Label*             m_printer_label = nullptr;
    Button*            m_update_btn    = nullptr;
    ProgressBar*       m_progress      = nullptr;
    Label*             m_stage_label   = nullptr;
    Label*             m_status_label  = nullptr;
    HyperLink*         m_file_link     = nullptr;
    FirmwareStepBadge* m_badges[3]     = {nullptr, nullptr, nullptr};
};

} // namespace GUI
} // namespace Slic3r
