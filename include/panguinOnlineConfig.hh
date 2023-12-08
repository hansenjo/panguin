#ifndef panguinOnlineConfig_h
#define panguinOnlineConfig_h

#include <utility>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <functional> // std::function

std::string DirnameStr( std::string path );
std::string BasenameStr( std::string path );

using uint_t = unsigned int;
using VecStr_t = std::vector<std::string>;

std::string ReplaceAll(
  std::string str, const std::string& ostr, const std::string& nstr );
bool EndsWith( const std::string& str, const std::string& tail );

// Class that takes care of the config file
class OnlineConfig {
  using strstr_t = std::pair<std::string, std::string>;
  using ConfLines_t = std::vector<VecStr_t>;
  using pagedim_t = std::pair<uint_t, uint_t>;

  std::string confFileName;       // config filename
  std::string fConfFileDir;       // Directory where config file found
  std::string fConfFilePath;      // Search path for configuration files
  std::string fRootFilesPath;     // Search path for ROOT files
  std::string rootfilename;       //  Just the name
  std::string goldenrootfilename; // Golden rootfile for comparisons
  std::string guicolor;           // User's choice of background color
  std::string fProtoPlotFile;
  std::string fProtoPlotPageFile;
  std::string fProtoImageFile;
  std::string fProtoMacroImageFile;
  std::string fPlotFormat;        // File format for saved plots (default: pdf)
  std::string fImageFormat;       // File format for saved image files (default: png)
  std::string fImagesDir;         // Where to save individual images
  std::string plotsdir;           // Where to save plots
  std::string fStyleFile;         // Plot style macro
  // the config file, in memory
  ConfLines_t sConfFile;
  VecStr_t    fProtoRootFiles; // Candidate ROOT file names

  // pageInfo is the vector of the pages containing the sConfFile index
  //   and how many commands issued within that page (title, 1d, etc.)

  // Bits for PageInfo::flags
  enum EPageFlags { kLogx = 1, kLogy = 2, kLogz = 4 };
  struct PageInfo {
    PageInfo() = default;
    PageInfo( uint_t pos, uint_t ncmd, uint_t ndraw )
      : page_index{pos}, cmd_count{ncmd}, draw_count{ndraw} {}
    pagedim_t get_dim() const { return std::make_pair(nx, ny); }
    void parse_newpage( const VecStr_t& pagedef, uint_t page );
    void set_title( const VecStr_t& titledef, uint_t page );
    void set_default_title( uint_t page );
    uint_t page_index{0};
    uint_t cmd_count{0};
    uint_t draw_count{0};
    uint_t nx{0};
    uint_t ny{0};
    uint_t flags{0};
    std::string title;
  };
  std::vector<PageInfo> pageInfo;

  std::vector<strstr_t> cutList;
  bool fFoundCfg;
  bool fMonitor;
  bool fPrintOnly;
  bool fSaveImages;
  bool fHallC;                   // Use Hall C defaults where applicable
  int fVerbosity;
  int hist2D_nBinsX, hist2D_nBinsY;
  int fRunNumber;
  int fRunNoWidth;
  int fPageNoWidth;
  int fPadNoWidth;
  int fCanvasWidth;
  int fCanvasHeight;

  int LoadFile( std::ifstream& infile, const std::string& filename );
  int CheckLoadIncludeFile( const std::string& sline,
                            const VecStr_t& strvect );
  ConfLines_t::iterator ParsePageInfo( ConfLines_t::iterator pos,
                                       ConfLines_t::iterator end );
  bool ParseForMultiPlots( ConfLines_t::iterator pos );
  std::vector<uint_t> GetDrawIndex( uint_t page ) const;

  struct CommandDef {
    CommandDef( std::string c, size_t n,
                std::function<void(const VecStr_t&)> a )
      : cmd(std::move(c)), narg(n), action(std::move(a)) {}
    std::string cmd;
    size_t narg = 0;
    std::function<void(const VecStr_t&)> action = nullptr;
  };
  static int ParseCommands( ConfLines_t::const_iterator pos,
                            ConfLines_t::const_iterator end,
                            const std::vector<CommandDef>& items );

public:
  struct CmdLineOpts {
    explicit CmdLineOpts(std::string f)
      : cfgfile(std::move(f))
    {}
    CmdLineOpts( std::string f, std::string d, std::string rf,
                 std::string gf, std::string rd, std::string pf,
                 std::string ifm, std::string pd, std::string id,
                 std::string sf, int rn, int v, bool po, bool si, bool hc )
      : cfgfile(std::move(f))
      , cfgdir(std::move(d))
      , rootfile(std::move(rf))
      , goldenfile(std::move(gf))
      , rootdir(std::move(rd))
      , plotfmt(std::move(pf))
      , imgfmt(std::move(ifm))
      , plotsdir(std::move(pd))
      , imgdir(std::move(id))
      , stylefile(std::move(sf))
      , run(rn)
      , verbosity(v)
      , printonly(po)
      , saveimages(si)
      , hallc(hc)
    {}
    std::string cfgfile;
    std::string cfgdir;
    std::string rootfile;
    std::string goldenfile;
    std::string rootdir;
    std::string plotfmt;
    std::string imgfmt;
    std::string plotsdir;
    std::string imgdir;
    std::string stylefile;
    int run{0};
    int verbosity{0};
    bool printonly{false};
    bool saveimages{false};
    bool hallc{false};
  };

  OnlineConfig();
  explicit OnlineConfig( const std::string& config_file_name );
  explicit OnlineConfig( const CmdLineOpts& opts );
  bool ParseConfig();
  int GetRunNumber() const { return fRunNumber; }
  std::string SubstituteRunNumber( std::string str, int runnumber ) const;

  const std::string& GetConfFilePath() const { return fConfFilePath; }
  const std::string& GetGuiDirectory() const { return fConfFileDir; }
  const std::string& GetConfFileName() const { return confFileName; }
  void Get2DnumberBins( int& nX, int& nY ) const
  {
    nX = hist2D_nBinsX;
    nY = hist2D_nBinsY;
  }
  void SetVerbosity( int ver ) { fVerbosity = ver; }
  const char* GetRootFile() const { return rootfilename.c_str(); };
  const char* GetGoldenFile() const { return goldenrootfilename.c_str(); };
  const std::string& GetGuiColor() const { return guicolor; };
  const std::string& GetProtoPlotFile() const { return fProtoPlotFile; }
  const std::string& GetProtoPlotPageFile() const { return fProtoPlotPageFile; }
  const std::string& GetProtoImageFile() const { return fProtoImageFile; }
  const std::string& GetProtoMacroImageFile() const { return fProtoMacroImageFile; }
  const std::string& GetPlotFormat() const { return fPlotFormat; }
  const std::string& GetImageFormat() const { return fImageFormat; }
  const std::string& GetPlotsDir() const { return plotsdir; };
  const std::string& GetImagesDir() const { return fImagesDir; };
  const std::string& GetStyleFile() const { return fStyleFile; };
  int GetVerbosity() const { return fVerbosity; }
  int GetRunNoWidth() const { return fRunNoWidth; }
  int GetPageNoWidth() const { return fPageNoWidth; }
  int GetPadNoWidth() const { return fPadNoWidth; }
  int GetCanvasWidth() const { return fCanvasWidth; };
  int GetCanvasHeight() const { return fCanvasHeight; };
  bool DoPrintOnly() const { return fPrintOnly; }
  bool DoSaveImages() const { return fSaveImages; }
  bool IsHallC() const { return fHallC; }
  bool IsMonitor() const { return fMonitor; };
  const std::string& GetDefinedCut( const std::string& ident ) const;
  VecStr_t GetCutIdent() const;
  void GetDrawCommand( uint_t page, uint_t nCommand,
                       std::map<std::string, std::string>& out_command ) const;
  void OverrideRootFile( int runnumber );
  // Page utilites
  uint_t GetPageCount() const { return pageInfo.size(); };
  pagedim_t GetPageDim( uint_t page ) const;
  std::string GetPageTitle( uint_t page ) const;
  uint_t GetDrawCount( uint_t page ) const;   // Number of histograms in a page
  bool IsLogx( uint_t page ) const;
  bool IsLogy( uint_t page ) const;
  bool IsLogz( uint_t page ) const;
};

#endif //panguinOnlineConfig_h
