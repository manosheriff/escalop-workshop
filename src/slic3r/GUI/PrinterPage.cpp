#include "PrinterPage.hpp"

#include <algorithm>

#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>

#include <wx/notifmsg.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>

#include "EscalopTheme.hpp"
#include "FirmwarePanel.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "PrinterWebView.hpp"
#include "Tab.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/HyperLink.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/Bonjour.hpp"
#include "slic3r/Utils/Http.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "format.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r {
namespace GUI {

namespace {
wxColour themed(const char* light) { return StateColor::darkModeColorFor(wxColour(light)); }

// The Wi-Fi module's network name is escalop-v80-<serial> (its setup hotspot: Escalop-v80-<serial>)
std::string serial_from(const std::string& name)
{
    std::string n = boost::algorithm::to_lower_copy(name);
    const std::string prefix = "escalop-v80-";
    const size_t      at     = n.find(prefix);
    if (at == std::string::npos) return {};
    std::string serial = name.substr(at + prefix.size());
    const size_t end = serial.find_first_of(". ");
    if (end != std::string::npos) serial = serial.substr(0, end);
    return boost::algorithm::to_upper_copy(serial);
}

// The page in the theme color (the Wi-Fi module's page reads ?accent=RRGGBB)
wxString with_accent(const wxString& url)
{
    wxString hex = from_u8(ESCALOP_ACCENT_HEX).Mid(1);
    return url + (url.Contains("?") ? "&" : "?") + "embed=1&accent=" + hex;
}
} // namespace

PrinterPage::PrinterPage(wxWindow* parent, bool with_firmware) : wxPanel(parent, wxID_ANY)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    m_book      = new wxSimplebook(this, wxID_ANY);

    build_find_page(); // page 0

    m_web_view = new PrinterWebView(m_book); // page 1
    m_book->AddPage(m_web_view, wxEmptyString);
    std::weak_ptr<int> alive = m_alive;
    m_web_view->set_on_load_error([this, alive]() {
        if (alive.lock()) on_page_error();
    });

    if (with_firmware) { // page 2: firmware over USB, for printers without the Wi-Fi module
        m_usb_page   = new wxPanel(m_book);
        auto* us     = new wxBoxSizer(wxVERTICAL);
        auto* back   = new HyperLink(m_usb_page, _L("< Back"));
        back->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { show(Section::Find); });
        us->Add(back, 0, wxALL, FromDIP(16));
        m_firmware = new FirmwarePanel(m_usb_page);
        us->Add(m_firmware, 1, wxEXPAND);
        m_usb_page->SetSizer(us);
        m_book->AddPage(m_usb_page, wxEmptyString);
    }

    sizer->Add(m_book, 1, wxEXPAND);
    SetSizer(sizer);
    show(Section::Find);

    if (with_firmware) { // not in the G-code viewer
        m_watch.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { watch_printer(); }, m_watch.GetId());
        m_watch.Start(15000);
    }
}

PrinterPage::~PrinterPage() { m_watch.Stop(); }

void PrinterPage::watch_printer()
{
    if (m_watching || !wxGetApp().app_config)
        return;
    const std::string host = wxGetApp().app_config->get("escalop_printer_host");
    if (host.empty())
        return;
    std::string url = boost::starts_with(host, "http://") || boost::starts_with(host, "https://") ? host : "http://" + host;
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    m_watching               = true;
    std::weak_ptr<int> alive = m_alive;
    Http::get(url + "/api/status")
        .timeout_connect(3)
        .timeout_max(6)
        .on_complete([this, alive, host](std::string body, unsigned) {
            long        done = -1;
            std::string file;
            try {
                const auto json = nlohmann::json::parse(body);
                const auto p    = json.value("printer", nlohmann::json::object());
                if (p.contains("done") && p["done"].is_number_integer())
                    done = p["done"].get<long>();
                if (p.contains("file") && p["file"].is_string())
                    file = p["file"].get<std::string>();
            } catch (const std::exception&) {}
            wxGetApp().CallAfter([this, alive, host, done, file]() {
                if (!alive.lock())
                    return;
                m_watching = false;
                on_printer_status(host, done, file);
            });
        })
        .on_error([this, alive](std::string, std::string, unsigned) {
            wxGetApp().CallAfter([this, alive]() {
                if (alive.lock())
                    m_watching = false;
            });
        })
        .perform();
}

void PrinterPage::on_printer_status(const std::string& host, long done, const std::string& file)
{
    if (done < 0) // a Wi-Fi module before 1.0.21 doesn't count
        return;
    // First answer, another printer, or the module restarted (its count starts again at 0): only remember the count
    if (m_done_seen < 0 || host != m_done_host || done < m_done_seen) {
        m_done_seen = done;
        m_done_host = host;
        return;
    }
    if (done == m_done_seen)
        return;
    m_done_seen = done;

    const wxString title = _L("Print finished");
    const wxString text  = file.empty() ? _L("Your Escaloplab printer finished printing.") :
                                          format_wxstr(_L("Your Escaloplab printer finished %1%."), from_u8(file));
    wxNotificationMessage note(title, text, wxGetApp().mainframe);
    note.Show(wxNotificationMessage::Timeout_Auto);
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->RequestUserAttention(wxUSER_ATTENTION_INFO);
    if (Plater* plater = wxGetApp().plater())
        if (NotificationManager* nm = plater->get_notification_manager())
            nm->push_notification(into_u8(text)); // a regular notification that fades after 10 s
}

void PrinterPage::build_find_page()
{
    m_find = new wxScrolledWindow(m_book, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_find->SetScrollRate(0, FromDIP(10));
    m_book->AddPage(m_find, wxEmptyString);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* col   = new wxBoxSizer(wxVERTICAL);

    m_title = new Label(m_find, Label::Head_20, _L("Connect your printer"));
    col->Add(m_title, 0, wxBOTTOM, FromDIP(6));
    m_status = new Label(m_find, Label::Body_14, wxEmptyString);
    col->Add(m_status, 0, wxBOTTOM, FromDIP(16));

    m_list = new wxBoxSizer(wxVERTICAL);
    col->Add(m_list, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_again_btn = new Button(m_find, _L("Search again"));
    m_again_btn->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
    m_again_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { start_search(); });
    col->Add(m_again_btn, 0, wxBOTTOM, FromDIP(24));

    // Typing the address by hand
    m_manual  = new wxPanel(m_find);
    auto* ms  = new wxBoxSizer(wxVERTICAL);
    auto* tip = new Label(m_manual, Label::Body_14,
                          _L("Your printer must be on the same Wi-Fi network as this computer. If it is, look up its "
                             "address on the printer (Menu > Wi-Fi > IP) and type it here:"));
    tip->Wrap(FromDIP(560));
    ms->Add(tip, 0, wxBOTTOM, FromDIP(10));
    auto* row  = new wxBoxSizer(wxHORIZONTAL);
    m_ip_input = new TextInput(m_manual, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(220), -1));
    m_ip_input->GetTextCtrl()->SetHint("192.168.1.57");
    row->Add(m_ip_input, 0, wxALIGN_CENTER_VERTICAL);
    auto* go = new Button(m_manual, _L("Connect"));
    go->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
    auto connect_typed = [this]() {
        std::string host = into_u8(m_ip_input->GetTextCtrl()->GetValue());
        boost::algorithm::trim(host);
        if (boost::istarts_with(host, "http://")) host = host.substr(7);
        while (!host.empty() && host.back() == '/') host.pop_back();
        if (!host.empty()) connect(host);
    };
    go->Bind(wxEVT_BUTTON, [connect_typed](wxCommandEvent&) { connect_typed(); });
    m_ip_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [connect_typed](wxCommandEvent&) { connect_typed(); });
    row->Add(go, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    ms->Add(row);
    m_manual->SetSizer(ms);
    col->Add(m_manual, 0, wxEXPAND | wxBOTTOM, FromDIP(32));

    // Printers without the Wi-Fi module: firmware over USB
    if (wxGetApp().is_editor()) {
        auto* usb = new HyperLink(m_find, _L("Printer connected with a USB cable? Update its firmware over USB"));
        usb->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { if (m_usb_page) show(Section::Usb); });
        col->Add(usb);
    }

    outer->Add(col, 0, wxALL, FromDIP(40));
    m_find->SetSizer(outer);
    refresh_find_page();
}

void PrinterPage::refresh_find_page()
{
    const wxColour bg = themed("#F8F8F8");
    m_find->SetBackgroundColour(bg);
    m_manual->SetBackgroundColour(bg);
    m_title->SetForegroundColour(themed("#262E30"));
    m_status->SetForegroundColour(themed("#6B6B6B"));
    for (wxWindow* w : m_manual->GetChildren())
        if (auto* l = dynamic_cast<Label*>(w)) l->SetForegroundColour(themed("#6B6B6B"));

    wxString status;
    if (m_searching)
        status = _L("Looking for Escaloplab printers on your network...");
    else if (!m_found.empty())
        status = m_found.size() == 1 ? _L("Found your printer. Click it to connect:") : _L("Choose your printer by its serial number:");
    else
        status = _L("No printer found on this network.");
    if (!m_problem.IsEmpty()) status = m_problem + "\n" + status;
    m_status->SetLabel(status);
    m_status->Wrap(FromDIP(560));

    m_list->Clear(true);
    for (const Found& f : m_found) {
        wxString label = wxString::Format(_L("Escaloplab v80  ·  Serial %s"), from_u8(f.serial));
        auto*    btn   = new Button(m_find, label + "      " + from_u8(f.host));
        btn->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
        btn->SetMinSize(wxSize(FromDIP(420), FromDIP(40)));
        const std::string host = f.host;
        btn->Bind(wxEVT_BUTTON, [this, host](wxCommandEvent&) { connect(host); });
        m_list->Add(btn, 0, wxBOTTOM, FromDIP(8));
    }
    m_again_btn->Enable(!m_searching);
    m_manual->Show(!m_searching && m_found.empty());
    m_find->Layout();
    m_find->FitInside();
    m_find->Refresh();
}

void PrinterPage::start_search()
{
    if (m_searching) return;
    m_searching = true;
    m_found.clear();
    refresh_find_page();

    std::weak_ptr<int> alive = m_alive;
    // The Wi-Fi module announces itself like OctoPrint (_octoprint._tcp), named escalop-v80-<serial>
    m_bonjour = Bonjour("octoprint")
                    .set_retries(3)
                    .set_timeout(2)
                    .on_reply([this, alive](BonjourReply&& reply) {
                        std::string serial = serial_from(reply.service_name);
                        if (serial.empty()) serial = serial_from(reply.hostname);
                        if (serial.empty()) return;
                        const std::string host = reply.ip.to_string();
                        wxGetApp().CallAfter([this, alive, serial, host]() {
                            if (!alive.lock()) return;
                            if (std::none_of(m_found.begin(), m_found.end(), [&](const Found& f) { return f.serial == serial; }))
                                m_found.push_back({serial, host});
                            std::sort(m_found.begin(), m_found.end(), [](const Found& a, const Found& b) { return a.serial < b.serial; });
                            refresh_find_page();
                        });
                    })
                    .on_complete([this, alive]() {
                        wxGetApp().CallAfter([this, alive]() {
                            if (!alive.lock()) return;
                            m_searching = false;
                            refresh_find_page();
                        });
                    })
                    .lookup();
}

namespace {
bool is_escaloplab(const Preset& p)
{
    const auto* model = p.config.option<ConfigOptionString>("printer_model");
    return model && boost::starts_with(model->value, "Escaloplab");
}

void set_host(DynamicPrintConfig& cfg, const std::string& host)
{
    cfg.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint)); // the module speaks OctoPrint's upload API
    cfg.set_key_value("print_host", new ConfigOptionString(host));
    cfg.set_key_value("print_host_webui", new ConfigOptionString(""));
}
} // namespace

void PrinterPage::apply_saved_host(const std::string& previous)
{
    GUI_App& app = wxGetApp();
    if (!app.app_config || !app.preset_bundle)
        return;
    PresetCollection& printers = app.preset_bundle->printers;
    std::string       host     = app.app_config->get("escalop_printer_host");
    if (host.empty()) {
        // Before 1.3.5 the address went into a "- Copy" of the preset: adopt it from there
        for (size_t i = 0; i < printers.size() && host.empty(); ++i) {
            const Preset& p = printers.preset(i);
            if (!p.is_system && is_escaloplab(p))
                if (const auto* opt = p.config.option<ConfigOptionString>("print_host"); opt && !opt->value.empty())
                    host = opt->value;
        }
        if (host.empty())
            return;
        app.app_config->set("escalop_printer_host", host);
    }
    const auto apply = [&](Preset& p) {
        if (!is_escaloplab(p))
            return;
        const auto*       opt = p.config.option<ConfigOptionString>("print_host");
        const std::string cur = opt ? opt->value : std::string();
        if (p.is_system || cur.empty() || cur == previous || cur == host)
            set_host(p.config, host);
    };
    for (size_t i = 0; i < printers.size(); ++i)
        apply(printers.preset(i));
    apply(printers.get_edited_preset()); // the same change on both sides: the preset doesn't look modified
}

// Remembers the address for all Escaloplab printer presets (the built-in ones too) and opens the printer's page
void PrinterPage::connect(const std::string& host)
{
    GUI_App& app = wxGetApp();
    Tab*     tab = app.get_tab(Preset::TYPE_PRINTER);
    if (is_escaloplab(app.preset_bundle->printers.get_edited_preset())) {
        const std::string previous = app.app_config->get("escalop_printer_host");
        app.app_config->set("escalop_printer_host", host);
        apply_saved_host(previous);
        if (tab)
            tab->reload_config();
    } else {
        // Another brand's preset: save the address in it, like the physical printer dialog does
        set_host(app.preset_bundle->printers.get_edited_preset().config, host);
        const Preset& sel    = tab->get_presets()->get_selected_preset();
        std::string   suffix = _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");
        std::string   name   = sel.is_default ? "Untitled" : sel.is_system ? sel.name + " - " + suffix : sel.name;
        tab->save_preset("", false, false, true, name);
    }
    wxGetApp().CallAfter([]() { wxGetApp().switch_printer_agent(); });

    m_problem.clear();
    if (MainFrame* mf = wxGetApp().mainframe) mf->load_printer_url();
}

void PrinterPage::load(const wxString& url, const wxString& apikey)
{
    // Orca shows its "missing connection" page when there's no address: we look for the printer instead
    if (url.IsEmpty() || url.StartsWith("file://")) {
        m_url.clear();
        show(Section::Find);
        if (IsShownOnScreen()) start_search();
        return;
    }
    m_url = url;
    m_problem.clear();
    wxString page = with_accent(url);
    m_web_view->load_url(page, apikey);
    show(Section::Web);
}

void PrinterPage::on_page_error()
{
    if (m_section != Section::Web) return;
    m_problem = wxString::Format(_L("Can't reach the printer at %s."), m_url);
    show(Section::Find);
    start_search();
}

void PrinterPage::show(Section section)
{
    m_section = section;
    switch (section) {
    case Section::Find: m_book->SetSelection(0); break;
    case Section::Web: m_book->SetSelection(1); break;
    case Section::Usb:
        if (m_usb_page) {
            m_book->SetSelection(2);
            m_firmware->on_activate();
        }
        break;
    }
    if (section == Section::Find) refresh_find_page();
    Layout();
}

void PrinterPage::on_activate()
{
    if (m_section == Section::Find && !m_searching) start_search();
    else if (m_section == Section::Usb && m_firmware) m_firmware->on_activate();
}

void PrinterPage::msw_rescale()
{
    if (m_firmware) m_firmware->msw_rescale();
    refresh_find_page();
}

void PrinterPage::on_sys_color_changed()
{
    if (m_firmware) m_firmware->on_sys_color_changed();
    refresh_find_page();
}

} // namespace GUI
} // namespace Slic3r
