// Boost.Asio (in Serial.hpp) has to come before anything that includes windows.h
#include "slic3r/Utils/Serial.hpp"
#include "FirmwarePanel.hpp"

#include <algorithm>
#include <cstring>
#include <thread>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/filedlg.h>
#include <wx/statbmp.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "format.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/HyperLink.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/StaticBox.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r {
namespace GUI {

namespace {

const char* const FIRMWARE_TAG_PREFIX = "firmware-v";
const char* const VENDOR              = "Escaloplab";
const wxColour    PINK(255, 46, 138); // #FF2E8A, Escaloplab's neon pink

wxColour themed(const char* light) { return StateColor::darkModeColorFor(wxColour(light)); }

// Boards whose USB chip an ATmega2560 printer uses: Arduino, CH340, FTDI, CP210x
bool is_printer_usb(const Utils::SerialPortInfo& port)
{
    for (unsigned vid : {0x2341u, 0x2A03u, 0x1A86u, 0x0403u, 0x10C4u})
        if (port.id_vendor == vid)
            return true;
    return false;
}

// Runs f on the UI thread, unless the panel is gone by then. `alive` is taken on the UI thread.
template<class F> void run_on_ui(const std::weak_ptr<int>& alive, F f)
{
    wxGetApp().CallAfter([alive, f]() mutable {
        if (alive.lock())
            f();
    });
}

} // namespace

// The round step number on the left; a check mark when the step is done
class FirmwareStepBadge : public wxWindow
{
public:
    enum class State { Pending, Active, Done };

    FirmwareStepBadge(wxWindow* parent, int number) : m_number(number)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Create(parent, wxID_ANY, wxDefaultPosition, parent->FromDIP(wxSize(30, 30)));
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) { render(); });
    }

    void set_state(State state)
    {
        if (state != m_state) {
            m_state = state;
            Refresh();
        }
    }

private:
    void render()
    {
        wxAutoBufferedPaintDC paint(this);
        paint.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        paint.Clear();
        wxGCDC       dc(paint);
        const wxSize size   = GetClientSize();
        const double radius = std::min(size.x, size.y) / 2.0 - 1;
        const wxPoint center(size.x / 2, size.y / 2);
        const bool    filled = m_state != State::Pending;
        dc.SetPen(wxPen(filled ? PINK : themed("#DBDBDB"), FromDIP(1)));
        dc.SetBrush(filled ? wxBrush(PINK) : *wxTRANSPARENT_BRUSH);
        dc.DrawCircle(center, int(radius));
        if (m_state == State::Done) {
            dc.SetPen(wxPen(*wxWHITE, FromDIP(2), wxPENSTYLE_SOLID));
            const double s = radius / 10.0;
            wxPoint check[3] = {wxPoint(center.x - int(4.5 * s), center.y), wxPoint(center.x - int(1 * s), center.y + int(3.5 * s)),
                                wxPoint(center.x + int(5 * s), center.y - int(3.5 * s))};
            dc.DrawLines(3, check);
        } else {
            dc.SetFont(Label::Head_14);
            dc.SetTextForeground(filled ? *wxWHITE : themed("#6B6B6B"));
            const wxString text = wxString::Format("%d", m_number);
            const wxSize   ext  = dc.GetTextExtent(text);
            dc.DrawText(text, center.x - ext.x / 2, center.y - ext.y / 2);
        }
    }

    int   m_number;
    State m_state = State::Pending;
};

FirmwarePanel::FirmwarePanel(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL | wxTAB_TRAVERSAL)
{
    SetScrollRate(FromDIP(10), FromDIP(10));
    if (auto it = wxGetApp().preset_bundle->vendors.find(VENDOR); it != wxGetApp().preset_bundle->vendors.end())
        for (const auto& model : it->second.models)
            m_models.push_back(model.id);
    if (m_models.empty())
        m_models = {"Escaloplab v80", "Escaloplab v80 Direct"};
    // Start on the printer chosen in the slicer
    const std::string current = wxGetApp().preset_bundle->printers.get_edited_preset().config.opt_string("printer_model");
    m_model = std::find(m_models.begin(), m_models.end(), current) != m_models.end() ? current : m_models.front();
    rebuild();
}

void FirmwarePanel::rebuild()
{
    Freeze();
    DestroyChildren();
    m_badges[0] = m_badges[1] = m_badges[2] = nullptr;

    const wxColour page_bg = themed("#F8F8F8");
    const wxColour card_bg = themed("#FFFFFF");
    const wxColour text    = themed("#262E30");
    const wxColour soft    = themed("#6B6B6B");
    SetBackgroundColour(page_bg);

    auto make_card = [&]() {
        auto* card = new StaticBox(this, wxID_ANY);
        card->SetCornerRadius(FromDIP(12));
        card->SetBorderWidth(1);
        card->SetBorderColor(StateColor(themed("#EEEEEE")));
        card->SetBackgroundColor(StateColor(card_bg));
        card->SetBackgroundColour(card_bg);
        return card;
    };
    auto make_label = [&](wxWindow* parent, const wxString& str, const wxFont& font, const wxColour& fg) {
        auto* label = new Label(parent, font, str);
        label->SetForegroundColour(fg);
        label->SetBackgroundColour(parent->GetBackgroundColour());
        return label;
    };
    const int wrap = FromDIP(470);

    // Header: the printer, what this tab does and the newest firmware
    auto* hero       = make_card();
    auto* hero_sizer = new wxBoxSizer(wxHORIZONTAL);
    {
        const int   px = FromDIP(120);
        wxImage           img;
        const std::string picture = resources_dir() + "/profiles/" + VENDOR + "/" + m_model + "_cover.png";
        if (wxFileExists(from_u8(picture)) && img.LoadFile(from_u8(picture), wxBITMAP_TYPE_PNG))
            hero_sizer->Add(new wxStaticBitmap(hero, wxID_ANY, wxBitmap(img.Rescale(px, px, wxIMAGE_QUALITY_HIGH))), 0,
                            wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(20));
        else
            hero_sizer->AddSpacer(FromDIP(20));

        auto* info = new wxBoxSizer(wxVERTICAL);
        info->Add(make_label(hero, _L("Firmware update"), Label::Head_24, text));
        auto* intro = make_label(hero, _L("Install the newest Escaloplab firmware on your printer with a USB cable. "
                                          "It takes about a minute."), Label::Body_14, soft);
        intro->Wrap(wrap);
        info->Add(intro, 0, wxTOP, FromDIP(6));

        auto* grid = new wxFlexGridSizer(2, FromDIP(10), FromDIP(14));
        grid->Add(make_label(hero, _L("Printer"), Label::Body_14, soft), 0, wxALIGN_CENTER_VERTICAL);
        m_model_combo = new ComboBox(hero, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(240), -1), 0, nullptr, wxCB_READONLY);
        for (const std::string& model : m_models)
            m_model_combo->Append(from_u8(model));
        grid->Add(m_model_combo, 0, wxALIGN_CENTER_VERTICAL);

        grid->Add(make_label(hero, _L("Newest firmware"), Label::Body_14, soft), 0, wxALIGN_CENTER_VERTICAL);
        auto* latest_row = new wxBoxSizer(wxHORIZONTAL);
        m_latest = make_label(hero, wxEmptyString, Label::Head_14, PINK);
        latest_row->Add(m_latest, 0, wxALIGN_CENTER_VERTICAL);
        m_whats_new = new HyperLink(hero, _L("What's new"));
        m_whats_new->SetFont(Label::Body_13);
        m_whats_new->SetBackgroundColour(card_bg);
        latest_row->Add(m_whats_new, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
        grid->Add(latest_row, 0, wxALIGN_CENTER_VERTICAL);
        info->Add(grid, 0, wxTOP, FromDIP(16));

        hero_sizer->Add(info, 1, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(20));
    }
    hero->SetSizer(hero_sizer);

    // Steps: connect, install, ready
    auto* steps       = make_card();
    auto* steps_sizer = new wxBoxSizer(wxVERTICAL);
    auto  add_step    = [&](int index, const wxString& title, const wxString& description) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_badges[index] = new FirmwareStepBadge(steps, index + 1);
        row->Add(m_badges[index], 0, wxALIGN_TOP);
        auto* body = new wxBoxSizer(wxVERTICAL);
        body->Add(make_label(steps, title, Label::Head_16, text));
        auto* desc = make_label(steps, description, Label::Body_13, soft);
        desc->Wrap(wrap);
        body->Add(desc, 0, wxTOP, FromDIP(4));
        row->Add(body, 1, wxLEFT, FromDIP(14));
        steps_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(22));
        return body;
    };
    auto add_divider = [&]() {
        auto* line = new wxPanel(steps, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
        line->SetBackgroundColour(themed("#EEEEEE"));
        steps_sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(22));
    };

    {
        auto* body = add_step(0, _L("Connect your printer"),
                              _L("Plug the printer into this computer with its USB cable and switch it on."));
        auto* row  = new wxBoxSizer(wxHORIZONTAL);
        m_port_combo = new ComboBox(steps, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(280), -1), 0, nullptr, wxCB_READONLY);
        row->Add(m_port_combo, 0, wxALIGN_CENTER_VERTICAL);
        m_refresh_btn = new Button(steps, _L("Refresh"));
        m_refresh_btn->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
        row->Add(m_refresh_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        m_check_btn = new Button(steps, _L("Check printer"));
        m_check_btn->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
        row->Add(m_check_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        body->Add(row, 0, wxTOP, FromDIP(12));
        m_printer_label = make_label(steps, wxEmptyString, Label::Body_13, soft);
        body->Add(m_printer_label, 0, wxTOP, FromDIP(8));
    }
    add_divider();
    {
        auto* body = add_step(1, _L("Install the update"),
                              _L("Keep the printer switched on and plugged in until it's done. "
                                 "After an update, check the Z offset and run bed leveling again."));
        m_update_btn = new Button(steps, _L("Update firmware"));
        m_update_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
        m_update_btn->SetMinSize(wxSize(FromDIP(180), FromDIP(36)));
        body->Add(m_update_btn, 0, wxTOP, FromDIP(12));

        m_progress = new ProgressBar(steps, wxID_ANY, 100);
        m_progress->SetHeight(FromDIP(8));
        m_progress->SetMinSize(wxSize(FromDIP(460), FromDIP(8)));
        m_progress->SetProgressBackgroundColour(PINK);               // the filled part
        m_progress->SetProgressForedColour(themed("#EEEEEE")); // the track
        m_progress->ShowNumber(false);
        body->Add(m_progress, 0, wxTOP, FromDIP(14));
        m_stage_label = make_label(steps, wxEmptyString, Label::Body_13, soft);
        body->Add(m_stage_label, 0, wxTOP, FromDIP(6));
        m_status_label = make_label(steps, wxEmptyString, Label::Body_13, soft);
        body->Add(m_status_label, 0, wxTOP, FromDIP(8));
    }
    add_divider();
    add_step(2, _L("Ready to print"), _L("The printer restarts by itself with the new firmware."));
    steps_sizer->AddSpacer(FromDIP(22));
    steps->SetSizer(steps_sizer);

    // For a firmware file from Escaloplab support
    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(make_label(this, _L("Have a firmware file from Escaloplab support?"), Label::Body_13, soft), 0, wxALIGN_CENTER_VERTICAL);
    m_file_link = new HyperLink(this, _L("Install it from this computer"));
    m_file_link->SetFont(Label::Body_13);
    m_file_link->SetBackgroundColour(page_bg);
    footer->Add(m_file_link, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(hero, 0, wxEXPAND);
    column->Add(steps, 0, wxEXPAND | wxTOP, FromDIP(16));
    column->Add(footer, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(16));
    column->SetMinSize(wxSize(FromDIP(680), -1));
    auto* outer = new wxBoxSizer(wxHORIZONTAL);
    outer->AddStretchSpacer(1);
    outer->Add(column, 0, wxTOP | wxBOTTOM, FromDIP(32));
    outer->AddStretchSpacer(1);
    SetSizer(outer);

    m_model_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        const int sel = m_model_combo->GetSelection();
        if (sel >= 0 && size_t(sel) < m_models.size() && m_models[sel] != m_model) {
            m_model = m_models[sel];
            CallAfter([this] { rebuild(); }); // the picture changes
        }
    });
    m_port_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        const int sel = m_port_combo->GetSelection();
        if (sel >= 0 && size_t(sel) < m_ports.size())
            m_port = m_ports[sel].port;
    });
    m_whats_new->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (m_release)
            wxLaunchDefaultBrowser(from_u8(m_release->page_url));
    });
    m_refresh_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_ports(); });
    m_check_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { check_printer(); });
    m_update_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { start_update(); });
    m_file_link->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { install_from_file(); });

    update_ui();
    Thaw();
}

void FirmwarePanel::update_ui()
{
    if (!m_update_btn)
        return;
    const wxColour soft  = themed("#6B6B6B");
    auto           color = [&](Tone tone) { return tone == Tone::Error ? themed("#D01B1B") : tone == Tone::Success ? PINK : soft; };

    const auto model_it = std::find(m_models.begin(), m_models.end(), m_model);
    m_model_combo->SetSelection(model_it == m_models.end() ? 0 : int(model_it - m_models.begin()));

    switch (m_fetch) {
    case Fetch::NotStarted:
    case Fetch::Loading: m_latest->SetLabel(_L("Looking…")); break;
    case Fetch::Failed: m_latest->SetLabel(_L("Couldn't check (offline?)")); break;
    case Fetch::Done:
        m_latest->SetLabel(m_release ? from_u8(m_release->version) : _L("None published yet"));
        break;
    }
    m_whats_new->Show(m_release.has_value());

    m_port_combo->Clear();
    int port_sel = -1;
    for (size_t i = 0; i < m_ports.size(); ++i) {
        m_port_combo->Append(from_u8(m_ports[i].name));
        if (m_ports[i].port == m_port)
            port_sel = int(i);
    }
    if (m_ports.empty())
        m_port_combo->SetLabel(_L("No printer found. Is the cable plugged in?"));
    else if (port_sel >= 0)
        m_port_combo->SetSelection(port_sel);

    m_printer_label->SetLabel(m_checking ? _L("Asking the printer…") : m_printer_text);
    m_printer_label->SetForegroundColour(color(m_printer_tone));

    const bool idle = !m_busy && !m_checking;
    m_model_combo->Enable(idle);
    m_port_combo->Enable(idle);
    m_refresh_btn->Enable(idle);
    m_check_btn->Enable(idle && !m_port.empty());
    m_update_btn->Enable(idle && !m_port.empty() && !hex_url_for_model().empty());
    m_update_btn->SetLabel(m_done ? _L("Update again") : _L("Update firmware"));
    m_file_link->Enable(idle);

    m_progress->Show(m_busy);
    m_progress->SetValue(m_percent);
    m_stage_label->Show(m_busy);
    m_stage_label->SetLabel(wxString::Format("%s %d%%", m_stage, m_percent));
    m_status_label->SetLabel(m_status);
    m_status_label->SetForegroundColour(color(m_status_tone));
    m_status_label->Wrap(FromDIP(470));

    using State = FirmwareStepBadge::State;
    m_badges[0]->set_state(m_busy || m_done || !m_port.empty() ? State::Done : State::Active);
    m_badges[1]->set_state(m_done ? State::Done : m_port.empty() ? State::Pending : State::Active);
    m_badges[2]->set_state(m_done ? State::Done : State::Pending);

    Layout();
    FitInside();
    Refresh();
}

void FirmwarePanel::on_activate()
{
    if (m_fetch == Fetch::NotStarted || m_fetch == Fetch::Failed)
        fetch_latest();
    if (!m_busy && !m_checking)
        refresh_ports();
}

void FirmwarePanel::refresh_ports()
{
    m_ports.clear();
    for (const Utils::SerialPortInfo& info : Utils::scan_serial_ports_extended())
        m_ports.push_back({info.port, info.friendly_name, is_printer_usb(info)});
    // Keep the chosen port; otherwise the first one that looks like a printer's USB chip
    if (std::none_of(m_ports.begin(), m_ports.end(), [this](const Port& p) { return p.port == m_port; })) {
        auto printer = std::find_if(m_ports.begin(), m_ports.end(), [](const Port& p) { return p.printer_usb; });
        m_port = printer != m_ports.end() ? printer->port : m_ports.empty() ? std::string() : m_ports.front().port;
        m_printer_text.clear();
    }
    update_ui();
}

void FirmwarePanel::fetch_latest()
{
    m_fetch = Fetch::Loading;
    update_ui();
    // The same GitHub repository as the app's own releases; firmware releases are tagged "firmware-vX.Y.Z"
    std::string url = wxGetApp().app_config->version_check_url();
    url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("per_page=100");
    std::weak_ptr<int> alive = m_alive;
    auto               http  = Http::get(url);
    http.header("accept", "application/vnd.github.v3+json")
        .timeout_connect(5)
        .timeout_max(15)
        .on_error([this, alive](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Firmware: release list failed: " << error << ", HTTP " << status;
            run_on_ui(alive, [this] {
                m_fetch = Fetch::Failed;
                update_ui();
            });
        })
        .on_complete([this, alive](std::string body, unsigned) {
            boost::optional<Release> best;
            boost::optional<Semver>  best_version;
            try {
                for (const auto& rel : nlohmann::json::parse(body)) {
                    const std::string tag = rel.value("tag_name", "");
                    if (!boost::starts_with(tag, FIRMWARE_TAG_PREFIX) || rel.value("draft", false) || rel.value("prerelease", false))
                        continue;
                    const std::string version = tag.substr(strlen(FIRMWARE_TAG_PREFIX));
                    const auto        semver  = Semver::parse(version);
                    if (!semver || (best_version && *semver <= *best_version))
                        continue;
                    Release release;
                    release.version  = version;
                    release.page_url = rel.value("html_url", "");
                    for (const auto& asset : rel.value("assets", nlohmann::json::array()))
                        if (boost::iends_with(asset.value("name", std::string()), ".hex"))
                            release.hex_files.emplace_back(asset.value("name", ""), asset.value("browser_download_url", ""));
                    best         = release;
                    best_version = semver;
                }
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "Firmware: release list unreadable: " << e.what();
            }
            run_on_ui(alive, [this, best] {
                m_release = best;
                m_fetch   = Fetch::Done;
                update_ui();
            });
        })
        .perform();
}

std::string FirmwarePanel::hex_url_for_model() const
{
    if (!m_release || m_release->hex_files.empty())
        return {};
    // "...direct....hex" is the Direct model's; any other .hex the standard model's
    const bool direct = boost::icontains(m_model, "direct");
    for (const auto& [name, url] : m_release->hex_files)
        if (boost::icontains(name, "direct") == direct)
            return url;
    return m_release->hex_files.size() == 1 ? m_release->hex_files.front().second : std::string();
}

void FirmwarePanel::check_printer()
{
    if (m_port.empty())
        return;
    m_checking = true;
    update_ui();
    const std::string  port  = m_port;
    std::weak_ptr<int> alive = m_alive;
    std::thread([this, port, alive] {
        wxString text;
        Tone     tone = Tone::Normal;
        try {
            if (auto info = Utils::AvrFlasher::query(port)) {
                text = format_wxstr(_L("On the printer now: %1%"), info->firmware_name);
                if (!info->machine_type.empty())
                    text += wxString::FromUTF8(" Â· ") + from_u8(info->machine_type);
            } else {
                text = _L("The printer didn't answer. Is it switched on? You can still update it.");
                tone = Tone::Error;
            }
        } catch (const std::exception& e) {
            text = from_u8(e.what());
            tone = Tone::Error;
        }
        run_on_ui(alive, [this, text, tone] {
            m_checking     = false;
            m_printer_text = text;
            m_printer_tone = tone;
            update_ui();
        });
    }).detach();
}

void FirmwarePanel::start_update()
{
    const std::string url = hex_url_for_model();
    if (url.empty() || m_port.empty() || !m_release)
        return;
    MessageDialog dialog(this,
                         format_wxstr(_L("Install firmware %1% on the %2% connected to %3%?\n\n"
                                         "Keep the printer switched on and plugged in until it's done."),
                                      m_release->version, m_model, m_port),
                         _L("Update firmware"), wxYES_NO | wxICON_QUESTION);
    if (dialog.ShowModal() != wxID_YES)
        return;

    m_busy    = true;
    m_done    = false;
    m_percent = 0;
    m_stage   = _L("Downloading");
    set_status(wxEmptyString, Tone::Normal);
    const wxString     what  = format_wxstr(_L("firmware %1%"), m_release->version);
    std::weak_ptr<int> alive = m_alive;
    Http::get(url)
        .on_error([this, alive](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Firmware: download failed: " << error << ", HTTP " << status;
            run_on_ui(alive, [this] {
                m_busy = false;
                set_status(_L("The firmware couldn't be downloaded. Check the internet connection and try again."), Tone::Error);
            });
        })
        .on_complete([this, alive, what](std::string body, unsigned) {
            run_on_ui(alive, [this, what, body] {
                try {
                    flash(Utils::FirmwareImage::from_intel_hex(body), what);
                } catch (const std::exception& e) {
                    m_busy = false;
                    set_status(from_u8(e.what()), Tone::Error);
                }
            });
        })
        .perform();
}

void FirmwarePanel::install_from_file()
{
    if (m_busy || m_checking)
        return;
    if (m_port.empty()) {
        set_status(_L("Connect the printer first (step 1)."), Tone::Error);
        return;
    }
    wxFileDialog dialog(this, _L("Choose a firmware file"), wxEmptyString, wxEmptyString, "Firmware (*.hex)|*.hex",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;
    try {
        boost::nowide::ifstream file(into_u8(dialog.GetPath()), std::ios::binary);
        const std::string       text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Utils::FirmwareImage    image = Utils::FirmwareImage::from_intel_hex(text);
        MessageDialog confirm(this,
                              format_wxstr(_L("Install %1% on the printer connected to %2%?\n\n"
                                              "Keep the printer switched on and plugged in until it's done."),
                                           dialog.GetFilename(), m_port),
                              _L("Update firmware"), wxYES_NO | wxICON_QUESTION);
        if (confirm.ShowModal() != wxID_YES)
            return;
        m_done = false;
        flash(std::move(image), dialog.GetFilename());
    } catch (const std::exception& e) {
        set_status(from_u8(e.what()), Tone::Error);
    }
}

void FirmwarePanel::flash(Utils::FirmwareImage image, const wxString& what)
{
    m_busy    = true;
    m_percent = 0;
    m_stage   = _L("Connecting to the printer");
    set_status(wxEmptyString, Tone::Normal);
    const std::string  port  = m_port;
    std::weak_ptr<int> alive = m_alive;
    std::thread([this, port, alive, image = std::move(image), what] {
        int last_percent = -1;
        try {
            Utils::AvrFlasher::flash(port, image, [this, alive, &last_percent](const std::string& stage, int percent) {
                if (percent == last_percent)
                    return;
                last_percent = percent;
                const wxString stage_text = from_u8(stage);
                run_on_ui(alive, [this, stage_text, percent] {
                    m_stage   = stage_text;
                    m_percent = percent;
                    update_ui();
                });
            });
            run_on_ui(alive, [this, what] {
                m_busy = false;
                m_done = true;
                set_status(format_wxstr(_L("Done! The printer now runs %1%."), what), Tone::Success);
                check_printer(); // shows what the restarted printer reports
            });
        } catch (const std::exception& e) {
            const wxString message = from_u8(e.what());
            run_on_ui(alive, [this, message] {
                m_busy = false;
                set_status(message, Tone::Error);
            });
        }
    }).detach();
}

void FirmwarePanel::set_status(const wxString& text, Tone tone)
{
    m_status      = text;
    m_status_tone = tone;
    update_ui();
}

} // namespace GUI
} // namespace Slic3r
