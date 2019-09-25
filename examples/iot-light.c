/*
 * Copyright (C) 2019
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v3.0. See the file LICENSE in the top level
 * directory for more details.
 *
 * See AUTHORS.md for complete list of NDN IOT PKG authors and contributors.
 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <ndn-lite.h>
#include "ndn-lite/app-support/service-discovery.h"

ndn_name_t self_identity;
ndn_name_t home_prefix;
ndn_name_t locator;
uint8_t light_brightness;
uint8_t data_buf[NDN_CONTENT_BUFFER_SIZE * 2];
// ndn_unix_face_t *face;
ndn_udp_face_t *face;
ndn_encoder_t encoder;
bool running;

int parseArgs(int argc, char *argv[]) {
  int i;

  if (argc < 2) {
    fprintf(stderr, "ERROR: wrong arguments.\n");
    printf("Usage: <self-prefix>\n");
    printf("Example: /ndn-iot/bedroom/light1\n");
    return 1;
  }
  if (ndn_name_from_string(&self_identity, argv[1], strlen(argv[1])) != NDN_SUCCESS) {
    fprintf(stderr, "ERROR: wrong name.\n");
    return 4;
  }
  ndn_name_init(&home_prefix);
  ndn_name_append_component(&home_prefix, &self_identity.components[0]);

  ndn_name_init(&locator);
  for(i = 1; i < self_identity.components_size; i ++){
    ndn_name_append_component(&locator, &self_identity.components[i]);
  }
  return 0;
}

int light_service(const uint8_t* interest, uint32_t interest_size, void* userdata) {
  uint8_t *param, *name, new_val;
  ndn_name_t name_check;
  size_t *param_size, ret_size;
  int ret;

  printf("RECEIVED INTEREST\n");

  ret = tlv_parse_interest(interest, interest_size, 4,
                           TLV_INTARG_NAME_BUF, &name,
                           TLV_INTARG_NAME_PTR, &name_check,
                           TLV_INTARG_PARAMS_BUF, &param,
                           TLV_INTARG_PARAMS_SIZE, &param_size);
  if(ret != NDN_SUCCESS){
    return NDN_FWD_STRATEGY_SUPPRESS;
  }

  // Remove parameter digest
  if(name_check.components[name_check.components_size - 1].type != TLV_GenericNameComponent){
    name_check.components_size --;
  }

  // Check the function ID (=0)
  if(name_check.components[name_check.components_size - 1].size != 1 ||
     name_check.components[name_check.components_size - 1].value[0] != 0)
  {
    return NDN_FWD_STRATEGY_SUPPRESS;
  }

  // Check the locator (can become API)
  if(name_check.components_size - 3 > locator.components_size){
    return NDN_FWD_STRATEGY_SUPPRESS;
  }
  if(ndn_name_compare_sub_names(&locator, 0, name_check.components_size - 3,
                                &name_check, 2, name_check.components_size - 1) != 0)
  {
    return NDN_FWD_STRATEGY_SUPPRESS;
  }

  // Execute the function
  if(param){
    new_val = *param;
  }else{
    new_val = 0xFF;
  }
  if(new_val != 0xFF){
    if((new_val > 0) != (light_brightness > 0)){
      if(new_val > 0){
        printf("Switch on the light.\n");
      }else{
        printf("Turn off the light.\n");
      }
    } 
    if(new_val < 10) {
      light_brightness = new_val;
      if(light_brightness > 0){
        printf("Successfully set the brightness = %u\n", light_brightness);
      }
    }else{
      light_brightness = 10;
      printf("Exceeding range. Set the brightness = %u\n", light_brightness);
    }
  }else{
    printf("Query the brightness = %u\n", light_brightness);
  }

  tlv_make_data(data_buf, sizeof(data_buf), &ret_size, 4,
                TLV_DATAARG_NAME_BUF, name,
                TLV_DATAARG_CONTENT_BUF, &light_brightness,
                TLV_DATAARG_CONTENT_SIZE, sizeof(light_brightness),
                TLV_DATAARG_FRESHNESSPERIOD_U64, 1000);
  ndn_forwarder_put_data(data_buf, ret_size);

  return NDN_FWD_STRATEGY_SUPPRESS;
}

void SignalHandler(int signum){
  running = false;
}

int main(int argc, char *argv[]){
  uint8_t temp_byte;
  ndn_name_t temp_name;
  in_port_t multicast_port = htons(56363);
  in_addr_t multicast_ip = inet_addr("224.0.23.170");
  int ret;

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);
  signal(SIGQUIT, SignalHandler);

  ndn_lite_startup();
  ret = parseArgs(argc, argv);
  if(ret != 0){
    return ret;
  }

  // Create face
  // face = ndn_unix_face_construct(NDN_NFD_DEFAULT_ADDR, true);
  face = ndn_udp_multicast_face_construct(INADDR_ANY, multicast_ip, multicast_port);

  // Register light service
  ndn_name_init(&temp_name);
  ndn_name_append_component(&temp_name, &home_prefix.components[0]);
  temp_byte = NDN_SD_LED;
  ndn_name_append_bytes_component(&temp_name, &temp_byte, sizeof(temp_byte));

  encoder_init(&encoder, data_buf, sizeof(data_buf));
  ndn_name_tlv_encode(&encoder, &home_prefix);
  ndn_forwarder_add_route(&face->intf, data_buf, encoder.offset);

  encoder_init(&encoder, data_buf, sizeof(data_buf));
  ndn_name_tlv_encode(&encoder, &temp_name);
  ndn_forwarder_register_prefix(data_buf, encoder.offset, light_service, NULL);

  // Start service discovery (This should be done after adding route)
  sd_add_or_update_self_service(NDN_SD_LED, true, 0);
  ndn_key_storage_t *ndn_key_storage = ndn_key_storage_get_instance();
  ndn_key_storage->self_identity = self_identity;
  ndn_sd_after_bootstrapping(&face->intf);

  // Main loop
  running = true;
  while(running){
    ndn_forwarder_process();
    usleep(10000);
  }
  ndn_face_destroy(&face->intf);
}
