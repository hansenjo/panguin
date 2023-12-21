///////////////////////////////////////////////////////////////////
//  Macro to help with online analysis
//    B. Moffit  Oct. 2003
#ifndef panguinOnline_h
#define panguinOnline_h 1

#include <TTree.h>
#include <TFile.h>
#include <TGButton.h>
#include <TGFrame.h>
#include <TGListBox.h>
#include <TRootEmbeddedCanvas.h>
#include "TGLabel.h"
#include "TGString.h"
#include <vector>
#include <string>
#include <map>
#include <set>
#include <TString.h>
#include <TCut.h>
#include <TTimer.h>
#include "TH1.h"
#include "TH2.h"
#include "TH3.h"
#include "panguinOnlineConfig.hh"

#define UPDATETIME 10000

class OnlineGUI {
  OnlineConfig fConfig;
  TGMainFrame* fMain = nullptr;
  TGHorizontalFrame* fTopframe = nullptr;
  TGVerticalFrame* vframe = nullptr;
  TGListBox* fPageListBox = nullptr;
  TGPictureButton* wile = nullptr;
  TGTextButton* fNow = nullptr; // current time
  TGTextButton* fLastUpdated = nullptr; // plots last updated
  TGTextButton* fRootFileLastUpdated = nullptr; // Root file last updated
  TRootEmbeddedCanvas* fEcanvas = nullptr;
  TGHorizontalFrame* fBottomFrame = nullptr;
  TGHorizontalFrame* hframe = nullptr;
  TGTextButton* fNext = nullptr;
  TGTextButton* fPrev = nullptr;
  TGTextButton* fExit = nullptr;
  TGLabel* fRunNumber = nullptr;
  TGTextButton* fPrint = nullptr;
  TCanvas* fCanvas = nullptr; // Present Embedded canvas
  TFile* fRootFile = nullptr;
  TFile* fGoldenFile = nullptr;
  TTimer* timer = nullptr;
  TTimer* timerNow = nullptr; // used to update time
  TH1* mytemp1d = nullptr;
  TH2* mytemp2d = nullptr;
  TH3* mytemp3d = nullptr;
  TH1* mytemp1d_golden = nullptr;
  //TH2* mytemp2d_golden = nullptr;
  TH3* mytemp3d_golden = nullptr;
  Int_t current_page;
  Int_t current_pad;
  Int_t runNumber;
  Int_t fVerbosity;
  Bool_t doGolden;
  Bool_t fUpdate;
  Bool_t fFileAlive;
  Bool_t fPrintOnly;
  Bool_t fSaveImages;

  struct RootFileObj {
    // For ordering std::set
    bool operator<(const RootFileObj& other ) const {
      return name < other.name;
    }
    TString name;   // Full path to object (dir/objname)
    TString title;  // Object title
    TString type;   // Object class name
  };
  std::vector<TTree*> fRootTree;
  std::vector<Int_t> fTreeEntries;
  std::set<RootFileObj> fileObjects;
  std::vector<std::vector<TString> > treeVars;

  using cmdmap_t = std::map<std::string, std::string>;

  static void BadDraw( const TString& );
  void CheckPageButtons();
  void CreateGUI( const TGWindow* p, UInt_t w, UInt_t h );
  void DeleteGUI();
  UInt_t GetFileObjects();
  void GetRootTree();
  UInt_t GetTreeIndex( TString var );
  UInt_t GetTreeIndexFromName( const TString& );
  UInt_t GetTreeVars();
  void HistDraw( const cmdmap_t& command );
  Bool_t IsHistogram( const TString& objectname ) const;
  static Bool_t IsHistogram( const RootFileObj& fileObject );
  void LoadDraw( const cmdmap_t& command );
  void LoadLib( const cmdmap_t& command );
  void MacroDraw( const cmdmap_t& command );
  void SaveImage( TObject* o, const cmdmap_t& command ) const;
  void SaveMacroImage( const cmdmap_t& drawcommand );
  UInt_t ScanFileObjects( TIter& iter, TString directory );
  static void SetupPad( const cmdmap_t& command );
  std::string SubstitutePlaceholders(
    std::string str, const std::string& var = std::string() ) const;
  void TreeDraw( const cmdmap_t& command );
  Int_t OpenRootFile();
  Int_t PrepareRootFiles();
  static void Print( const RootFileObj& fobj, int typew, int namew,
                     bool do_title = true );

public:
  OnlineGUI();
  explicit OnlineGUI( OnlineConfig config );
  ~OnlineGUI();
  void InspectRootFile( const std::string& scanfile );
  Bool_t IsPrintOnly() const { return fPrintOnly; }
  void PrintPages();
  void SetVerbosity( int ver ) { fVerbosity = ver; }

  // GUI callbacks, must be public
  void CheckRootFile();
  void CloseGUI();
  void DoDraw();
  void DoDrawClear();
  void DoListBox( Int_t id );
  void DrawNext();
  void DrawPrev();
  void MyCloseWindow();
  void PrintToFile();
  void TimerUpdate();
  void UpdateCurrentTime();  // update current time

  ClassDefNV(OnlineGUI, 0)
};

#endif //panguinOnline_h
