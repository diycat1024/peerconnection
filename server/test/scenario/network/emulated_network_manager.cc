/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/scenario/network/emulated_network_manager.h"

#include <memory>
#include <utility>

#include "absl/memory/memory.h"

namespace webrtc {
namespace test {

EmulatedNetworkManager::EmulatedNetworkManager(
    Clock* clock,
    EndpointsContainer* endpoints_container)
    : endpoints_container_(endpoints_container),
      socket_server_(clock, endpoints_container),
      network_thread_(&socket_server_),
      sent_first_update_(false),
      start_count_(0) {
  network_thread_.SetName("net_thread", nullptr);
  network_thread_.Start();
}

void EmulatedNetworkManager::EnableEndpoint(EmulatedEndpoint* endpoint) {
  RTC_CHECK(endpoints_container_->HasEndpoint(endpoint))
      << "No such interface: " << endpoint->GetPeerLocalAddress().ToString();
  network_thread_.PostTask(RTC_FROM_HERE, [this, endpoint]() {
    endpoint->Enable();
    UpdateNetworksOnce();
  });
}

void EmulatedNetworkManager::DisableEndpoint(EmulatedEndpoint* endpoint) {
  RTC_CHECK(endpoints_container_->HasEndpoint(endpoint))
      << "No such interface: " << endpoint->GetPeerLocalAddress().ToString();
  network_thread_.PostTask(RTC_FROM_HERE, [this, endpoint]() {
    endpoint->Disable();
    UpdateNetworksOnce();
  });
}

// Network manager interface. All these methods are supposed to be called from
// the same thread.
void EmulatedNetworkManager::StartUpdating() {
  RTC_DCHECK_RUN_ON(&network_thread_);

  if (start_count_) {
    // If network interfaces are already discovered and signal is sent,
    // we should trigger network signal immediately for the new clients
    // to start allocating ports.
    if (sent_first_update_)
      network_thread_.PostTask(RTC_FROM_HERE,
                               [this]() { MaybeSignalNetworksChanged(); });
  } else {
    network_thread_.PostTask(RTC_FROM_HERE, [this]() { UpdateNetworksOnce(); });
  }
  ++start_count_;
}

void EmulatedNetworkManager::StopUpdating() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!start_count_)
    return;

  --start_count_;
  if (!start_count_) {
    sent_first_update_ = false;
  }
}

void EmulatedNetworkManager::UpdateNetworksOnce() {
  RTC_DCHECK_RUN_ON(&network_thread_);

  std::vector<rtc::Network*> networks;
  for (std::unique_ptr<rtc::Network>& net :
       endpoints_container_->GetEnabledNetworks()) {
    net->set_default_local_address_provider(this);
    networks.push_back(net.release());
  }

  bool changed;
  MergeNetworkList(networks, &changed);
  if (changed || !sent_first_update_) {
    MaybeSignalNetworksChanged();
    sent_first_update_ = true;
  }
}

void EmulatedNetworkManager::MaybeSignalNetworksChanged() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // If manager is stopped we don't need to signal anything.
  if (start_count_ == 0) {
    return;
  }
  SignalNetworksChanged();
}

}  // namespace test
}  // namespace webrtc
