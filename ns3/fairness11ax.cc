#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/applications-module.h"

#include <sstream>
#include <vector>
#include <unordered_map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MixedUlNax");

/**
 * Simple Poisson UDP uplink generator:
 * inter-arrival ~ Exp(lambda), constant packet size, sends to AP:port
 */
class PoissonUdpApp : public Application
{
public:
  PoissonUdpApp() = default;

  void Setup(Ptr<Socket> socket,
             Address peer,
             uint32_t pktSize,
             double lambdaPktsPerSec,
             uint64_t maxPackets = 0 /*0 => unlimited*/)
  {
    m_socket = socket;
    m_peer = peer;
    m_pktSize = pktSize;
    m_lambda = lambdaPktsPerSec;
    m_maxPackets = maxPackets;
    m_sent = 0;
    m_running = false;
    m_rng = CreateObject<ExponentialRandomVariable>();
    if (m_lambda > 0.0)
    {
      m_rng->SetAttribute("Mean", DoubleValue(1.0 / m_lambda));
    }
  }

private:
  void StartApplication() override
  {
    m_running = true;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    ScheduleNext();
  }

  void StopApplication() override
  {
    m_running = false;
    if (m_sendEvent.IsPending())
    {
      Simulator::Cancel(m_sendEvent);
    }
    if (m_socket)
    {
      m_socket->Close();
    }
  }

  void SendOnce()
  {
    if (!m_running)
      return;

    if (m_maxPackets != 0 && m_sent >= m_maxPackets)
      return;

    Ptr<Packet> p = Create<Packet>(m_pktSize);
    m_socket->Send(p);
    m_sent++;

    ScheduleNext();
  }

  void ScheduleNext()
  {
    if (!m_running)
      return;

    if (m_lambda <= 0.0)
      return;

    const double dt = m_rng->GetValue(); // seconds
    m_sendEvent = Simulator::Schedule(Seconds(dt), &PoissonUdpApp::SendOnce, this);
  }

private:
  Ptr<Socket> m_socket;
  Address m_peer;
  uint32_t m_pktSize{1200};
  double m_lambda{100.0};
  uint64_t m_maxPackets{0};
  uint64_t m_sent{0};
  bool m_running{false};
  EventId m_sendEvent;
  Ptr<ExponentialRandomVariable> m_rng;
};

// ---- Utilities ----

static std::vector<double> ParseCsvDoubles(const std::string& s)
{
  std::vector<double> out;
  if (s.empty())
    return out;

  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ','))
  {
    if (!item.empty())
      out.push_back(std::stod(item));
  }
  return out;
}

// Per-station counters
struct StaStats
{
  uint64_t collisionsLike{0};
  uint64_t finalFailures{0};
  uint64_t phyTxDrops{0};
  uint64_t qBytesSum{0};
  uint64_t qSamples{0};

  // HE uplink mode counters (counts MPDUs observed on PHY TX)
  uint64_t heSuTxMpdu{0};
  uint64_t heTbTxMpdu{0};
  uint64_t heSuTxBytes{0};
  uint64_t heTbTxBytes{0};
};

// Forward declarations (must appear before main)
static void OnMacTxDataFailed(uint32_t staIndex,
                             std::vector<StaStats>* stats,
                             ns3::Mac48Address addr);

static void OnMacTxFinalDataFailed(uint32_t staIndex,
                                  std::vector<StaStats>* stats,
                                  ns3::Mac48Address addr);

static void OnPhyTxDrop(uint32_t staIndex,
                        std::vector<StaStats>* stats,
                        ns3::Ptr<const ns3::Packet> p);


static void OnMacTxDataFailed(uint32_t staIndex,
                             std::vector<StaStats>* stats,
                             ns3::Mac48Address /*addr*/)
{
  (*stats)[staIndex].collisionsLike++;
}

static void OnMacTxFinalDataFailed(uint32_t staIndex,
                                  std::vector<StaStats>* stats,
                                  ns3::Mac48Address /*addr*/)
{
  (*stats)[staIndex].finalFailures++;
}

static void OnPhyTxDrop(uint32_t staIndex,
                        std::vector<StaStats>* stats,
                        ns3::Ptr<const ns3::Packet> /*p*/)
{
  (*stats)[staIndex].phyTxDrops++;
}

static void
OnHePhyTxMonitor(uint32_t staIndex,
                 std::vector<StaStats>* stats,
                 ns3::Ptr<const ns3::Packet> p,
                 uint16_t /*channelFreqMhz*/,
                 ns3::WifiTxVector txVector,
                 ns3::MpduInfo /*mpduInfo*/,
                 uint16_t /*something_uint16*/)   // <-- THIS must be uint16_t in your build
{
  const uint32_t bytes = p ? p->GetSize() : 0;
  const auto pre = txVector.GetPreambleType();

  if (pre == WIFI_PREAMBLE_HE_TB)
  {
    (*stats)[staIndex].heTbTxMpdu++;
    (*stats)[staIndex].heTbTxBytes += bytes;
  }
  else if (pre == WIFI_PREAMBLE_HE_SU)
  {
    (*stats)[staIndex].heSuTxMpdu++;
    (*stats)[staIndex].heSuTxBytes += bytes;
  }
}
/**
 * Sample BE queue size for a given STA device:
 * We read the WifiMac attribute "BE_Txop" (pointer to Txop/QosTxop), then get its WifiMacQueue size.
 */
static void SampleQueue(Ptr<WifiNetDevice> dev, uint32_t staIndex, std::vector<StaStats>* stats)
{
  Ptr<WifiMac> mac = dev->GetMac();
  PointerValue pv;
  mac->GetAttribute("BE_Txop", pv); // attribute exists on WifiMac. :contentReference[oaicite:2]{index=2}
  Ptr<Txop> txop = pv.Get<Txop>();
  uint32_t qBytes = 0;
  if (txop && txop->GetWifiMacQueue())
  {
    qBytes = txop->GetWifiMacQueue()->GetNBytes(); // bytes currently in queue
  }
  (*stats)[staIndex].qBytesSum += qBytes;
  (*stats)[staIndex].qSamples++;

  Simulator::Schedule(MilliSeconds(1), &SampleQueue, dev, staIndex, stats);
}

int main(int argc, char* argv[])
{
  uint32_t nLegacy = 5;
  uint32_t mHe = 5;
  double simTime = 30.0;
  uint32_t payloadSize = 1200;
  uint32_t apCwMin = 15;   // default DCF CWmin
  uint32_t apCwMax = 1023; // default DCF CWmax

  // If lambdaList is provided, it applies to ALL STAs in order:
  // [0..nLegacy-1]=legacy, [nLegacy..nLegacy+mHe-1]=HE
  // If not provided, we use lambdaLegacy for legacy and lambdaHe for HE.
  std::string lambdaListCsv = "";
  double lambdaLegacy = 1000.0; // pkts/s
  double lambdaHe = 1000.0;     // pkts/s

  // UL OFDMA scheduler knobs
  bool enableUlOfdma = true;
  Time muAccessReqInterval = MilliSeconds(0);

  CommandLine cmd(__FILE__);
  cmd.AddValue("nLegacy", "Number of 802.11ac (HT) stations", nLegacy);
  cmd.AddValue("mHe", "Number of 802.11ax (HE) stations", mHe);
  cmd.AddValue("simTime", "Simulation time (s) after apps start", simTime);
  cmd.AddValue("payloadSize", "UDP payload size (bytes)", payloadSize);
  cmd.AddValue("lambdaList", "Comma-separated lambdas (pkts/s) per station (legacy first, then HE)", lambdaListCsv);
  cmd.AddValue("lambdaLegacy", "Default lambda (pkts/s) for legacy STAs if lambdaList is empty", lambdaLegacy);
  cmd.AddValue("lambdaHe", "Default lambda (pkts/s) for HE STAs if lambdaList is empty", lambdaHe);
  cmd.AddValue("apCwMin", "AP BE CWmin (DCF)", apCwMin);
  cmd.AddValue("apCwMax", "AP BE CWmax (DCF)", apCwMax);
  cmd.AddValue("enableUlOfdma", "Enable UL OFDMA in MU scheduler", enableUlOfdma);
  cmd.AddValue("muAccessReqInterval", "MU scheduler access request interval (e.g., 0ms, 2ms)", muAccessReqInterval);
  cmd.Parse(argc, argv);

  const uint32_t nTotal = nLegacy + mHe;

  // ---- Nodes ----
  NodeContainer apNode;
  apNode.Create(1);

  NodeContainer staLegacy;
  staLegacy.Create(nLegacy);

  NodeContainer staHe;
  staHe.Create(mHe);

  NodeContainer allStas;
  allStas.Add(staLegacy);
  allStas.Add(staHe);

  // ---- PHY/channel (Spectrum is used; required/typical when OFDMA is enabled) ----
  Ptr<MultiModelSpectrumChannel> channel = CreateObject<MultiModelSpectrumChannel>();

  SpectrumWifiPhyHelper phy;
  phy.SetChannel(channel);
  phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

  // 20 MHz @ 5 GHz, channel 36 (common)
  // Format used by ns-3 Wi-Fi examples: "{channelNumber, channelWidth, band, 0}"
  // Example file uses ChannelSettings like this. :contentReference[oaicite:3]{index=3}
  phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));

  Ssid ssid = Ssid("mixed-ul");

  // ---- Install AP (HE / 802.11ax) ----
  WifiHelper wifiAp;
  wifiAp.SetStandard(WIFI_STANDARD_80211ax);

  WifiMacHelper macAp;

  // Attach MU scheduler at AP (Round-Robin), and enable UL OFDMA flag
  // (Same pattern as official HE example.) :contentReference[oaicite:4]{index=4}
  macAp.SetMultiUserScheduler("ns3::RrMultiUserScheduler",
                              "EnableUlOfdma", BooleanValue(enableUlOfdma),
                              "EnableBsrp", BooleanValue(false),
                              "AccessReqInterval", TimeValue(muAccessReqInterval));

  macAp.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "EnableBeaconJitter", BooleanValue(false));

  NetDeviceContainer apDev = wifiAp.Install(phy, macAp, apNode);

  // ---- Adjust AP contention window (BE AX only) ----
  Ptr<WifiNetDevice> apWifiDev = DynamicCast<WifiNetDevice>(apDev.Get(0));
  NS_ASSERT(apWifiDev);

  Ptr<WifiMac> apMac = apWifiDev->GetMac();
  PointerValue pv;
  apMac->GetAttribute("BE_Txop", pv);
  Ptr<Txop> beTxop = pv.Get<Txop>();

  NS_ASSERT(beTxop);

  // Set CWmin / CWmax for AP
  beTxop->SetMinCw(apCwMin);
  beTxop->SetMaxCw(apCwMax);

  //NS_LOG_UNCOND("AP BE CW configured: CWmin=" << apCwMin
  //              << ", CWmax=" << apCwMax);

  // ---- Install legacy STAs (HT / 802.11ac) sharing same channel ----
  WifiHelper wifiLegacy;
  wifiLegacy.SetStandard(WIFI_STANDARD_80211ac);

  WifiMacHelper macLegacy;
  macLegacy.SetType("ns3::StaWifiMac",
                    "Ssid", SsidValue(ssid),
                    "ActiveProbing", BooleanValue(false));

  NetDeviceContainer legacyDevs = wifiLegacy.Install(phy, macLegacy, staLegacy);

  // ---- Install HE STAs (802.11ax) ----
  WifiHelper wifiHe;
  wifiHe.SetStandard(WIFI_STANDARD_80211ax);

  WifiMacHelper macHe;
  macHe.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));

  NetDeviceContainer heDevs = wifiHe.Install(phy, macHe, staHe);

  // ---- Forcing HE STA to use UL OFDMA only
  for (uint32_t j = 0; j < mHe; ++j)
  {
    Ptr<WifiNetDevice> dev =
      DynamicCast<WifiNetDevice>(heDevs.Get(j));
    Ptr<WifiMac> mac = dev->GetMac();

    PointerValue pv;
    mac->GetAttribute("BE_Txop", pv);
    Ptr<Txop> txop = pv.Get<Txop>();

    txop->SetMinCw(15);
    txop->SetMaxCw(1023);
  }

  // ---- Mobility (static, close distance) ----
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
  pos->Add(Vector(0.0, 0.0, 0.0)); // AP
  for (uint32_t i = 0; i < nTotal; ++i)
  {
    pos->Add(Vector(1.0 + 0.1 * i, 0.0, 0.0));
  }
  mobility.SetPositionAllocator(pos);
  mobility.Install(apNode);
  mobility.Install(allStas);

  // ---- Internet ----
  InternetStackHelper stack;
  stack.Install(apNode);
  stack.Install(allStas);

  Ipv4AddressHelper addr;
  addr.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer apIf = addr.Assign(apDev);

  Ipv4InterfaceContainer legacyIf = addr.Assign(legacyDevs);
  Ipv4InterfaceContainer heIf = addr.Assign(heDevs);

  // ---- Lambda assignment ----
  std::vector<double> lambdas(nTotal, 0.0);
  auto parsed = ParseCsvDoubles(lambdaListCsv);
  if (!parsed.empty())
  {
    for (uint32_t i = 0; i < nTotal; ++i)
    {
      lambdas[i] = (i < parsed.size()) ? parsed[i] : parsed.back();
    }
  }
  else
  {
    for (uint32_t i = 0; i < nLegacy; ++i) lambdas[i] = lambdaLegacy;
    for (uint32_t j = 0; j < mHe; ++j) lambdas[nLegacy + j] = lambdaHe;
  }

  // ---- Per-station sinks at AP (one port per STA) ----
  const uint16_t basePort = 40000;
  std::vector<Ptr<PacketSink>> sinks(nTotal);

  for (uint32_t i = 0; i < nTotal; ++i)
  {
    Address sinkLocal(InetSocketAddress(Ipv4Address::GetAny(), basePort + i));
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", sinkLocal);
    ApplicationContainer sinkApp = sinkHelper.Install(apNode.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(1.0 + simTime + 0.1));

    sinks[i] = DynamicCast<PacketSink>(sinkApp.Get(0));
  }

  // ---- Install Poisson UDP apps on STAs (uplink only) ----
  const Time appStart = Seconds(1.0);
  const Time appStop = Seconds(1.0 + simTime);

  for (uint32_t i = 0; i < nTotal; ++i)
  {
    Ptr<Node> sta = allStas.Get(i);
    Ptr<Socket> sock = Socket::CreateSocket(sta, UdpSocketFactory::GetTypeId());
    Address peer(InetSocketAddress(apIf.GetAddress(0), basePort + i));

    Ptr<PoissonUdpApp> app = CreateObject<PoissonUdpApp>();
    app->Setup(sock, peer, payloadSize, lambdas[i]);
    sta->AddApplication(app);
    app->SetStartTime(appStart);
    app->SetStopTime(appStop);
  }

  // ---- Stats: collisions/errors/queue ----
  std::vector<StaStats> stats(nTotal);

  // Hook per-device traces for each STA
  // - collisionsLike: MacTxDataFailed
  // - finalFailures: MacTxFinalDataFailed
  // These traces exist in ns-3.46. :contentReference[oaicite:5]{index=5}
  for (uint32_t i = 0; i < nTotal; ++i)
  {
    ns3::Ptr<ns3::WifiNetDevice> dev = nullptr;

    if (i < nLegacy)
    {
    dev = ns3::DynamicCast<ns3::WifiNetDevice>(legacyDevs.Get(i));
    }
    else
    {
      dev = ns3::DynamicCast<ns3::WifiNetDevice>(heDevs.Get(i - nLegacy));
    }

    NS_ASSERT(dev);

    ns3::Ptr<ns3::WifiRemoteStationManager> rsm = dev->GetRemoteStationManager();

    rsm->TraceConnectWithoutContext(
      "MacTxDataFailed",
      ns3::MakeBoundCallback(&OnMacTxDataFailed, i, &stats));

    rsm->TraceConnectWithoutContext(
      "MacTxFinalDataFailed",
      ns3::MakeBoundCallback(&OnMacTxFinalDataFailed, i, &stats));

    dev->GetPhy()->TraceConnectWithoutContext(
      "PhyTxDrop",
      ns3::MakeBoundCallback(&OnPhyTxDrop, i, &stats));

    // Count HE SU vs HE TB uplink frames (HE stations only)
    if (i >= nLegacy)
    {
      dev->GetPhy()->TraceConnectWithoutContext(
        "MonitorSnifferTx",
        MakeBoundCallback(&OnHePhyTxMonitor, i, &stats));
    }

    // Queue sampler (BE queue). WifiMac has BE_Txop attribute. :contentReference[oaicite:6]{index=6}
    Simulator::Schedule(MilliSeconds(1), &SampleQueue, dev, i, &stats);
  }

  Simulator::Stop(appStop + Seconds(0.2));
  Simulator::Run();

  // ---- Print results ----
  const double measuredInterval = simTime; // seconds
  std::cout << "\n=== Results (uplink only) ===\n";
  std::cout << "nLegacy=" << nLegacy << ", mHe=" << mHe
            << ", channelWidth=20MHz, simTime=" << simTime << "s"
            << ", apCWmin="<< apCwMin <<", apCWmax=" << apCwMax << "s\n\n";

  for (uint32_t i = 0; i < nTotal; ++i)
  {
    const uint64_t rxBytes = sinks[i]->GetTotalRx();
    const double thrMbps = (rxBytes * 8.0) / (measuredInterval * 1e6);

    const double avgQ = (stats[i].qSamples > 0)
                          ? (static_cast<double>(stats[i].qBytesSum) / stats[i].qSamples)
                          : 0.0;

    const bool isLegacy = (i < nLegacy);
    std::cout << "STA[" << i << "] "
              << (isLegacy ? "HT(11ac)" : "HE(11ax)")
              << "  lambda=" << lambdas[i] << " pkt/s"
              << "  throughput=" << thrMbps << " Mbps"
              << "  avgMacQueue=" << avgQ << " B"
              << "  macTxDataFailed=" << stats[i].collisionsLike
              << "  macTxFinalDataFailed=" << stats[i].finalFailures
              << "  phyTxDrop=" << stats[i].phyTxDrops
              << "  heSuTxMpdu=" << stats[i].heSuTxMpdu
              << "  heTbTxMpdu=" << stats[i].heTbTxMpdu
              << "  heSuTxBytes=" << stats[i].heSuTxBytes
              << "  heTbTxBytes=" << stats[i].heTbTxBytes
              << "\n";
  }

  Simulator::Destroy();
  return 0;
}
