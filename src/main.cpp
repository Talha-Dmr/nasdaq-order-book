#include "order_book.hpp"
#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "core/symbol_table.hpp"
#include "net/feed_listener.hpp"
#include "net/arbiter.hpp"
#include "core/apply.hpp"
#include "perf/latency_tracker.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

template<typename OB>
static void run_file_mode_impl(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "Error: Could not open file " << path << std::endl;
    return;
  }
  std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
  std::cout << "Read " << buffer.size() << " bytes from file." << std::endl;

  core::SymbolTable symtab;
  itch::Decoder decoder(symtab);

  OB ob;
  size_t events = 0; size_t msgs = 0;
  const char* cur = buffer.data();
  const char* end = cur + buffer.size();
  while (cur < end) {
    auto res = decoder.decode_one(cur, static_cast<size_t>(end - cur));
    if (res.message_size == 0) break;
    ++msgs;
    if (res.event.has_value()) {
      core::apply(*res.event, ob);
      ++events;
    }
    cur += res.message_size;
  }
  std::cout << "File mode finished: messages=" << msgs << ", events=" << events << std::endl;
  std::cout << "Top snapshot (debug):" << std::endl; ob.display();
  std::cout << "File mode finished: messages=" << msgs << ", events=" << events << std::endl;
}

static void run_file_mode(const std::string& path, bool ultra = false) {
  if (ultra) {
    run_file_mode_impl<UltraOrderBook>(path);
  } else {
    run_file_mode_impl<OptimizedOrderBook>(path);
  }
}

template <typename OB>
static void run_net_mode_impl(const std::string& mcast, int portA, int portB, std::chrono::seconds duration) {
  using namespace std::chrono;
  core::SymbolTable symtab;
  itch::Decoder decoder(symtab);

  // Latency trackers for different pipeline stages
  perf::LatencyTracker net_to_arbiter_latency(5000);
  perf::LatencyTracker decode_latency(5000);
  perf::LatencyTracker orderbook_latency(5000);
  perf::LatencyTracker end_to_end_latency(5000);

  net::FeedListener feedA(mcast, portA, 65536);
  net::FeedListener feedB(mcast, portB, 65536);
  feedA.start();
  feedB.start();

  // Bind arbiter to listeners
  net::Arbiter arb(
    [&](core::PacketView& p){ return feedA.pop(p); },
    [&](core::PacketView& p){ return feedB.pop(p); }
  );

  auto t0 = high_resolution_clock::now();
  size_t packets = 0, events = 0;

  OB ob; // OrderBook instance (Optimized or Ultra)
  while (high_resolution_clock::now() - t0 < duration) {
    // Start end-to-end timing
    auto e2e_start = perf::now();
    
    // Measure arbiter (network + ordering) latency
    auto arb_start = perf::now();
    auto msg_opt = arb.next_message();
    if (!msg_opt) { 
      std::this_thread::sleep_for(std::chrono::microseconds(100)); 
      continue; 
    }
    auto arb_end = perf::now();
    net_to_arbiter_latency.record(perf::elapsed_ns(arb_start, arb_end));
    
    ++packets; // counting messages now

    // Measure decode latency
    auto decode_start = perf::now();
    auto res = decoder.decode_one(msg_opt->data, msg_opt->len);
    auto decode_end = perf::now();
    decode_latency.record(perf::elapsed_ns(decode_start, decode_end));
    
    if (res.event.has_value()) {
      // Measure order book update latency
      auto ob_start = perf::now();
      core::apply(*res.event, ob);
      auto ob_end = perf::now();
      orderbook_latency.record(perf::elapsed_ns(ob_start, ob_end));
      
      ++events;
      
      // Record full end-to-end latency
      auto e2e_end = perf::now();
      end_to_end_latency.record(perf::elapsed_ns(e2e_start, e2e_end));
    }
  }

  feedA.stop();
  feedB.stop();
  auto m = arb.metrics();
  std::cout << "Net mode finished: packets=" << packets << ", events=" << events
            << " | gaps: detected=" << m.gap_detected
            << ", filled=" << m.gap_filled
            << ", dropped_ttl=" << m.gap_dropped_ttl
            << ", dup_dropped=" << m.dup_dropped
            << std::endl;
  
  std::cout << "\n=== END-TO-END LATENCY BREAKDOWN ===" << std::endl;
  net_to_arbiter_latency.print_stats("Network + Arbitration");
  decode_latency.print_stats("ITCH Decoding");
  orderbook_latency.print_stats("Order Book Update");
  end_to_end_latency.print_stats("Total End-to-End");
}

static void run_net_mode(const std::string& mcast, int portA, int portB, bool ultra, int seconds_param) {
  using namespace std::chrono;
  seconds dur = seconds_param > 0 ? seconds(seconds_param) : seconds(10);
  if (ultra) {
    run_net_mode_impl<UltraOrderBook>(mcast, portA, portB, dur);
  } else {
    run_net_mode_impl<OptimizedOrderBook>(mcast, portA, portB, dur);
  }
}

int main(int argc, char *argv[]) {
  // CLI: --mode=net --mcast=239.0.0.1 --port-a=5007 --port-b=5008 | default: file <path>
  std::string mode = (argc > 1) ? argv[1] : "";
  if (mode == "--mode=net") {
    std::string mcast = "239.0.0.1";
    int portA = 5007, portB = 5008;
    bool ultra = false;
    int duration_sec = 10;
    // naive parse of args
    for (int i=2;i<argc;i++) {
      std::string a = argv[i];
      auto eq = a.find('=');
      if (a.rfind("--mcast=",0)==0) mcast = a.substr(eq+1);
      else if (a.rfind("--port-a=",0)==0) portA = std::stoi(a.substr(eq+1));
      else if (a.rfind("--port-b=",0)==0) portB = std::stoi(a.substr(eq+1));
      else if (a.rfind("--duration=",0)==0) duration_sec = std::stoi(a.substr(eq+1));
      else if (a == "--ultra") ultra = true;
    }
    run_net_mode(mcast, portA, portB, ultra, duration_sec);
    return 0;
  }

  if (argc >= 2) {
    bool ultra = false;
    // Check for --ultra flag in file mode
    for (int i = 2; i < argc; i++) {
      if (std::string(argv[i]) == "--ultra") {
        ultra = true;
        break;
      }
    }
    run_file_mode(argv[1], ultra);
    return 0;
  }

  std::cerr << "Usage: " << argv[0] << " <path_to_data.bin> [--ultra]\n"
            << "   or: " << argv[0] << " --mode=net [--mcast=239.0.0.1 --port-a=5007 --port-b=5008 --ultra --duration=SECONDS]" << std::endl;
  return 1;
}
