///////////////////////////////////////////////////////////////////
//  Macro to help with online analysis
//    B. Moffit  Oct. 2003
///////////////////////////////////////////////////////////////////

#include "panguinOnline.hh"
#include <TBranch.h>
#include <TGClient.h>
#include <TCanvas.h>
#include <TStyle.h>
#include <TROOT.h>
#include <TGImageMap.h>
#include <TGFileDialog.h>
#include <TKey.h>
#include <TSystem.h>
#include <TLatex.h>
#include "TPaveText.h"
#include <TApplication.h>
#include "TEnv.h"
#include "TRegexp.h"
#include "TGaxis.h"
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <sys/stat.h>
#include <ctime>
#include <utility>
#include <cassert>
#include <memory>
#include <type_traits>  // std::make_signed

#define OLDTIMERUPDATE

ClassImp(OnlineGUI)

using namespace std;

template<typename T>
static inline
typename std::make_signed<T>::type SINT(T uint) {
  return static_cast<typename std::make_signed<T>::type>(uint);
}

//_____________________________________________________________________________
// Helper function to get value for given key from const std::map&
static const string& getMapVal( const map<string, string>& m, const string& key )
{
  static const string nullstr{};
  auto it = m.find(key);
  return it != m.end() ? it->second : nullstr;
}

//_____________________________________________________________________________
// Set up mode of current pad (axes linear/log scale, margins)
static void SetupPad( const OnlineGUI::cmdmap_t& command )
{
  gPad->SetLogx(command.find("logx") != command.end());
  gPad->SetLogy(command.find("logy") != command.end());
  gPad->SetLogz(command.find("logz") != command.end());
  bool do_grid = (getMapVal(command, "grid") == "grid");
  gPad->SetGrid(do_grid, do_grid);

  const string& mopt = getMapVal(command, "drawopt");
  if( mopt.find("colz") != string::npos )
    gPad->SetRightMargin(0.15);
}

//_____________________________________________________________________________
// Make a temporary canvas in batch mode for drawing images to be saved
static unique_ptr<TCanvas> MakeCanvas( const char* name = "c" )
{
  auto ww = gPad->GetWw(), hh = gPad->GetWh();
#if __cplusplus >= 201402L
  auto c = make_unique<TCanvas>(name, name, ww, hh);
#else
  auto c = unique_ptr<TCanvas>(
                new TCanvas(name, name, gPad->GetWw(), gPad->GetWh()));
#endif
  c->SetBatch();
  c->SetCanvasSize(ww, hh);
  return c;
}

//_____________________________________________________________________________
static int MakePlotsDir( const string& dir )
{
  if( !dir.empty() && gSystem->AccessPathName(dir.c_str()) ) {
    auto status = gSystem->mkdir(dir.c_str(), true);
    if( status ) {
      cerr << "ERROR:  Cannot create requested output directory "
           << dir << endl;
      return 1;
    }
  }
  return 0;
}

//_____________________________________________________________________________
// Get file basename without extension (erase starting from first '.')
static inline string StripExtension( string str )
{
  str = BasenameStr(str);
  auto pos = str.find('.');
  if( pos != string::npos )
    str.erase(pos);
  return str;
}

//_____________________________________________________________________________
// Substitute placeholders in file name 'str'. Used to construct plot and image
// file names
string OnlineGUI::SubstitutePlaceholders( string str, const string& var ) const
{
  ostringstream ostr;
  str = fConfig.SubstituteRunNumber(str, runNumber);
  // Config file name (less extension)
  str = ReplaceAll(
    str, "%C", StripExtension(fConfig.GetConfFileName()));
  // Histogram, tree variable or macro name (less extension)
  if( !var.empty() ) {
    str = ReplaceAll(str, "%V", StripExtension(var));
  }
  // Page number
  ostr.clear();
  ostr.str("");
  if( fConfig.GetPageNoWidth() > 0 )
    ostr << setw(fConfig.GetPageNoWidth()) << setfill('0');
  ostr << current_page + 1;
  str = ReplaceAll(str, "%P", ostr.str());
  // Pad number
  ostr.clear();
  ostr.str("");
  if( fConfig.GetPadNoWidth() > 0 )
    ostr << setw(fConfig.GetPadNoWidth()) << setfill('0');
  ostr << current_pad;
  str = ReplaceAll(str, "%D", ostr.str());
  // Plot and image formats
  str = ReplaceAll(str, "%E", fConfig.GetPlotFormat());
  str = ReplaceAll(str, "%F", fConfig.GetImageFormat());
  return str;
}

///////////////////////////////////////////////////////////////////
//  Class: OnlineGUI
//
//    Creates a GUI to display the commands used in OnlineConfig
//    unless batch mode is set, i.e. config.DoPrintOnly() == true
//    in which case the caller should invoke PrintPages().
//

OnlineGUI::OnlineGUI( OnlineConfig config )
  : fConfig{std::move(config)}
  , runNumber{0}
  , current_page{0}
  , current_pad{0}
  , doGolden{false}
  , fUpdate{false}
  , fFileAlive{false}
  , fVerbosity{fConfig.GetVerbosity()}
  , fPrintOnly{fConfig.DoPrintOnly()}
  , fSaveImages{fConfig.DoSaveImages()}
{
  // Constructor. Make the GUI.
  int bin2Dx(0), bin2Dy(0);
  fConfig.Get2DnumberBins(bin2Dx, bin2Dy);
  if( bin2Dx > 0 && bin2Dy > 0 ) {
    gEnv->SetValue("Hist.Binning.2D.x", bin2Dx);
    gEnv->SetValue("Hist.Binning.2D.y", bin2Dy);
    if( fVerbosity > 1 ) {
      cout << "Set 2D default bins to x, y: " << bin2Dx << ", " << bin2Dy << endl;
    }
  }

  if( PrepareRootFiles() )
    throw runtime_error("Error opening ROOT file");

  if( !fPrintOnly )
    CreateGUI(gClient->GetRoot(),
              UInt_t(fConfig.GetCanvasWidth() / 0.7),
              UInt_t(fConfig.GetCanvasHeight() / 0.9)
    );
}

void OnlineGUI::CreateGUI( const TGWindow* p, UInt_t w, UInt_t h )
{
  if( !fRootFile )
    throw runtime_error("No ROOT file");

  // Create the main frame
  fMain = new TGMainFrame(p, w, h);
  fMain->Connect("CloseWindow()", "OnlineGUI", this, "MyCloseWindow()");
  ULong_t lightgreen, lightblue, red, mainguicolor;
  gClient->GetColorByName("lightgreen", lightgreen);
  gClient->GetColorByName("lightblue", lightblue);
  gClient->GetColorByName("red", red);

  Bool_t good_color = kFALSE;
  TString usercolor = fConfig.GetGuiColor();
  if( !usercolor.IsNull() ) {
    good_color = gClient->GetColorByName(usercolor, mainguicolor);
  }

  if( !good_color ) {
    if( !usercolor.IsNull() ) {
      cout << "Bad guicolor (" << usercolor << ").. using default." << endl;
    }
    if( fConfig.IsMonitor() ) {
      // Default background color for Online Monitor
      mainguicolor = lightgreen;
    } else {
      // Default background color for Normal Online Display
      mainguicolor = lightblue;
    }
  }

  fMain->SetBackgroundColor(mainguicolor);

  // Top frame, to hold page buttons and canvas
  fTopframe = new TGHorizontalFrame(fMain, w, UInt_t(h * 0.9));
  fTopframe->SetBackgroundColor(mainguicolor);
  fMain->AddFrame(fTopframe, new TGLayoutHints(kLHintsExpandX
                                               | kLHintsExpandY, 10, 10, 10, 1));

  // Create a verticle frame widget
  //  This will hold the listbox
  vframe = new TGVerticalFrame(fTopframe, UInt_t(w * 0.3), UInt_t(h * 0.9));
  vframe->SetBackgroundColor(mainguicolor);
  current_page = 0;

  // Create the listbox that'll hold the list of pages
  fPageListBox = new TGListBox(vframe);
  fPageListBox->IntegralHeight(kTRUE);

  TString buff;
  for( Int_t i = 0; i < SINT(fConfig.GetPageCount()); ++i ) {
    buff = fConfig.GetPageTitle(i);
    fPageListBox->AddEntry(buff, i);
  }

  vframe->AddFrame(fPageListBox, new TGLayoutHints(kLHintsExpandX |
                                                   kLHintsCenterY, 5, 5, 3, 4));

  UInt_t maxsize = (fConfig.GetPageCount() + 1 > 30) ? 30 : fConfig.GetPageCount() + 1;
  fPageListBox->Resize(UInt_t(w * 0.15),
                       fPageListBox->GetItemVsize() * (maxsize));

  fPageListBox->Select(0);
  fPageListBox->Connect("Selected(Int_t)", "OnlineGUI", this,
                        "DoListBox(Int_t)");

  // heartbeat below the picture, watchfile only
  if( fConfig.IsMonitor() ) {
    fRootFileLastUpdated = new TGTextButton(vframe, "File updated at: XX:XX:XX");
    // fRootFileLastUpdated->SetWidth(156);
    vframe->AddFrame(fRootFileLastUpdated, new TGLayoutHints(kLHintsBottom | kLHintsRight, 5, 5, 3, 4));

    fLastUpdated = new TGTextButton(vframe, "Plots updated at: XX:XX:XX");
    // fLastUpdated->SetWidth(156);
    vframe->AddFrame(fLastUpdated, new TGLayoutHints(kLHintsBottom | kLHintsRight, 5, 5, 3, 4));

    fNow = new TGTextButton(vframe, "Current time: XX:XX:XX");
    // fNow->SetWidth(156);
    vframe->AddFrame(fNow, new TGLayoutHints(kLHintsBottom | kLHintsRight, 5, 5, 3, 4));
  }

  if( !fConfig.IsMonitor() ) {
    wile =
      new TGPictureButton(vframe, gClient->GetPicture((fConfig.GetGuiDirectory() + "/genius.xpm").c_str()));
    wile->Connect("Pressed()", "OnlineGUI", this, "DoDraw()");
  } else {
    wile =
      new TGPictureButton(vframe, gClient->GetPicture((fConfig.GetGuiDirectory() + "/panguin.xpm").c_str()));
    wile->Connect("Pressed()", "OnlineGUI", this, "DoDrawClear()");
  }
  wile->SetBackgroundColor(mainguicolor);

  vframe->AddFrame(wile, new TGLayoutHints(kLHintsBottom | kLHintsLeft, 5, 10, 4, 2));


  fTopframe->AddFrame(vframe, new TGLayoutHints(kLHintsLeft |
                                                kLHintsCenterY, 2, 2, 2, 2));

  // Create canvas widget
  fEcanvas = new TRootEmbeddedCanvas("Ecanvas", fTopframe, UInt_t(w * 0.7), UInt_t(h * 0.9));
  fEcanvas->SetBackgroundColor(mainguicolor);
  fTopframe->AddFrame(fEcanvas, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 10, 10, 10, 1));
  fCanvas = fEcanvas->GetCanvas();

  // Create the bottom frame.  Contains control buttons
  fBottomFrame = new TGHorizontalFrame(fMain, w, UInt_t(h * 0.1));
  fBottomFrame->SetBackgroundColor(mainguicolor);
  fMain->AddFrame(fBottomFrame, new TGLayoutHints(kLHintsExpandX, 10, 10, 10, 10));

  // Create a horizontal frame widget with buttons
  hframe = new TGHorizontalFrame(fBottomFrame, w, 40);
  hframe->SetBackgroundColor(mainguicolor);
  //fBottomFrame->AddFrame(hframe,new TGLayoutHints(kLHintsExpandX,200,20,2,2));
  fBottomFrame->AddFrame(hframe, new TGLayoutHints(kLHintsExpandX, 2, 2, 2, 2));

  fPrev = new TGTextButton(hframe, "Prev");
  fPrev->SetBackgroundColor(mainguicolor);
  fPrev->Connect("Clicked()", "OnlineGUI", this, "DrawPrev()");
  hframe->AddFrame(fPrev, new TGLayoutHints(kLHintsCenterX, 5, 5, 1, 1));

  fNext = new TGTextButton(hframe, "Next");
  fNext->SetBackgroundColor(mainguicolor);
  fNext->Connect("Clicked()", "OnlineGUI", this, "DrawNext()");
  hframe->AddFrame(fNext, new TGLayoutHints(kLHintsCenterX, 5, 5, 1, 1));

  fExit = new TGTextButton(hframe, "Exit GUI");
  fExit->SetBackgroundColor(red);
  fExit->Connect("Clicked()", "OnlineGUI", this, "CloseGUI()");

  hframe->AddFrame(fExit, new TGLayoutHints(kLHintsCenterX, 5, 5, 1, 1));

  TString Buff;
  if( runNumber == 0 ) {
    Buff = "";
  } else {
    Buff = "Run #";
    Buff += runNumber;
  }
  TGString labelBuff(Buff);

  fRunNumber = new TGLabel(hframe, Buff);
  fRunNumber->SetBackgroundColor(mainguicolor);
  hframe->AddFrame(fRunNumber, new TGLayoutHints(kLHintsCenterX, 5, 5, 1, 1));

  fPrint = new TGTextButton(hframe, "Print To &File");
  fPrint->SetBackgroundColor(mainguicolor);
  fPrint->Connect("Clicked()", "OnlineGUI", this, "PrintToFile()");
  hframe->AddFrame(fPrint, new TGLayoutHints(kLHintsCenterX, 5, 5, 1, 1));


  // Set a name to the main frame
  if( fConfig.IsMonitor() ) {
    fMain->SetWindowName("Parity ANalysis GUI moNitor");
  } else {
    fMain->SetWindowName("Online Analysis GUI");
  }

  // Map all sub windows to main frame
  fMain->MapSubwindows();

  // Initialize the layout algorithm
  fMain->Resize(fMain->GetDefaultSize());

  // Map main frame
  fMain->MapWindow();

  if( fVerbosity >= 1 )
    fMain->Print();

  if( fFileAlive )
    DoDraw();

  if( fConfig.IsMonitor() ) {
    timerNow = new TTimer();
    TTimer::Connect(timerNow, "Timeout()", "OnlineGUI", this, "UpdateCurrentTime()");
    timerNow->Start(1000);  // update every second

    timer = new TTimer();
    if( fFileAlive ) {
      TTimer::Connect(timer, "Timeout()", "OnlineGUI", this, "TimerUpdate()");
    } else {
      TTimer::Connect(timer, "Timeout()", "OnlineGUI", this, "CheckRootFile()");
    }
    timer->Start(UPDATETIME);
  }

}

void OnlineGUI::DoDraw()
{
  // The main Drawing Routine.

  if( fConfig.GetStyleFile().empty() ) {
    // Default style
    gStyle->SetOptStat(1110);
    //gStyle->SetStatFontSize(0.1);
    //   gStyle->SetTitleH(0.10);
    //   gStyle->SetTitleW(0.40);
    gStyle->SetTitleH(0.1);
    //  gStyle->SetTitleX(0.55);
    //gStyle->SetTitleW(0.6);
    //gStyle->SetTitleW(0.60);
    //gStyle->SetStatH(0.2);
    gStyle->SetStatW(0.25);
    gStyle->SetStatX(0.9);
    gStyle->SetStatY(0.88);
    //   gStyle->SetLabelSize(0.10,"X");
    //   gStyle->SetLabelSize(0.10,"Y");
    gStyle->SetLabelSize(0.05, "X");
    gStyle->SetLabelSize(0.05, "Y");
    gStyle->SetPadLeftMargin(0.15);
    gStyle->SetPadBottomMargin(0.08);
    gStyle->SetPadRightMargin(0.1);
    gStyle->SetPadTopMargin(0.12);
    // gStyle->SetNdivisions(505,"X");
    // gStyle->SetNdivisions(404,"Y");
    gStyle->SetNdivisions(505, "XYZ");
  } else {
    // Load up a user-configurable style
    gROOT->Macro(fConfig.GetStyleFile().c_str());
  }

  gStyle->SetOptLogx(fConfig.IsLogx(current_page) ? 1 : 0);
  gStyle->SetOptLogy(fConfig.IsLogy(current_page) ? 1 : 0);
  gStyle->SetOptLogz(fConfig.IsLogz(current_page) ? 1 : 0);

  TGaxis::SetMaxDigits(3);

  gROOT->ForceStyle();

  // Determine the dimensions of the canvas..
  UInt_t draw_count = fConfig.GetDrawCount(current_page);
  if( draw_count >= 8 ) {
    gStyle->SetLabelSize(0.08, "X");
    gStyle->SetLabelSize(0.08, "Y");
  }
  //   Int_t dim = Int_t(round(sqrt(double(draw_count))));
  Int_t nx, ny;
  std::tie(nx, ny) = fConfig.GetPageDim(current_page);

  if( fVerbosity >= 1 )
    cout << "Dimensions: " << nx << "X" << ny << endl;

  // Create a nice clean canvas.
  fCanvas->Clear();
  fCanvas->Divide(nx, ny);

  cmdmap_t drawcommand;
  //keys are "variable", "cut", "drawopt", "title", "treename", "grid", "nostat"

  // Draw the histograms.
  for( Int_t i = 0; i < SINT(draw_count); i++ ) {
    current_pad = i + 1;
    fConfig.GetDrawCommand(current_page, current_pad - 1, drawcommand);
    fCanvas->cd(current_pad);

    const string& cmd = getMapVal(drawcommand, "variable");
    if( !cmd.empty() ) {
      if( cmd == "macro" ) {
        SaveMacroImage(drawcommand);
        MacroDraw(drawcommand);
      } else if( cmd == "loadmacro" ) {
        LoadDraw(drawcommand);
      } else if( cmd == "loadlib" ) {
        LoadLib(drawcommand);
      } else if( IsHistogram(cmd) ) {
        HistDraw(drawcommand);
      } else {
        TreeDraw(drawcommand);
      }
    }
  }

  fCanvas->cd();
  fCanvas->Update();

  if( fConfig.IsMonitor() ) {
    char buffer[9]; // HH:MM:SS
    time_t t = time(nullptr);
    TString sLastUpdated("Plots updated at: ");
    strftime(buffer, 9, "%T", localtime(&t));
    sLastUpdated += buffer;
    fLastUpdated->SetText(sLastUpdated);

    struct stat result{};
    stat(fConfig.GetRootFile(), &result);
    time_t tf = result.st_mtime;
    strftime(buffer, 9, "%T", localtime(&tf));

    TString sRootFileLastUpdated("File updated at: ");
    sRootFileLastUpdated += buffer;
    fRootFileLastUpdated->SetText(sRootFileLastUpdated);
    ULong_t backgroundColor = fLastUpdated->GetBackground();
    if( fVerbosity >= 4 )
      cout << "Updating plots (current, file, diff[s]):\t"
           << t << "\t" << tf << "\t" << t - tf << endl;
    if( t - tf > 60 ) {
      ULong_t red;
      gClient->GetColorByName("red", red);
      fRootFileLastUpdated->SetBackgroundColor(red);
    } else {
      fRootFileLastUpdated->SetBackgroundColor(backgroundColor);
    }
  }

  if( !fPrintOnly ) {
    CheckPageButtons();
  }

}

void OnlineGUI::DrawNext()
{
  // Handler for the "Next" button.

  Int_t current_selection = fPageListBox->GetSelected();
  fPageListBox->Select(current_selection + 1);
  current_page = current_selection + 1;

  DoDraw();
}

void OnlineGUI::DrawPrev()
{
  // Handler for the "Prev" button.
  Int_t current_selection = fPageListBox->GetSelected();
  fPageListBox->Select(current_selection - 1);
  current_page = current_selection - 1;

  DoDraw();
}

void OnlineGUI::DoListBox( Int_t id )
{
  // Handle selection in the list box
  current_page = id;
  DoDraw();

}

void OnlineGUI::CheckPageButtons()
{
  // Checks the current page to see if it's the first or last page.
  //  If so... turn off the appropriate button.
  //  If not.. turn on both buttons.

  if( current_page == 0 ) {
    fPrev->SetState(kButtonDisabled);
    if( fConfig.GetPageCount() != 1 )
      fNext->SetState(kButtonUp);
  } else if( current_page == fConfig.GetPageCount() - 1 ) {
    fNext->SetState(kButtonDisabled);
    if( fConfig.GetPageCount() != 1 )
      fPrev->SetState(kButtonUp);
  } else {
    fPrev->SetState(kButtonUp);
    fNext->SetState(kButtonUp);
  }
}

Bool_t OnlineGUI::IsHistogram( const RootFileObj& fileObject )
{
  const auto& type = fileObject.type;
  return type.BeginsWith("TH") && type.Length() > 2 &&
         (type[2] == '1' || type[2] == '2' || type[2] == '3');
}

Bool_t OnlineGUI::IsHistogram( const TString& objectname ) const
{
  // Utility to determine if the objectname provided is a histogram

  RootFileObj target{objectname, "", ""};
  auto found = fileObjects.find(target);
  if( found == fileObjects.end() )
    return kFALSE;

  const auto& fileObject = *found;
  if( fVerbosity >= 2 )
    cout << fileObject.name << "      "
         << fileObject.type << endl;

  return IsHistogram(fileObject);
}

void OnlineGUI::ScanFileObjects( TIter& iter, const TString& directory ) // NOLINT(*-no-recursion)
{
  TKey* key;
  bool need_slash = !directory.IsNull() && !directory.EndsWith("/");
  while( (key = (TKey*) iter()) ) {
    TString objname = directory;
    if( need_slash ) objname.Append("/");
    objname.Append(key->GetName());
    TString objtype = key->GetClassName();
    TString objtitle = key->GetTitle();

    if( fVerbosity > 1 )
      cout << "Key = " << objname << " " << objtype << endl;

    if( !objtype.BeginsWith("TDirectory") ) {  // TDirectoryFile nowadays
      // Normal case
      auto success = fileObjects.insert(
        std::move(RootFileObj{std::move(objname), std::move(objtitle),
                                std::move(objtype)}));

    } else {
      // Subdirectory
      auto* thisdir = fRootFile->Get<TDirectory>(objname);
      if( !thisdir )
        continue;
      TIter dir_iter(thisdir->GetListOfKeys());
      ScanFileObjects(dir_iter, objname);
    }
  }
}

void OnlineGUI::GetFileObjects()
{
  // Utility to find all objects within a File (TTree, TH1F, etc).
  //  The pair stored in the vector is <ObjName, ObjType>
  //  For histograms, the title is also stored
  //    (in case the name is not very descriptive... like when
  //    using h2root)
  //  If there's no good keys.. do nothing.

  if( fRootFile->ReadKeys() == 0 ) {
    fUpdate = kFALSE;
    //     delete fRootFile;
    //     fRootFile = 0;
    //     CheckRootFile();
    return;
  }
  fileObjects.clear();
  auto* keylst = fRootFile->GetListOfKeys();
  if( !keylst || keylst->GetSize() == 0 ) {
    cerr << "Empty ROOT file. Can't make any plots \U0001F622" << endl;
    return;
  }
  TIter next(keylst);

  // Do the search
  ScanFileObjects(next, "");

  fUpdate = kTRUE;
}

void OnlineGUI::GetTreeVars()
{
  // Utility to find all variables (leaves/branches) within a
  // Specified TTree and put them within the treeVars vector.
  treeVars.clear();
  TObjArray* branchList;
  vector<TString> currentTree;

  for( const auto& tree: fRootTree ) {
    currentTree.clear();
    branchList = tree->GetListOfBranches();
    TIter next(branchList);
    TBranch* brc;

    while( (brc = (TBranch*) next()) ) {
      TString found = brc->GetName();
      // Not sure if the line below is so smart...
      currentTree.push_back(found);
    }
    treeVars.push_back(currentTree);
  }

  if( fVerbosity >= 5 ) {
    for( UInt_t iTree = 0; iTree < treeVars.size(); iTree++ ) {
      cout << "In Tree " << iTree << ": " << endl;
      for( const auto& var: treeVars[iTree] ) {
        cout << var << endl;
      }
    }
  }
}


void OnlineGUI::GetRootTree()
{
  // Utility to search a ROOT File for ROOT Trees
  // Fills the fRootTree vector
  fRootTree.clear();

  for( const auto& fileObject: fileObjects ) {

    if( fVerbosity >= 2 )
      cout << "Object = " << fileObject.type <<
           "     Name = " << fileObject.name << endl;

    if( fileObject.type == "TTree" ) {
      auto* tree = fRootFile->Get<TTree>(fileObject.name);
      if( tree )
        fRootTree.push_back(tree);
    }
  }

  // Initialize the fTreeEntries vector
  fTreeEntries.assign(fRootTree.size(), 0);
}

UInt_t OnlineGUI::GetTreeIndex( const TString& var )
{
  // Utility to find out which Tree (in fRootTree) has the specified
  // variable "var".  If the variable is a collection of Tree
  // variables (e.g. bcm1:lumi1), will only check the first
  // (e.g. bcm1).
  // Returns the correct index.  if not found returns an index 1
  // larger than fRootTree.size()

  //  This is for 2d draws... look for the first only
  string svar{var.Data()};
  auto pos = svar.find_first_of(":-/*+([");
  if( pos != string::npos )
    svar.erase(pos);

  if( fVerbosity >= 3 )
    cout << __PRETTY_FUNCTION__ << "\t" << __LINE__ << endl
         << "\t looking for variable: " << svar << endl;

  for( UInt_t iTree = 0; iTree < treeVars.size(); iTree++ ) {
    for( UInt_t ivar = 0; ivar < treeVars[iTree].size(); ivar++ ) {
      if( fVerbosity >= 4 )
        cout << "Checking tree " << iTree << " name:" << fRootTree[iTree]->GetName()
             << " \t var " << ivar << " >> " << treeVars[iTree][ivar] << endl;
      if( svar == treeVars[iTree][ivar] ) return iTree;
    }
  }

  return fRootTree.size() + 1;
}

UInt_t OnlineGUI::GetTreeIndexFromName( const TString& name )
{
  // Called by TreeDraw().  Tries to find the Tree index provided the
  //  name.  If it doesn't match up, return a number that's one larger
  //  than the number of found trees.
  for( UInt_t iTree = 0; iTree < fRootTree.size(); iTree++ ) {
    TString treename = fRootTree[iTree]->GetName();
    if( name == treename ) {
      return iTree;
    }
  }

  return fRootTree.size() + 1;
}

void OnlineGUI::MacroDraw( const cmdmap_t& command )
{
  // Called by DoDraw(), this will make a call to the defined macro, and
  //  plot it in its own pad.  One plot per macro, please.

  const string& macro = getMapVal(command, "macro");
  if( macro.empty() ) {
    cout << "macro command doesn't contain a macro to execute" << endl;
    return;
  }

  if( doGolden ) fRootFile->cd();
  gROOT->Macro(macro.c_str());
}

void OnlineGUI::LoadDraw( const cmdmap_t& command )
{
  // Called by DoDraw(), this will load a shared object library
  // and then make a call to the defined macro, and
  // plot it in its own pad.  One plot per macro, please.

  //  TString slib("library");
  //TString smacro("macro");

  const string& lib = getMapVal(command, "library");
  const string& mac = getMapVal(command, "macro");
  if( lib.empty() || mac.empty() ) {
    cout << "load command is missing either a shared library or macro command or both" << endl;
    return;
  }

  if( doGolden ) fRootFile->cd();
  gSystem->Load(lib.c_str());
  gROOT->Macro(mac.c_str());


}

void OnlineGUI::LoadLib( const cmdmap_t& command )
{
  // Called by DoDraw(), this will load a shared object library

  const string& lib = getMapVal(command, "library");
  if( lib.empty() ) {
    cout << "load command doesn't contain a shared object library path" << endl;
    return;
  }

  if( doGolden ) fRootFile->cd();
  gSystem->Load(lib.c_str());


}

void OnlineGUI::DoDrawClear()
{
  // Utility to grab the number of entries in each tree.  This info is
  // then used, if watching a file, to "clear" the TreeDraw
  // histograms, and begin looking at new data.
  for( UInt_t i = 0; i < fTreeEntries.size(); i++ ) {
    fTreeEntries[i] = (Int_t) fRootTree[i]->GetEntries();
  }
}

void OnlineGUI::TimerUpdate()
{
  // Called periodically by the timer, if "watchfile" is indicated
  // in the config.  Reloads the ROOT file, and updates the current page.
  if( fVerbosity >= 1 )
    cout << __PRETTY_FUNCTION__ << "\t" << __LINE__ << endl;

#ifdef OLDTIMERUPDATE
  if( fVerbosity >= 2 )
    cout << "\t rtFile: " << fRootFile << "\t" << fConfig.GetRootFile() << endl;
  if( fRootFile ) {
    fRootFile->Close();
    fRootFile->Delete();
    delete fRootFile;
    fRootFile = nullptr;
  }
  fRootFile = new TFile(fConfig.GetRootFile(), "READ");
  if( fRootFile->IsZombie() ) {
    cout << "New run not yet available.  Waiting..." << endl;
    fRootFile->Close();
    delete fRootFile;
    fRootFile = nullptr;
    timer->Reset();
    timer->Disconnect();
    TTimer::Connect(timer, "Timeout()", "OnlineGUI", this, "CheckRootFile()");
    return;
  }

  // Update the runnumber
  runNumber = fConfig.GetRunNumber();
  if( runNumber != 0 ) {
    TString rnBuff = "Run #";
    rnBuff += runNumber;
    fRunNumber->SetText(rnBuff.Data());
    hframe->Layout();
  }

  // Open the Root Trees.  Give a warning if it's not there.
  GetFileObjects();
  if( fUpdate ) { // Only do this stuff if there are valid keys
    GetRootTree();
    GetTreeVars();
    for( UInt_t i = 0; i < fRootTree.size(); i++ ) {
      if( !fRootTree[i] ) {
        fRootTree.erase(fRootTree.begin() + i);
      }
    }
    DoDraw();
  }
  timer->Reset();

#else

  if(fRootFile->IsZombie() || (fRootFile->GetSize() == -1)
     || (fRootFile->ReadKeys()==0)) {
    cout << "New run not yet available.  Waiting..." << endl;
    fRootFile->Close();
    delete fRootFile;
    fRootFile = 0;
    timer->Reset();
    timer->Disconnect();
    timer->Connect(timer,"Timeout()","OnlineGUI",this,"CheckRootFile()");
    return;
  }
  for(UInt_t i=0; i<fRootTree.size(); i++) {
    fRootTree[i]->Refresh();
  }
  DoDraw();
  timer->Reset();

#endif

}

void OnlineGUI::UpdateCurrentTime()
{
  char buffer[9];
  time_t t = time(nullptr);
  strftime(buffer, 9, "%T", localtime(&t));
  TString sNow("Current time: ");
  sNow += buffer;
  fNow->SetText(sNow);
  timerNow->Reset();
}

void OnlineGUI::BadDraw( const TString& errMessage )
{
  // Routine to display (in Pad) why a particular draw method has
  // failed.
  auto* pt = new TPaveText(0.1, 0.1, 0.9, 0.9, "brNDC");
  pt->SetBorderSize(3);
  pt->SetFillColor(10);
  pt->SetTextAlign(22);
  pt->SetTextFont(72);
  pt->SetTextColor(2);
  pt->AddText(errMessage.Data());
  pt->Draw();
  //   cout << errMessage << endl;
}


void OnlineGUI::CheckRootFile()
{
  // Check the path to the rootfile (should follow symbolic links)
  // ... If found:
  //   Reopen new root file,
  //   Reconnect the timer to TimerUpdate()

  if( gSystem->AccessPathName(fConfig.GetRootFile()) == 0 ) {
    cout << "Found the new run" << endl;
#ifndef OLDTIMERUPDATE
    if(OpenRootFile()==0) {
#endif
    timer->Reset();
    timer->Disconnect();
    TTimer::Connect(timer, "Timeout()", "OnlineGUI", this, "TimerUpdate()");
#ifndef OLDTIMERUPDATE
    }
#endif
  } else {
    TString rnBuff = "Waiting for run";
    fRunNumber->SetText(rnBuff.Data());
    hframe->Layout();
  }
}

Int_t OnlineGUI::PrepareRootFiles()
{
  // Open the RootFile. Die if it doesn't exist unless we're watching a file.
  // Also open GoldenFile. Warn if it doesn't exist.

  delete fRootFile; fRootFile = nullptr;
  delete fGoldenFile; fGoldenFile = nullptr;

  fRootFile = new TFile(fConfig.GetRootFile(), "READ");
  if( !fRootFile->IsOpen() ) {
    ostringstream ostr;
    ostr << "ERROR:  rootfile: " << fConfig.GetRootFile()
         << " cannot be opened";
    fFileAlive = kFALSE;
    if( !fPrintOnly && fConfig.IsMonitor() ) {
      cout << ostr.str() << endl;
      cout << "Will wait... hopefully.." << endl;
    } else {
      cerr << ostr.str() << endl;
      delete fRootFile;
      fRootFile = nullptr;
      return 1;
    }
  } else {
    fFileAlive = kTRUE;
    runNumber = fConfig.GetRunNumber();
    // Open the Root Trees.  Give a warning if it's not there..
    GetFileObjects();
    GetRootTree();
    GetTreeVars();
    for( UInt_t i = 0; i < fRootTree.size(); i++ ) {
      if( fRootTree[i] == nullptr ) {
        fRootTree.erase(fRootTree.begin() + i);
      }
    }
  }
  TString goldenfilename = fConfig.GetGoldenFile();
  if( !goldenfilename.IsNull() ) {
    fGoldenFile = new TFile(goldenfilename, "READ");
    doGolden = fGoldenFile->IsOpen();
    if( !doGolden ) {
      cerr << "ERROR: goldenrootfile: " << goldenfilename
           << " cannot be opened.  Oh well, no comparison plots." << endl;
      delete fGoldenFile;
      fGoldenFile = nullptr;
      if( fFileAlive )
        fRootFile->cd();
    }
  } else {
    doGolden = kFALSE;
    fGoldenFile = nullptr;
  }

  return 0;
}

Int_t OnlineGUI::OpenRootFile()
{
  fRootFile = new TFile(fConfig.GetRootFile(), "READ");
  if( fRootFile->IsZombie() || (fRootFile->GetSize() == -1)
      || (fRootFile->ReadKeys() == 0) ) {
    cout << "New run not yet available.  Waiting..." << endl;
    fRootFile->Close();
    delete fRootFile;
    fRootFile = nullptr;
    timer->Reset();
    timer->Disconnect();
    TTimer::Connect(timer, "Timeout()", "OnlineGUI", this, "CheckRootFile()");
    return -1;
  }

  // Update the runnumber
  runNumber = fConfig.GetRunNumber();
  if( runNumber != 0 ) {
    TString rnBuff = "Run #";
    rnBuff += runNumber;
    fRunNumber->SetText(rnBuff.Data());
    hframe->Layout();
  }

  // Open the Root Trees.  Give a warning if it's not there..
  GetFileObjects();
  if( fUpdate ) { // Only do this stuff if there are valid keys
    GetRootTree();
    GetTreeVars();
    for( UInt_t i = 0; i < fRootTree.size(); i++ ) {
      if( !fRootTree[i] ) {
        fRootTree.erase(fRootTree.begin() + i);
      }
    }
    DoDraw();
  } else {
    return -1;
  }
  return 0;

}

void OnlineGUI::SaveImage( TObject* o, const cmdmap_t& command ) const
{
  if( fSaveImages ) {
    const string& var = getMapVal(command, "variable");
    if( !var.empty() ) {
      auto c = MakeCanvas();
      SetupPad(command);
      const char* opt = getMapVal(command, "drawopt").c_str();
      o->Draw(opt);
      auto outfile = SubstitutePlaceholders(fConfig.GetProtoImageFile(), var);
      auto outdir = DirnameStr(outfile);
      if( MakePlotsDir(outdir) == 0 )
        c->SaveAs(outfile.c_str());
    }
  }
}

void OnlineGUI::SaveMacroImage( const cmdmap_t& drawcommand )
{
  if( fSaveImages ) {
    auto c = MakeCanvas();
    MacroDraw(drawcommand);
    auto outfile = SubstitutePlaceholders(
      fConfig.GetProtoMacroImageFile(), getMapVal(drawcommand, "macro"));
    auto outdir = DirnameStr(outfile);
    if( MakePlotsDir(outdir) == 0 )
      c->SaveAs(outfile.c_str());
    // Switch back to main canvas for subsequent MacroDraw call
    fCanvas->cd(current_pad);
  }
}

void OnlineGUI::HistDraw( const cmdmap_t& command )
{
  // Called by DoDraw(), this will plot a histogram.

  Bool_t showGolden = doGolden && command.find("noshowgolden") == command.end();

  TString drawopt = getMapVal(command, "drawopt");
  TString newtitle = getMapVal(command, "title");
  bool showstat = getMapVal(command, "nostat").empty();
  SetupPad(command);

  const string& var = getMapVal(command, "variable");
  assert(!var.empty());  // logic error in DoDraw()
  const char* cvar = var.c_str();
  RootFileObj target{cvar, "", ""};
//FIXME This search is redundant (IsHistogram)
  auto found = fileObjects.find(target);
  if( found == fileObjects.end() )
    return;
  const auto& fileObject = *found;
  assert(fileObject.type.BeginsWith("TH")); // logic error in IsHistogram

  // Determine dimensionality of histogram
  auto dim = fileObject.type[2]; // Histogram dimension ('1', '2', or '3')
  switch( dim ) {
    case '1':  // TH1
      if( showGolden )
        fRootFile->cd();
      mytemp1d = gDirectory->Get<TH1>(cvar);
      assert(mytemp1d);
      if( !mytemp1d || mytemp1d->GetEntries() == 0 ) {
        BadDraw("Empty Histogram");
        return;
      }
      if( showGolden ) {
        fGoldenFile->cd();
        mytemp1d_golden = gDirectory->Get<TH1>(cvar);
        assert(mytemp1d_golden);
//            if( !mytemp1d_golden ) break;
        mytemp1d_golden->SetLineColor(30);
        mytemp1d_golden->SetFillColor(30);
        Style_t fillstyle = fPrintOnly ? 3010 : 3027;
        mytemp1d_golden->SetFillStyle(fillstyle);
        mytemp1d_golden->SetStats(false);
        if( !newtitle.IsNull() )
          mytemp1d_golden->SetTitle(newtitle);
        mytemp1d_golden->Draw();
        mytemp1d->SetStats(showstat);
        if( !newtitle.IsNull() )
          mytemp1d->SetTitle(newtitle); // for SaveImage
        mytemp1d->Draw("sames" + drawopt);
      } else {
        mytemp1d->SetStats(showstat);
        if( newtitle != "" ) mytemp1d->SetTitle(newtitle);
        mytemp1d->Draw(drawopt);
      }
      SaveImage(mytemp1d, command);
      break;

    case '2':  // TH2
      if( showGolden )
        fRootFile->cd();
      mytemp2d = gDirectory->Get<TH2>(cvar);
      assert(mytemp2d);
      if( !mytemp2d || mytemp2d->GetEntries() == 0 ) {
        BadDraw("Empty Histogram");
        return;
      }
      // These are commented out because it usually doesn't make sense to
      // superimpose two 2d histos together
      // 	  if(showGolden) {
      // 	    fGoldenFile->cd();
      // 	    mytemp2d_golden = (TH2*)gDirectory->Get(cvar);
      // 	    mytemp2d_golden->SetMarkerColor(2);
      // 	    mytemp2d_golden->Draw();
      //mytemp2d->Draw("sames");
      // 	  } else {
//          if( drawopt.Contains("colz") ) {
//            gPad->SetRightMargin(0.15);
//          }

      if( !newtitle.IsNull() )
        mytemp2d->SetTitle(newtitle);
      mytemp2d->SetStats(showstat);
      mytemp2d->Draw(drawopt);
      SaveImage(mytemp2d, command);
      break;

    case '3':  // TH3
      if( showGolden )
        fRootFile->cd();
      mytemp3d = gDirectory->Get<TH3>(cvar);
      assert(mytemp3d);
      if( !mytemp3d || mytemp3d->GetEntries() == 0 ) {
        BadDraw("Empty Histogram");
        return;
      }
      mytemp3d->Draw();
      if( showGolden ) {
        fGoldenFile->cd();
        mytemp3d_golden = gDirectory->Get<TH3>(cvar);
        assert(mytemp3d_golden);
//            if( !mytemp3d_golden ) break;
        mytemp3d_golden->SetMarkerColor(2);
        mytemp3d_golden->Draw();
        mytemp3d->Draw("sames" + drawopt);
      } else {
        mytemp3d->Draw(drawopt);
      }
      SaveImage(mytemp3d, command);
      break;

    default:
      assert(false); // logic error in IsHistogram()
      break;
  }
}

void OnlineGUI::TreeDraw( const cmdmap_t& command )
{
  // Called by DoDraw(), this will plot a Tree Variable

  const string& mvar = getMapVal(command, "variable");
  TString var = mvar;

  //  Check to see if we're projecting to a specific histogram
  TString histoname = var(TRegexp(">>.+(?"));
  if( histoname.Length() > 0 ) {
    histoname.Remove(0, 2);
    Int_t bracketindex = histoname.First("(");
    if( bracketindex > 0 ) histoname.Remove(bracketindex);
    if( fVerbosity >= 3 )
      cout << histoname << " " << var(TRegexp(">>.+(?")) << endl;
  } else {
    histoname = "htemp";
  }

  // Combine the cuts (definecuts and specific cuts)
  TCut cut = "";
  const string& mcut = getMapVal(command, "cut");
  if( command.size() > 1 ) {
    TString tempCut = mcut;
    vector<string> cutIdents = fConfig.GetCutIdent();
    for( const auto& cutIdent: cutIdents ) {
      if( tempCut.Contains(cutIdent) ) {
        TString cut_found = fConfig.GetDefinedCut(cutIdent);
        tempCut.ReplaceAll(cutIdent, cut_found);
      }
    }
    cut = (TCut) tempCut;
  }

  // Determine which Tree the variable comes from, then draw it.
  UInt_t iTree;
  const string& mtree = getMapVal(command, "tree");
  if( mtree.empty() ) {
    iTree = GetTreeIndex(var);
    if( fVerbosity >= 2 )
      cout << "got index from variable " << iTree << endl;
  } else {
    iTree = GetTreeIndexFromName(mtree);
    if( fVerbosity >= 2 )
      cout << "got index from command " << iTree << endl;
  }

  const string& mopt = getMapVal(command, "drawopt");
  if( mopt.find("colz") != string::npos )
    gPad->SetRightMargin(0.15);
  string mtitle = getMapVal(command, "title");

  if( fVerbosity >= 3 )
    cout << "\tDraw option:" << mopt << " and histo name " << histoname << endl;
  if( iTree <= fRootTree.size() ) {
    if( fVerbosity >= 1 ) {
      cout << __PRETTY_FUNCTION__ << "\t" << __LINE__ << endl;
      cout << mvar << "\t"
           << mcut << "\t"
           << mopt << "\t"
           << mtitle << "\t"
           << mtree << endl;
      if( fVerbosity >= 2 )
        cout << "\tProcessing from tree: " << iTree << "\t" << fRootTree[iTree]->GetTitle() << "\t"
             << fRootTree[iTree]->GetName() << endl;
    }
    Long64_t nentries = fRootTree[iTree]->Draw(var, cut, mopt.c_str());
    if( getMapVal(command, "grid") == "grid" ) {
      gPad->SetGrid();
    }

    TObject* hobj = gROOT->FindObject(histoname);
    if( fVerbosity >= 3 )
      cout << "Finished drawing with return value " << nentries << endl;

    if( nentries == -1 ) {
      BadDraw(var + " not found");
    } else if( nentries != 0 ) {
      if( !mtitle.empty() ) {
        //  Generate a "unique" histogram name based on the MD5 of the drawn variable, cut, drawopt,
        //  and plot title.
        //  Makes it less likely to cause a name collision if two plot titles are the same.
        //  If you draw the exact same plot twice, the histograms will have the same name, but
        //  since they are exactly the same, you likely won't notice (or it will complain at you).
        TString tmpstring(var);
        tmpstring += cut.GetTitle();
        tmpstring += mopt;
        tmpstring += mtitle;
        TString myMD5 = tmpstring.MD5();
        TH1* thathist = (TH1*) hobj;
        thathist->SetNameTitle(myMD5, mtitle.c_str());
        SaveImage(thathist, command);
      }
    } else {
      BadDraw("Empty Histogram");
    }
  } else {
    BadDraw(var + " not found");
    if( fConfig.IsMonitor() ) {
      // Maybe we missed it... look again.  I don't like the code
      // below... maybe I can come up with something better
      GetFileObjects();
      GetRootTree();
      GetTreeVars();
    }
  }
}

void OnlineGUI::PrintToFile()
{
  // Routine to print the current page to a File.
  //  A file dialog pops up to request the file name.
  fCanvas = fEcanvas->GetCanvas();
  gStyle->SetPaperSize(20, 24);
  static TString dir("printouts");
  TGFileInfo fi;
  const char* myfiletypes[] =
    {"All files", "*",
     "Portable Document Format", "*.pdf",
     "PostScript files", "*.ps",
     "Encapsulated PostScript files", "*.eps",
     "GIF files", "*.gif",
     "JPG files", "*.jpg",
     nullptr, nullptr};
  fi.fFileTypes = myfiletypes;
  fi.fIniDir = StrDup(dir.Data());

  new TGFileDialog(gClient->GetRoot(), fMain, kFDSave, &fi);
  if( fi.fFilename )
    fCanvas->Print(fi.fFilename);
}

void OnlineGUI::PrintPages()
{
  // Routine to go through each defined page, and print the output to
  // a postscript file. (good for making sample histograms).

  if( !fRootFile )
    throw runtime_error("No ROOT file");

  fCanvas = new TCanvas("fCanvas", "Panguin Canvas",
                        fConfig.GetCanvasWidth(), fConfig.GetCanvasHeight());
  auto* lt = new TLatex();

  Bool_t pagePrint = kFALSE;
  TString printFormat = fConfig.GetPlotFormat();
  cout << "Plot Format = " << printFormat << endl;
  if( printFormat.IsNull() )
    printFormat = "pdf";
  else if( printFormat != "pdf" )
    pagePrint = kTRUE;

  string protofilename = pagePrint ? fConfig.GetProtoPlotPageFile()
                                   : fConfig.GetProtoPlotFile();
  TString filename;
  if( !pagePrint ) {
    filename = SubstitutePlaceholders(protofilename);
    auto outdir = DirnameStr(filename.Data());
    if( MakePlotsDir(outdir) )
      throw runtime_error("Bad directory name");
  }

  TString pagehead = "Summary Plots";
  if( runNumber != 0 ) {
    pagehead += "(Run #";
    pagehead += runNumber;
    pagehead += ")";
  }
  //  pagehead += ": ";

  gStyle->SetPalette(1);
  //gStyle->SetTitleX(0.15);
  //gStyle->SetTitleY(0.9);
  gStyle->SetPadBorderMode(0);
  //gStyle->SetHistLineColor(1);
  gStyle->SetHistFillStyle(0);
  if( !pagePrint )
    fCanvas->Print(filename + "[");
  for( Int_t i = 0; i < SINT(fConfig.GetPageCount()); i++ ) {
    current_page = i;
    DoDraw();
    TString pagename = pagehead;
    pagename += " ";
    pagename += i + 1;
    pagename += ": ";
    pagename += fConfig.GetPageTitle(current_page);
    lt->SetTextSize(0.025);
    lt->DrawLatex(0.05, 0.98, pagename);
    if( pagePrint ) {
      filename = SubstitutePlaceholders(protofilename);
      cout << "Printing page " << current_page + 1
           << " to file = " << filename << endl;
      auto outdir = DirnameStr(filename.Data());
      if( MakePlotsDir(outdir) )
        throw runtime_error("Bad directory name");
    }
    fCanvas->Print(filename);
  }
  if( !pagePrint )
    fCanvas->Print(filename + "]");

}

//_____________________________________________________________________________
template<typename PTR> inline void DelPtr( PTR*& p )
{
  delete p; p = nullptr;
}

//_____________________________________________________________________________
void OnlineGUI::DeleteGUI()
{
  DelPtr(timer);
  DelPtr(timerNow);
  DelPtr(fPrint);
  DelPtr(fExit);
  DelPtr(fRunNumber);
  DelPtr(fPrev);
  DelPtr(fNext);
  DelPtr(wile);
  DelPtr(fNow);
  DelPtr(fRootFileLastUpdated);
  DelPtr(fPageListBox);
  DelPtr(hframe);
  DelPtr(fEcanvas);
  DelPtr(vframe);
  DelPtr(fBottomFrame);
  DelPtr(fTopframe);
  DelPtr(fMain);
}

//_____________________________________________________________________________
void OnlineGUI::MyCloseWindow()
{
  cout << "OnlineGUI Closed." << endl;
  if( timer ) {
    timer->Stop();
  }
  DeleteGUI();

  gApplication->Terminate();
}

//_____________________________________________________________________________
void OnlineGUI::CloseGUI()
{
  // Routine to take care of the Exit GUI button
  fMain->SendCloseMessage();
}

//_____________________________________________________________________________
OnlineGUI::~OnlineGUI()
{
  if( timer )
    timer->Stop();
  if( fMain ) {
    fMain->SendCloseMessage();
    DeleteGUI();
  }
  DelPtr(fGoldenFile);
  DelPtr(fRootFile);
}
