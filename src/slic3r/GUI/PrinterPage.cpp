#include "PrinterPage.hpp"

#include <algorithm>

#include <wx/simplebook.h>
#include <wx/sizer.h>

#include "FirmwarePanel.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "PrinterWebView.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r {
namespace GUI {

namespace {
wxColour themed(const char* light) { return StateColor::darkModeColorFor(wxColour(light)); }
} // namespace

PrinterPage::PrinterPage(wxWindow* parent, bool with_firmware) : wxPanel(parent, wxID_ANY)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Header: what the tab is for and the two sections
    m_header      = new wxPanel(this, wxID_ANY);
    auto* header  = new wxBoxSizer(wxVERTICAL);
    auto* top_row = new wxBoxSizer(wxHORIZONTAL);
    auto* text    = new wxBoxSizer(wxVERTICAL);
    m_title       = new Label(m_header, Label::Head_20, _L("Printer"));
    text->Add(m_title);
    m_intro = new Label(m_header, Label::Body_14,
                        with_firmware ? _L("Update your printer's firmware over USB, or control a printer on your network.") :
                                        _L("Control a printer on your network."));
    text->Add(m_intro, 0, wxTOP, FromDIP(4));
    top_row->Add(text, 1, wxALIGN_CENTER_VERTICAL);

    if (with_firmware) {
        m_firmware_btn = new Button(m_header, _L("Update firmware"));
        m_control_btn  = new Button(m_header, _L("Control printer"));
        for (Button* btn : {m_firmware_btn, m_control_btn}) {
            btn->SetMinSize(wxSize(FromDIP(150), FromDIP(32)));
            top_row->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        }
        m_firmware_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_section(Section::Firmware); });
        m_control_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_section(Section::Control); });
    }
    header->Add(top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    m_hint = new Label(m_header, Label::Body_13, wxEmptyString);
    header->Add(m_hint, 0, wxEXPAND | wxALL, FromDIP(20));
    m_header->SetSizer(header);
    sizer->Add(m_header, 0, wxEXPAND);

    // The sections
    m_book = new wxSimplebook(this, wxID_ANY);
    if (with_firmware) {
        m_firmware = new FirmwarePanel(m_book);
        m_book->AddPage(m_firmware, wxEmptyString);
    }
    m_web_view = new PrinterWebView(m_book);
    m_book->AddPage(m_web_view, wxEmptyString);
    sizer->Add(m_book, 1, wxEXPAND);
    SetSizer(sizer);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        update_hint(); // re-wrap to the new width
        e.Skip();
    });

    // Start on the firmware, unless the printer has a network address
    const auto&    cfg      = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const bool     has_host = !cfg.opt_string("print_host").empty() || !cfg.opt_string("print_host_webui").empty();
    show_section(m_firmware && !has_host ? Section::Firmware : Section::Control);
}

void PrinterPage::show_section(Section section)
{
    if (!m_firmware)
        section = Section::Control;
    m_section = section;
    m_book->SetSelection(section == Section::Firmware ? 0 : m_book->FindPage(m_web_view));

    update_hint();
    apply_colors();
    if (section == Section::Firmware && IsShownOnScreen())
        m_firmware->on_activate();
    Layout();
}

void PrinterPage::update_hint()
{
    m_hint->SetLabel(m_section == Section::Firmware ?
        _L("Connect the printer to this computer with a USB cable, then follow the three steps below.") :
        _L("Watch and control your print from here: this shows your printer's own page (OctoPrint, Klipper, PrusaLink...). "
           "Add the printer's address with the network button next to the printer name on the Prepare tab. "
           "A printer connected only with a USB cable can't be controlled here."));
    m_hint->Wrap(std::max(GetClientSize().x - FromDIP(40), FromDIP(400)));
}

void PrinterPage::on_printer_url_loaded()
{
    if (m_firmware && !m_firmware->is_busy() && !IsShownOnScreen())
        show_section(Section::Control);
}

void PrinterPage::on_activate()
{
    if (m_section == Section::Firmware && m_firmware)
        m_firmware->on_activate();
}

void PrinterPage::apply_colors()
{
    const wxColour bg = themed("#F8F8F8");
    m_header->SetBackgroundColour(bg);
    m_title->SetForegroundColour(themed("#262E30"));
    m_intro->SetForegroundColour(themed("#6B6B6B"));
    m_hint->SetForegroundColour(themed("#6B6B6B"));
    for (Label* label : {m_title, m_intro, m_hint})
        label->SetBackgroundColour(bg);
    if (m_firmware_btn) {
        // The open section is the pink one
        m_firmware_btn->SetStyle(m_section == Section::Firmware ? ButtonStyle::Confirm : ButtonStyle::Regular, ButtonType::Choice);
        m_control_btn->SetStyle(m_section == Section::Control ? ButtonStyle::Confirm : ButtonStyle::Regular, ButtonType::Choice);
        for (Button* btn : {m_firmware_btn, m_control_btn})
            btn->SetMinSize(wxSize(FromDIP(150), FromDIP(32)));
    }
    m_header->Refresh();
}

void PrinterPage::msw_rescale()
{
    if (m_firmware)
        m_firmware->msw_rescale();
    apply_colors();
    Layout();
}

void PrinterPage::on_sys_color_changed()
{
    if (m_firmware)
        m_firmware->on_sys_color_changed();
    apply_colors();
}

} // namespace GUI
} // namespace Slic3r
