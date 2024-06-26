#ifndef CEMC_CEMCMONDRAW_H
#define CEMC_CEMCMONDRAW_H

#include <onlmon/OnlMonDraw.h>

#include <TH2.h>
#include <TStyle.h>
#include <string>  // for allocator, string

class TCanvas;
class TGraphErrors;
class TPad;
class TProfile;

class CemcMonDraw : public OnlMonDraw
{
 public:
  explicit CemcMonDraw(const std::string &name = "CEMCMON"); // same name as server!

  ~CemcMonDraw() override {}

  int Init() override;
  int Draw(const std::string &what = "ALL") override;
  int MakeHtml(const std::string &what = "ALL") override;
  int SavePlot(const std::string &what = "ALL", const std::string &type = "png") override;
  void setSave(int s) {save = s;}
  void HandleEvent(int event, int x, int y, TObject* sel);

 private:
  int MakeCanvas(const std::string &name);
  int DrawFirst(const std::string &what = "ALL");
  int DrawSecond(const std::string &what = "ALL");
  int DrawThird(const std::string &what = "ALL");
  int DrawFourth(const std::string &what = "ALL");
  int DrawFifth(const std::string &what = "ALL");
  int DrawSixth(const std::string &what = "ALL");
  int DrawSeventh(const std::string &what = "ALL");
  int DrawHistory(const std::string &what = "ALL");
  int DrawServerStats();

  int FindHotTower(TPad *warn, TH2 *);
  time_t getTime();
  std::vector<int>  getBadPackets(TH1 *hist, int what, float cutoff);

  const int nTowersEta{96};
  const int nTowersPhi{256};
  const int templateDepth{10000};
  const double SampleLowBoundary{4.5};
  const double SampleHighBoundary{7.5};
  int save{0};

  TCanvas *TC[19]{nullptr};
  TCanvas *PopUpCanvas{nullptr};
  TPad *transparent[19]{nullptr};
  TPad *Pad[29]{nullptr};
  TPad *warning[29]{nullptr};

  TPad *PopUpPad[8][8] {{nullptr}};
  TPad *PopUpTransparent{nullptr};
  TGraphErrors *gr[2]{nullptr};
  TProfile *summedProfile[8][8]{{nullptr}};
  TProfile *AllProfiles[256][96]{{nullptr}};
  TStyle *cemcStyle{nullptr};
};

#endif /* CEMC_CEMCMONDRAW_H */
