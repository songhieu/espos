#pragma once
// QEMU's network (env s3desk-qemu): the OpenCores Ethernet MAC (esp_eth_mac_openeth.c) and its DP83848 PHY
// (esp_eth_phy_dp83848.c, declared in esp_eth_phy.h). QEMU: -nic user,model=open_eth (NAT, DHCP, DNS).
#include "esp_eth.h"

#ifdef __cplusplus
extern "C" {
#endif
esp_eth_mac_t* esp_eth_mac_new_openeth(const eth_mac_config_t* config);
#ifdef __cplusplus
}
#endif
