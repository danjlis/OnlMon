// use #include "" only for your local include and put
// those in the first line(s) before any #include <>
// otherwise you are asking for weird behavior
// (more info - check the difference in include path search when using "" versus <>)

#include "CemcMon.h"

#include <onlmon/OnlMon.h>  // for OnlMon
#include <onlmon/OnlMonServer.h>
#include <onlmon/pseudoRunningMean.h>

#include <calobase/TowerInfoDefs.h>
#include <caloreco/CaloWaveformFitting.h>

#include <Event/Event.h>
#include <Event/EventTypes.h>
#include <Event/eventReceiverClient.h>
#include <Event/msg_profile.h>

#include <TH1.h>
#include <TH2.h>
#include <TProfile.h>
#include <TProfile2D.h>

#include <cmath>
#include <cstdio>  // for printf
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>  // for allocator, string, char_traits

enum
{
  TRGMESSAGE = 1,
  FILLMESSAGE = 2
};

const int depth = 10000;
// const int historyLength = 100;
const float hit_threshold = 100;

using namespace std;

CemcMon::CemcMon(const std::string &name)
  : OnlMon(name)
  , eventCounter(0)
{
  // leave ctor fairly empty, its hard to debug if code crashes already
  // during a new CemcMon()

  return;
}

CemcMon::~CemcMon()
{
  for (auto iter : rm_vector_sectAvg)
  {
    delete iter;
  }
  for (auto iter : rm_vector_twr)
  {
    delete iter;
  }
  for (auto iter : rm_vector)
  {
    delete iter;
  }
  delete WaveformProcessingFast;
  delete WaveformProcessingTemp;
  delete erc;
  return;
}

int CemcMon::Init()
{
  // read our calibrations from CemcMonData.dat
  const char *cemccalib = getenv("CEMCCALIB");
  if (!cemccalib)
  {
    std::cout << "CEMCCALIB environment variable not set" << std::endl;
    exit(1);
  }
  std::string fullfile = std::string(cemccalib) + "/" + "CemcMonData.dat";
  std::ifstream calib(fullfile);
  calib.close();
  // use printf for stuff which should go the screen but not into the message
  // system (all couts are redirected)
  printf("CemcMon::Init()\n");
  // Histograms definitions
  // Trigger histograms
  for(int itrig = 0; itrig < 64; itrig++)
  {
    h2_cemc_hits_trig[itrig] = new TH2F(Form("h2_cemc_hits_trig_bit_%d", itrig), "", 96, 0, 96, 256, 0, 256);
  }
  p2_zsFrac_etaphi   = new TProfile2D("p2_zsFrac_etaphi","",96,0,96,256,0,256);
  h1_cemc_trig = new TH1F("h1_cemc_trig", "", 64, -0.5, 63.5);
  h1_packet_event = new TH1F("h1_packet_event", "", 8, packetlow - 0.5, packethigh + 0.5);
  h2_caloPack_gl1_clock_diff = new TH2F("h2_caloPack_gl1_clock_diff", "", 8, packetlow - 0.5, packethigh + 0.5, 65536, 0, 65536);
  h_evtRec = new TProfile("h_evtRec", "", 1, 0, 1);

  // tower hit information
  h2_cemc_hits = new TH2F("h2_cemc_hits", "", 96, 0, 96, 256, 0, 256);
  h2_cemc_rm = new TH2F("h2_cemc_rm", "", 96, 0, 96, 256, 0, 256);
  h2_cemc_mean = new TH2F("h2_cemc_mean", "", 96, 0, 96, 256, 0, 256);
  h1_cemc_adc = new TH1F("h1_cemc_adc", "", 1000, 0.5, pow(2, 14));
  // event couunter
  h1_event = new TH1F("h1_event", "", 1, 0, 1);

  // waveform processing
  // h2_waveform_twrAvg = new TH2F("h2_waveform_twrAvg", "", 16, 0.5, 16.5, 10000,0,pow(2,14));
  h2_waveform_twrAvg = new TH2F("h2_waveform_twrAvg", "", 12, -0.5, 11.5, 1000, 0, 15000);
  h1_waveform_time = new TH1F("h1_waveform_time", "", 12, -0.5, 11.5);
  h1_waveform_pedestal = new TH1F("h1_waveform_pedestal", "", 25, 1.3e3, 2.0e3);

  // waveform processing, template vs. fast interpolation
  h1_cemc_fitting_sigDiff = new TH1F("h1_fitting_sigDiff", "", 50, 0, 2);
  h1_cemc_fitting_pedDiff = new TH1F("h1_fitting_pedDiff", "", 50, 0, 2);
  h1_cemc_fitting_timeDiff = new TH1F("h1_fitting_timeDiff", "", 50, -10, 10);

  //

  // h1_sectorAvg_total = new TH1F("h1_sectorAvg_total", "", 32, 0.5, 32.5);

  // packet information
  h1_packet_number = new TH1F("h1_packet_number", "", 128, 6000.5, 6128.5);
  h1_packet_length = new TH1F("h1_packet_length", "", 128, 6000.5, 6128.5);
  h1_packet_chans = new TH1F("h1_packet_chans", "", 128, 6000.5, 6128.5);

  // for (int ih = 0; ih < Nsector; ih++)
  // h1_rm_sectorAvg[ih] = new TH1F(Form("h1_rm_sectorAvg_s%d", ih), "", historyLength, 0, historyLength);

  // make the per-packet running mean objects
  // 32 packets and 48 channels for hcal detectors
  for (int i = 0; i < Nsector; i++)
  {
    rm_vector_sectAvg.push_back(new pseudoRunningMean(1, depth));
  }
  for (int i = 0; i < Ntower; i++)
  {
    rm_vector_twr.push_back(new pseudoRunningMean(1, depth));
  }

  std::string h_id = "cemc_occupancy";
  cemc_occupancy = new TH2F(h_id.c_str(), "cemc_occupancy plot", 48 * 2, -48, 48, 32 * 8, -0.5, 255.5);
  cemc_occupancy->GetXaxis()->SetTitle("eta");
  cemc_occupancy->GetYaxis()->SetTitle("phi");
  cemc_occupancy->SetStats(false);
  // cemc_occupancy->SetMinimum(0);
  //   cemc_occupancy->SetMaximum(1200);

  h_id = "cemc_runningmean";
  cemc_runningmean = new TH2F(h_id.c_str(), "Cemc Running Mean Run 0 Event 0", 48 * 2, -48, 48, 32 * 8, -0.5, 255.5);
  cemc_runningmean->GetXaxis()->SetTitle("eta");
  cemc_runningmean->GetYaxis()->SetTitle("phi");
  cemc_runningmean->SetStats(false);
  cemc_runningmean->SetMinimum(0);
  cemc_runningmean->SetMaximum(700);

  cemc_signal = new TH1F("cemc_signal", "Signal Distribution", 512, -200., 4000);

  //  cemchist2 = new TH2F("cemcmon_hist2", "test 2d histo", 101, 0., 100., 101, 0., 100.);
  OnlMonServer *se = OnlMonServer::instance();
  // register histograms with server otherwise client won't get them
  se->registerHisto(this, cemc_occupancy);    // uses the TH1->GetName() as key
  se->registerHisto(this, cemc_runningmean);  // uses the TH1->GetName() as key

  // Trigger histograms
  for(int itrig = 0; itrig < 64; itrig++)
  {
    se->registerHisto(this, h2_cemc_hits_trig[itrig]);
  }
  se->registerHisto(this, p2_zsFrac_etaphi);
  se->registerHisto(this, h1_cemc_trig);
  se->registerHisto(this, h1_packet_event);
  se->registerHisto(this, h2_caloPack_gl1_clock_diff);
  se->registerHisto(this, h_evtRec);

  se->registerHisto(this, h2_cemc_hits);
  se->registerHisto(this, h2_cemc_rm);
  se->registerHisto(this, h2_cemc_mean);
  se->registerHisto(this, h1_event);
  // se->registerHisto(this, h1_sectorAvg_total);
  se->registerHisto(this, h2_waveform_twrAvg);
  se->registerHisto(this, h1_waveform_time);
  se->registerHisto(this, h1_waveform_pedestal);
  se->registerHisto(this, h1_cemc_fitting_sigDiff);
  se->registerHisto(this, h1_cemc_fitting_pedDiff);
  se->registerHisto(this, h1_cemc_fitting_timeDiff);
  se->registerHisto(this, h1_packet_number);
  se->registerHisto(this, h1_packet_length);
  se->registerHisto(this, h1_packet_chans);
  se->registerHisto(this, h1_cemc_adc);

  // for (int ih = 0; ih < Nsector; ih++)
  //   {
  //     se->registerHisto(this, h1_rm_sectorAvg[ih]);
  //   }

  h2_waveform=new TProfile**[nPhiIndex];
  for(int iphi=0; iphi<nPhiIndex; iphi++){
    h2_waveform[iphi]=new TProfile*[nEtaIndex];
    for(int ieta=0; ieta<nEtaIndex; ieta++){
      h2_waveform[iphi][ieta]=new TProfile(Form("h2_waveform_phi%d_eta%d",iphi,ieta),Form("Profiled raw waveform for #phi %d and #eta %d",iphi,ieta),12, -0.5, 11.5, "s");
      h2_waveform[iphi][ieta]->GetXaxis()->SetTitle("sample #");
      h2_waveform[iphi][ieta]->GetYaxis()->SetTitle("ADC counts");
      h2_waveform[iphi][ieta]->SetStats(false);
      se->registerHisto(this, (TH1*)h2_waveform[iphi][ieta]);
    }
  }

  // make the per-packet runnumg mean objects
  for (int i = 0; i < 64; i++)
  {
    rm_vector.push_back(new pseudoRunningMean(192, 50));
  }

  // initialize waveform extraction tool
  WaveformProcessingFast = new CaloWaveformFitting();

  WaveformProcessingTemp = new CaloWaveformFitting();

  std::string cemctemplate;
  if (getenv("CEMCCALIB"))
  {
    cemctemplate = getenv("CEMCCALIB");
  }
  else
  {
    cemctemplate = ".";
  }
  cemctemplate += std::string("/testbeam_cemc_template.root");
  WaveformProcessingTemp->initialize_processing(cemctemplate);

  if (anaGL1)
  {
    erc = new eventReceiverClient("gl1daq");
  }

  return 0;
}

int CemcMon::BeginRun(const int /* runno */)
{
  // if you need to read calibrations on a run by run basis
  // this is the place to do it

  // reset the running means
  std::vector<runningMean *>::iterator rm_it;
  for (rm_it = rm_vector.begin(); rm_it != rm_vector.end(); ++rm_it)
  {
    (*rm_it)->Reset();
  }
  for (rm_it = rm_vector_twr.begin(); rm_it != rm_vector_twr.end(); ++rm_it)
  {
    (*rm_it)->Reset();
  }

  return 0;
}

// simple wavefrom analysis for possibe issues with the wavforProcessor
std::vector<float> CemcMon::getSignal(Packet *p, const int channel)
{
  double baseline = 0;
  for (int s = 0; s < 3; s++)
  {
    baseline += p->iValue(s, channel);
  }
  baseline /= 3.;

  double signal = 0;
  float x = 0;
  for (int s = 3; s < p->iValue(0, "SAMPLES"); s++)
  {
    x++;
    signal += p->iValue(s, channel) - baseline;
  }

  signal /= x;

  std::vector<float> result;
  result.push_back(signal);
  result.push_back(2);
  result.push_back(1);
  return result;
}

std::vector<float> CemcMon::anaWaveformFast(Packet *p, const int channel)
{
  std::vector<float> waveform;

  //int nSamples = p->iValue(0, "SAMPLES");
  //waveform.reserve(nSamples);
  if (p->iValue(channel, "SUPPRESSED"))
    {
      waveform.push_back(p->iValue(channel, "PRE"));
      waveform.push_back(p->iValue(channel, "POST"));
    }
  else
    {
      int nSamples = p->iValue(0, "SAMPLES");
      for (int s = 0; s < nSamples; s++)
	{
	  waveform.push_back(p->iValue(s, channel));
	}
    }
  
  std::vector<std::vector<float>> multiple_wfs;
  multiple_wfs.push_back(waveform);
  
  std::vector<std::vector<float>> fitresults_cemc;
  fitresults_cemc = WaveformProcessingFast->calo_processing_fast(multiple_wfs);

  std::vector<float> result;
  result = fitresults_cemc.at(0);
  return result;
}

std::vector<float> CemcMon::anaWaveformTemp(Packet *p, const int channel)
{
  std::vector<float> waveform;

  //waveform.reserve(p->iValue(0, "SAMPLES"));
  if (p->iValue(channel, "SUPPRESSED"))
    {
      waveform.push_back(p->iValue(channel, "PRE"));
      waveform.push_back(p->iValue(channel, "POST"));
    }
  else
    {
      for (int s = 0; s < p->iValue(0, "SAMPLES"); s++)
	{
	  waveform.push_back(p->iValue(s, channel));
	}
    }
  std::vector<std::vector<float>> multiple_wfs;
  multiple_wfs.push_back(waveform);

  std::vector<std::vector<float>> fitresults_cemc;
  fitresults_cemc = WaveformProcessingTemp->process_waveform(multiple_wfs);

  std::vector<float> result;
  result = fitresults_cemc.at(0);
  return result;
}

int CemcMon::process_event(Event *e /* evt */)
{
  float sectorAvg[Nsector] = {0};
  unsigned int towerNumber = 0;

  std::vector<bool> trig_bools;
  trig_bools.resize(64);
  long long int gl1_clock = 0;
  bool have_gl1 = false;
  if (anaGL1)
  {
    int evtnr = e->getEvtSequence();
    Event *gl1Event = erc->getEvent(evtnr);
    if (gl1Event)
    {
      have_gl1 = true;
      Packet *p = gl1Event->getPacket(14001);
      h_evtRec->Fill(0.0, 1.0);
      if (p)
      {
        gl1_clock = p->lValue(0, "BCO");
        uint64_t triggervec = p->lValue(0, "TriggerVector");
        for (int i = 0; i < 64; i++)
        {
          bool trig_decision = ((triggervec & 0x1U) == 0x1U);
          trig_bools[i] = trig_decision;
          if (trig_decision)
          {
            h1_cemc_trig->Fill(i);
          }
          triggervec = (triggervec >> 1U) & 0xffffffffU;
        }
        delete p;
      }
      delete gl1Event;
    }
    else
    {
      std::cout << "GL1 event is null" << std::endl;
      h_evtRec->Fill(0.0, 0.0);
    }
  }

  // loop over packets which contain a single sector
  eventCounter++;
  for (int packet = packetlow; packet <= packethigh; packet++)
  {
    Packet *p = e->getPacket(packet);

    if (p)
    {
      h1_packet_number->Fill(packet);
      h1_packet_length->SetBinContent(packet - 6000, h1_packet_length->GetBinContent(packet - 6000) + p->getLength());


      h1_packet_event->SetBinContent(packet - 6000, p->lValue(0, "CLOCK"));

      if (have_gl1)
      {
	long long int p_clock = p->lValue(0, "CLOCK");
	long long int diff = (p_clock - gl1_clock) % 65536;
	h2_caloPack_gl1_clock_diff->Fill(packet, diff);
      }
      int nChannels = p->iValue(0, "CHANNELS");
      if (nChannels > m_nChannels)
      {
        return -1;  // packet is corrupted, reports too many channels
      }
      for (int c = 0; c < nChannels; c++)
      {
        h1_packet_chans->Fill(packet);

        towerNumber++;
        // channel mapping
        unsigned int key = TowerInfoDefs::encode_emcal(towerNumber - 1);
        unsigned int phi_bin = TowerInfoDefs::getCaloTowerPhiBin(key);
        unsigned int eta_bin = TowerInfoDefs::getCaloTowerEtaBin(key);

	for (int s = 0; s < p->iValue(0, "SAMPLES"); s++)
	  {
	    h2_waveform[phi_bin][eta_bin]->Fill(s,p->iValue(s,c));//for the moment only for good packet and with signal (potentially also bad packet later, not sure for zero suppressed)
	  }
	////Uninstrumented area
	//if ((packet==6019)||(packet==6073)){
	//  if(c>63&&c<128) continue;
	//}
	//if (packet==6030){
	//  if(c>127)continue;
	//}

        std::vector<float> resultFast = anaWaveformFast(p, c);  // fast waveform fitting
        float signalFast = resultFast.at(0);
        float timeFast = resultFast.at(1);
        float pedestalFast = resultFast.at(2);

	if(p->iValue(c,"SUPPRESSED")){
	  p2_zsFrac_etaphi->Fill(eta_bin,phi_bin,0);
	}
	else{
	  p2_zsFrac_etaphi->Fill(eta_bin,phi_bin,1);
	}
	  
	
        h1_waveform_pedestal->Fill(pedestalFast);

        if (signalFast < hit_threshold)
        {
          continue;
        }

        for (int s = 0; s < p->iValue(0, "SAMPLES"); s++)
        {
          h2_waveform_twrAvg->Fill(s, p->iValue(s, c) - pedestalFast);
        }


        h1_waveform_time->Fill(timeFast);

        int bin = h2_cemc_mean->FindBin(eta_bin + 0.5, phi_bin + 0.5);

        rm_vector_twr[towerNumber - 1]->Add(&signalFast);

        h2_cemc_rm->SetBinContent(bin, rm_vector_twr[towerNumber - 1]->getMean(0));

        // create beginning of run template
        if (eventCounter < templateDepth)
        {
          h2_cemc_mean->SetBinContent(bin, h2_cemc_mean->GetBinContent(bin) + signalFast);
        }

        h1_cemc_adc->Fill(signalFast);

        if (!((eventCounter - 2) % 10000))
        {
          std::vector<float> resultTemp = anaWaveformTemp(p, c);  // template waveform fitting
          float signalTemp = resultTemp.at(0);
          float timeTemp = resultTemp.at(1);
          float pedestalTemp = resultTemp.at(2);
          h1_cemc_fitting_sigDiff->Fill(signalFast / signalTemp);
          h1_cemc_fitting_pedDiff->Fill(pedestalFast / pedestalTemp);
          h1_cemc_fitting_timeDiff->Fill(timeFast - timeTemp - 6);
        }
        if (signalFast > hit_threshold)
        {
          h2_cemc_hits->SetBinContent(bin, h2_cemc_hits->GetBinContent(bin) + signalFast);
	  if (have_gl1)
	  {
	    for(int itrig = 0; itrig < 64; itrig++)
	    {
	      if(trig_bools[itrig])
	      {
		h2_cemc_hits_trig[itrig]->Fill(eta_bin + 0.5, phi_bin + 0.5);
	      }
	    }
	  }
        }
      }  // channel loop
      if (nChannels < m_nChannels)
      {
        // still need to correctly set bad channels to zero.
        for (int channel = 0; channel < m_nChannels - nChannels; channel++)
        {
          towerNumber++;

          unsigned int key = TowerInfoDefs::encode_emcal(towerNumber - 1);
          unsigned int phi_bin = TowerInfoDefs::getCaloTowerPhiBin(key);
          unsigned int eta_bin = TowerInfoDefs::getCaloTowerEtaBin(key);

          int sectorNumber = phi_bin / 8 + 1;

          int bin = h2_cemc_mean->FindBin(eta_bin + 0.5, phi_bin + 0.5);

          sectorAvg[sectorNumber - 1] += 0.;

          float signalFast = 0.0;

          rm_vector_twr[towerNumber - 1]->Add(&signalFast);

          h2_cemc_rm->SetBinContent(bin, rm_vector_twr[towerNumber - 1]->getMean(0));

          h2_cemc_mean->SetBinContent(bin, h2_cemc_mean->GetBinContent(bin));
        }
      }
      delete p;
    }     // if packet good
    else  // packet is corrupted, treat all channels as zero suppressed
    {
      for (int channel = 0; channel < m_nChannels; channel++)
      {
        towerNumber++;
        unsigned int key = TowerInfoDefs::encode_emcal(towerNumber - 1);
        unsigned int phi_bin = TowerInfoDefs::getCaloTowerPhiBin(key);
        unsigned int eta_bin = TowerInfoDefs::getCaloTowerEtaBin(key);

        int sectorNumber = phi_bin / 8 + 1;

        int bin = h2_cemc_mean->FindBin(eta_bin + 0.5, phi_bin + 0.5);

        sectorAvg[sectorNumber - 1] += 0;

        float signalFast = 0;

        rm_vector_twr[towerNumber - 1]->Add(&signalFast);

        h2_cemc_rm->SetBinContent(bin, rm_vector_twr[towerNumber - 1]->getMean(0));

        h2_cemc_mean->SetBinContent(bin, h2_cemc_mean->GetBinContent(bin));
      }
    }  // zero filling bad packets
  }    // packet loop

  h1_event->Fill(0);

  eventCounter++;
  return 0;
}

int CemcMon::Reset()
{
  // reset our internal counters
  eventCounter = 0;
  idummy = 0;
  return 0;
}
