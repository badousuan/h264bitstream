#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/textfile.h>
#include <wx/progdlg.h>
#include <wx/dnd.h>
#include <vector>
#include <string>
#include "h264_stream.h"
#include "h264_analyzer.h"

class H264AnalyzerFrame;

class FileDropTarget : public wxFileDropTarget {
public:
    FileDropTarget(H264AnalyzerFrame* frame) : m_frame(frame) {}
    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;

private:
    H264AnalyzerFrame* m_frame;
};

// NalInfo structure to store basic information about each NAL unit
struct NalInfo {
    int64_t offset;
    int size;
    int type;
    std::string type_name;
    std::vector<uint8_t> data;
};

class H264AnalyzerFrame : public wxFrame {
public:
    H264AnalyzerFrame(const wxString& title);
    void AnalyzeFile(const wxString& filename);

private:
    void OnOpenFile(wxCommandEvent& event);
    void OnSaveDetails(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnNalSelected(wxListEvent& event);

    std::string GetNalTypeName(int type);
    wxString CaptureNalDetails(const NalInfo& nal);

    wxListCtrl* m_nalList;
    wxTextCtrl* m_detailsText;
    std::vector<NalInfo> m_nals;
    h264_stream_t* m_h264;

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_OpenFile = 1,
    ID_SaveDetails = 2
};

bool FileDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) {
    if (filenames.GetCount() > 0) {
        m_frame->AnalyzeFile(filenames[0]);
        return true;
    }
    return false;
}

wxBEGIN_EVENT_TABLE(H264AnalyzerFrame, wxFrame)
    EVT_MENU(ID_OpenFile, H264AnalyzerFrame::OnOpenFile)
    EVT_MENU(ID_SaveDetails, H264AnalyzerFrame::OnSaveDetails)
    EVT_BUTTON(ID_OpenFile, H264AnalyzerFrame::OnOpenFile)
    EVT_BUTTON(ID_SaveDetails, H264AnalyzerFrame::OnSaveDetails)
    EVT_MENU(wxID_EXIT, H264AnalyzerFrame::OnExit)
    EVT_MENU(wxID_ABOUT, H264AnalyzerFrame::OnAbout)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, H264AnalyzerFrame::OnNalSelected)
wxEND_EVENT_TABLE()

class H264AnalyzerApp : public wxApp {
public:
    virtual bool OnInit() {
        H264AnalyzerFrame* frame = new H264AnalyzerFrame("H.264 Analyzer GUI");
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(H264AnalyzerApp);

H264AnalyzerFrame::H264AnalyzerFrame(const wxString& title)
    : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1000, 700)) {
    
    m_h264 = h264_new();

    SetDropTarget(new FileDropTarget(this));

    wxMenu* menuFile = new wxMenu;
    menuFile->Append(ID_OpenFile, "&Open...\tCtrl-O", "Open an H.264 bitstream file");
    menuFile->Append(ID_SaveDetails, "&Save Details...\tCtrl-S", "Save the detailed NAL information to a file");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);

    wxMenu* menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);

    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuHelp, "&Help");
    SetMenuBar(menuBar);

    // Create a top panel for buttons to ensure visibility (avoiding native toolbar quirks)
    wxPanel* buttonPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    wxButton* openBtn = new wxButton(buttonPanel, ID_OpenFile, "Open File");
    wxButton* saveBtn = new wxButton(buttonPanel, ID_SaveDetails, "Save Details");

    buttonSizer->Add(openBtn, 0, wxALL, 5);
    buttonSizer->Add(saveBtn, 0, wxALL, 5);
    buttonPanel->SetSizer(buttonSizer);

    CreateStatusBar();
    SetStatusText("Welcome to H.264 Analyzer!");

    wxSplitterWindow* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);

    m_nalList = new wxListCtrl(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_nalList->InsertColumn(0, "Offset", wxLIST_FORMAT_LEFT, 100);
    m_nalList->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 80);
    m_nalList->InsertColumn(2, "Type", wxLIST_FORMAT_LEFT, 120);
    m_nalList->InsertColumn(3, "RefIdc", wxLIST_FORMAT_LEFT, 60);
    m_nalList->InsertColumn(4, "FZC", wxLIST_FORMAT_LEFT, 40);

    m_detailsText = new wxTextCtrl(splitter, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    // Use monospace font for details
    m_detailsText->SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    splitter->SplitVertically(m_nalList, m_detailsText, 420);
    splitter->SetMinimumPaneSize(100);

    // Use a vertical sizer for the frame to arrange button panel and splitter
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(buttonPanel, 0, wxEXPAND);
    mainSizer->Add(splitter, 1, wxEXPAND);
    SetSizer(mainSizer);
}

void H264AnalyzerFrame::OnExit(wxCommandEvent& event) {
    h264_free(m_h264);
    Close(true);
}

void H264AnalyzerFrame::OnAbout(wxCommandEvent& event) {
    wxMessageBox("H.264 Analyzer GUI\nBased on h264bitstream library", "About H.264 Analyzer", wxOK | wxICON_INFORMATION);
}

void H264AnalyzerFrame::OnOpenFile(wxCommandEvent& event) {
    wxFileDialog openFileDialog(this, _("Open H.264 bitstream"), "", "",
                               "H.264 files (*.264;*.h264;*.bin)|*.264;*.h264;*.bin|All files (*.*)|*.*", 
                               wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_CANCEL)
        return;

    AnalyzeFile(openFileDialog.GetPath());
}

void H264AnalyzerFrame::OnSaveDetails(wxCommandEvent& event) {
    if (m_nals.empty()) {
        if (m_detailsText->IsEmpty()) {
            wxMessageBox("Nothing to save!", "Warning", wxOK | wxICON_WARNING);
        } else {
            // Fallback to saving current view if m_nals is somehow empty but text is not
            wxFileDialog saveFileDialog(this, _("Save current details"), "", "details.txt",
                                       "Text files (*.txt)|*.txt|All files (*.*)|*.*", 
                                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (saveFileDialog.ShowModal() != wxID_CANCEL) {
                m_detailsText->SaveFile(saveFileDialog.GetPath());
            }
        }
        return;
    }

    wxFileDialog saveFileDialog(this, _("Save all NAL details"), "", "details.txt",
                               "Text files (*.txt)|*.txt|All files (*.*)|*.*", 
                               wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;

    wxString filename = saveFileDialog.GetPath();
    wxProgressDialog progress("Saving Details", "Processing NAL units...", m_nals.size(), this, wxPD_APP_MODAL | wxPD_AUTO_HIDE);

    wxTextFile file;
    if (!file.Create(filename) && !file.Open(filename)) {
        wxMessageBox("Could not create or open file.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    file.Clear();
    for (size_t i = 0; i < m_nals.size(); ++i) {
        file.AddLine(CaptureNalDetails(m_nals[i]));
        progress.Update(i + 1);
    }

    if (file.Write()) {
        SetStatusText("All NAL details saved successfully.");
    } else {
        wxMessageBox("Error writing to file.", "Error", wxOK | wxICON_ERROR);
    }
    file.Close();
}

std::string H264AnalyzerFrame::GetNalTypeName(int type) {
    switch (type) {
        case NAL_UNIT_TYPE_UNSPECIFIED: return "Unspecified";
        case NAL_UNIT_TYPE_CODED_SLICE_NON_IDR: return "non-IDR picture";
        case NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A: return "Slice partition A";
        case NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_B: return "Slice partition B";
        case NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_C: return "Slice partition C";
        case NAL_UNIT_TYPE_CODED_SLICE_IDR: return "IDR picture";
        case NAL_UNIT_TYPE_SEI: return "SEI";
        case NAL_UNIT_TYPE_SPS: return "SPS";
        case NAL_UNIT_TYPE_PPS: return "PPS";
        case NAL_UNIT_TYPE_AUD: return "Access unit delimiter";
        case NAL_UNIT_TYPE_END_OF_SEQUENCE: return "End of sequence";
        case NAL_UNIT_TYPE_END_OF_STREAM: return "End of stream";
        case NAL_UNIT_TYPE_FILLER: return "Filler data";
        case NAL_UNIT_TYPE_SPS_EXT: return "SPS extension";
        case NAL_UNIT_TYPE_PREFIX_NAL: return "Prefix NAL unit";
        case NAL_UNIT_TYPE_SUBSET_SPS: return "Subset SPS";
        case NAL_UNIT_TYPE_DPS: return "DPS";
        case NAL_UNIT_TYPE_CODED_SLICE_AUX: return "Slice auxiliary";
        case NAL_UNIT_TYPE_CODED_SLICE_SVC_EXTENSION: return "Coded slice extension";
        default: return "Unknown";
    }
}

void H264AnalyzerFrame::AnalyzeFile(const wxString& filename) {
    m_nalList->DeleteAllItems();
    m_nals.clear();

    auto callback = [](void* user_data, int64_t offset, uint8_t* data, int size) {
        H264AnalyzerFrame* self = static_cast<H264AnalyzerFrame*>(user_data);
        NalInfo nal;
        nal.offset = offset;
        nal.size = size;
        
        int fzc = -1;
        int ref_idc = -1;

        if (size > 0) {
            uint8_t header = data[0];
            fzc = (header >> 7) & 0x01;
            ref_idc = (header >> 5) & 0x03;
            nal.type = header & 0x1F;
            nal.type_name = self->GetNalTypeName(nal.type);
            nal.data.assign(data, data + size);
        }

        self->m_nals.push_back(nal);

        long itemIndex = self->m_nalList->InsertItem(self->m_nalList->GetItemCount(), wxString::Format("%lld", nal.offset));
        self->m_nalList->SetItem(itemIndex, 1, wxString::Format("%d", nal.size));
        self->m_nalList->SetItem(itemIndex, 2, nal.type_name);
        self->m_nalList->SetItem(itemIndex, 3, wxString::Format("%d", ref_idc));
        self->m_nalList->SetItem(itemIndex, 4, wxString::Format("%d", fzc));
    };

    h264_analyze_file(filename.mb_str(), callback, this);

    SetStatusText(wxString::Format("Found %zu NAL units", m_nals.size()));
}

#if defined(_WIN32) && !defined(HAVE_OPEN_MEMSTREAM)
#include <windows.h>
#include <io.h>
#include <fcntl.h>

// Simple replacement for open_memstream on Windows
FILE* open_memstream(char** ptr, size_t* sizeloc) {
    if (!ptr || !sizeloc) return NULL;

    char temp_path[MAX_PATH];
    char temp_file[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_path) == 0) return NULL;
    if (GetTempFileNameA(temp_path, "h264", 0, temp_file) == 0) return NULL;

    FILE* f = fopen(temp_file, "w+b");
    if (!f) return NULL;

    // We need a way to retrieve the buffer and its size later.
    // A simple approach is to store the temp file name and read it on fclose.
    // This is a simplified and somewhat hacky implementation.
    // The caller is responsible for reading the file and deleting it.
    *ptr = _strdup(temp_file); // Store the temp file name in *ptr
    *sizeloc = 0; // Not used in this implementation to store the size directly

    return f;
}

void close_memstream(FILE* f, char** ptr, size_t* sizeloc) {
    if (!f || !ptr || !*ptr || !sizeloc) return;

    fflush(f);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(size + 1);
    if (buf) {
        size_t readSize = fread(buf, 1, size, f);
        buf[readSize] = '\0';
        *sizeloc = readSize;
    }

    fclose(f);
    DeleteFileA(*ptr); // Delete the temp file
    free(*ptr); // Free the stored filename
    *ptr = buf; // Return the buffer
}

#define USE_CUSTOM_MEMSTREAM 1
#endif

wxString H264AnalyzerFrame::CaptureNalDetails(const NalInfo& nal) {
    char* memBuf = nullptr;
    size_t memSize = 0;
    FILE* memFile = nullptr;

#ifdef USE_CUSTOM_MEMSTREAM
    memFile = open_memstream(&memBuf, &memSize);
#else
    memFile = open_memstream(&memBuf, &memSize);
#endif
    
    if (memFile) {
        FILE* old_dbgfile = h264_dbgfile;
        h264_dbgfile = memFile;

        fprintf(h264_dbgfile, "!! Found NAL at offset %lld (0x%04llX), size %lld (0x%04llX) \n",
                (long long int)nal.offset, (long long int)nal.offset,
                (long long int)nal.size, (long long int)nal.size);

        h264_stream_t* h = h264_new();
        read_debug_nal_unit(h, const_cast<uint8_t*>(nal.data.data()), nal.size);
        h264_free(h);
        
#ifdef USE_CUSTOM_MEMSTREAM
        close_memstream(memFile, &memBuf, &memSize);
#else
        fflush(memFile);
        fclose(memFile);
#endif

        h264_dbgfile = old_dbgfile;

        if (memBuf) {
            wxString details = wxString::FromUTF8(memBuf);
            free(memBuf);
            return details;
        }
    }
    return "Error capturing details";
}

void H264AnalyzerFrame::OnNalSelected(wxListEvent& event) {
    long index = event.GetIndex();
    if (index < 0 || index >= (long)m_nals.size()) return;

    m_detailsText->SetValue(CaptureNalDetails(m_nals[index]));
}
