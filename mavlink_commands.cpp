#include "mavlink_commands.hpp"

MAVLink::MAVLink(int domain, int type, int protocol) {
  this->sockfd = socket(domain, type, protocol);
  if(this->sockfd < 0){
    printf("Error socket failed\n");
    exit(0);
  }
  strcpy(this->target_ip, "127.0.0.1");
  memset(&this->addr, 0, sizeof(this->addr));
  this->addr.sin_family = AF_INET;
  // this->addr.sin_addr.s_addr = INADDR_ANY;
  inet_pton(AF_INET, "127.0.0.1", &(this->addr.sin_addr));
  this->addr.sin_port = htons(14540);
  if(bind(this->sockfd, (struct sockaddr*) &addr, sizeof(sockaddr)) != 0){
    close(this->sockfd);
    exit(0);
  }
  if(fcntl(this->sockfd, F_SETFL, O_NONBLOCK | O_ASYNC) < 0){
    close(this->sockfd);
    exit(0);
  }
	memset(&this->destAddr, 0, sizeof(this->destAddr));
	this->destAddr.sin_family = AF_INET;
	// this->destAddr.sin_addr.s_addr = inet_addr(this->target_ip);
  inet_pton(AF_INET, "127.0.0.1", &destAddr.sin_addr.s_addr);
	this->destAddr.sin_port = htons(18570);
  this->fromlen = sizeof(this->destAddr);
  // if(bind(this->sockfd, (struct sockaddr *) &this->destAddr, sizeof(struct sockaddr))){
  //   close(this->sockfd);
  //   exit(0);
  // }
}

MAVLink::~MAVLink() {}

uint8_t MAVLink::get_px_mode(){
  return this->px_mode;
}

uint8_t MAVLink::get_px_status(){
  return this->px_status;
}

uint16_t MAVLink::get_mis_seq(){
  return this->mis_seq;
}

bool MAVLink::get_mis_req_status(){
  return this->req_mis;
}

std::array<float,3> MAVLink::get_global_pos_curr(){
  return this->global_pos_curr;
}

std::array<float, 3> MAVLink::get_velocity_curr(){
  return this->velocity_curr;
}

float MAVLink::get_time_boot(){
  return this->time_boot_sec;
}

uint16_t MAVLink::get_yaw_curr(){
  return this->yaw_curr;
}

void MAVLink::req_data_stream(){
  this->sys_id = 255;
  this->comp_id = 2;
  this->tgt_sys = 1;
  this->tgt_comp = 1;
  uint8_t req_stream_id = MAV_DATA_STREAM_ALL;
  uint16_t req_msg_rate = 0x01; // 1 times per second
  uint8_t start_stop = 1; // 1 start, 0 = stop

  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MAX_PACKET_LEN];

  mavlink_msg_request_data_stream_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    req_stream_id, 
    req_msg_rate, 
    start_stop
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
  
  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::read_data(){
  mavlink_message_t msg;
  mavlink_status_t status;
  uint8_t buf[BUFFER_LENGTH];

  memset(buf, 0, BUFFER_LENGTH);
  this->recsize = recvfrom(this->sockfd, (void*) buf, BUFFER_LENGTH, 0, (struct sockaddr *)&this->destAddr, &this->fromlen);
  if(recsize > 0)
  {
    //Get new message
    for (int i = 0; i < recsize; i++){
      if(mavlink_parse_char(MAVLINK_COMM_0, buf[i], &msg, &status))
      {

      //Handle new message from autopilot
        switch(msg.msgid)
        {
          case MAVLINK_MSG_ID_HEARTBEAT:
            this->check_mode(&msg);
            break;
          case MAVLINK_MSG_ID_MISSION_REQUEST_INT:
            this->mission_request(&msg);
            break;
          case MAVLINK_MSG_ID_MISSION_ACK:
            this->uploaded_mission_status(&msg);
            break;
          case MAVLINK_MSG_ID_MISSION_ITEM_REACHED:
            this->check_mission_progress(&msg);
            break;
          case MAVLINK_MSG_ID_COMMAND_ACK:
            this->command_ack(&msg);
            break;
          case MAVLINK_MSG_ID_SYS_STATUS:
            this->sys_status(&msg);
            break;
          case MAVLINK_MSG_ID_MISSION_CURRENT:
            this->current_mission_status(&msg);
            break;
          case MAVLINK_MSG_ID_MISSION_COUNT:
            this->recv_mission_count(&msg);
            break;
          case MAVLINK_MSG_ID_MISSION_ITEM_INT:
            this->recv_mission(&msg);
            break;
          case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
            this->recv_global_pos(&msg);
            break;
        }
      }
    }
  }
  memset(buf, 0, BUFFER_LENGTH);
}

void MAVLink::check_mode(mavlink_message_t* msg){
  mavlink_heartbeat_t hb;
  mavlink_msg_heartbeat_decode(msg, &hb);
  this->px_mode = hb.base_mode;
  this->px_status = hb.system_status;
  printf("Heartbeat detected\nMode : %u\nSystem : %u\n", this->px_mode, this->px_status);
}

void MAVLink::command_ack(mavlink_message_t* msg){
  mavlink_command_ack_t cmd_ack;
  mavlink_msg_command_ack_decode(msg, &cmd_ack);
  printf("Command %u result code %u\n", cmd_ack.command, cmd_ack.result);
}

void MAVLink::mission_request(mavlink_message_t* msg){
  if(this->mis_seq == 0) this->takeoff(5);
  else if(this->mis_seq == this->mis_count - 2) this->return_to_launch();
  else if(this->mis_seq == this->mis_count - 1) this->land();
  else{
    mavlink_mission_request_int_t mis_req;
    mavlink_msg_mission_request_int_decode(msg, &mis_req);
    this->mis_seq = mis_req.seq;
    this->tgt_sys = mis_req.target_system;
    this->tgt_comp = mis_req.target_component;
    printf("Requesting for mission type %u sequence %u\n", mis_req.mission_type, this->mis_seq);
    this->send_mission_item();
  }
}

void MAVLink::check_mission_progress(mavlink_message_t* msg){
  mavlink_mission_item_reached_t it;
  mavlink_msg_mission_item_reached_decode(msg, &it);
  printf("Mission sequence %u reached\n", it.seq);
}

void MAVLink::uploaded_mission_status(mavlink_message_t* msg){
  mavlink_mission_ack_t mis_ack;
  mavlink_msg_mission_ack_decode(msg, &mis_ack);
  if(mis_ack.type == MAV_MISSION_ACCEPTED){
    printf("Mission accepted\n");
    this->start_mission();
  }else{
    printf("Mission unaccepted with enum %u\n", mis_ack.type);
  }
}

void MAVLink::sys_status(mavlink_message_t* msg){
  mavlink_sys_status_t sys_status;
  mavlink_msg_sys_status_decode(msg, &sys_status);
  // printf(
  //   "Sensors Present : %u\n\
  //   Sensors Enabled : %u\n\
  //   Sensors Healthy : %u\n\
  //   Load (<1000%): %u %\n\
  //   Battery Voltage : %u V\n\
  //   Battery Current : %u cA\n\
  //   Battery Remaining : %u %\n\
  //   Comm Drop Rate : %u c%\n\
  //   Comm Errors : %u\n"
  // );
}

void MAVLink::recv_global_pos(mavlink_message_t* msg){
  mavlink_global_position_int_t global_pos;
  mavlink_msg_global_position_int_decode(msg, &global_pos);
  this->global_pos_curr[0] = static_cast<float>(global_pos.lat / 1e7);
  this->global_pos_curr[1] = static_cast<float>(global_pos.lon / 1e7);
  this->global_pos_curr[2] = static_cast<float>(global_pos.relative_alt / 1000);
  this->velocity_curr[0] = static_cast<float>(global_pos.vx / 100);
  this->velocity_curr[1] = static_cast<float>(global_pos.vy / 100);
  this->velocity_curr[2] = static_cast<float>(global_pos.vz / 100);
  this->time_boot_sec = static_cast<float>(global_pos.time_boot_ms / 1000);
  this->yaw_curr = global_pos.hdg;
}

void MAVLink::current_mission_status(mavlink_message_t* msg){
  mavlink_mission_current_t mis_stat;
  mavlink_msg_mission_current_decode(msg, &mis_stat);
  switch (mis_stat.mission_state){
    case MISSION_STATE_COMPLETE:
      printf("Mission completed. Returning to launch.");
      this->return_to_launch();
      break;
    case MISSION_STATE_NO_MISSION:
      printf("No mission uploaded");
      break;
    case MISSION_STATE_NOT_STARTED:
      printf("Mission uploaded but not started");
      break;
    case MISSION_STATE_PAUSED:
      printf("Mission paused at waypoint %u out of %u\n", mis_stat.seq, mis_stat.total);
      break;
    case MISSION_STATE_ACTIVE:
      printf("Mission active on the way to waypoint %u out of %u\n", mis_stat.seq, mis_stat.total);
      break;
    default:
      printf("Unknown mission status");
      break;
  }
}

void MAVLink::recv_mission_count(mavlink_message_t* msg){
  mavlink_mission_count_t recv_mis_count;
  mavlink_msg_mission_count_decode(msg, &recv_mis_count);
  if(recv_mis_count.count != this->mis_count){
    printf("Downloaded mission count is not the same as sent\n");
  }else{
    printf("Downloading %u missions\n", recv_mis_count.count);
    this->req_mission_item();
  }
}

void MAVLink::recv_mission(mavlink_message_t* msg){
  mavlink_mission_item_int_t recv_mis;
  mavlink_msg_mission_item_int_decode(msg, &recv_mis);
  printf("Downloaded waypoint %u with lat %f, long %f, and height %f\n", recv_mis.seq, recv_mis.x / 1e7, recv_mis.y / 1e7, recv_mis.z);
  if(recv_mis.seq != this->mis_count - 1) this->req_mission_item();
  else{
    this->mis_seq = 0;
  } 
}

void MAVLink::run_prearm_checks(){
  printf("Running prearm checks\n");

  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_COMMAND_LONG_LEN];

  uint16_t command = 401;
  uint8_t conf = 0;

  mavlink_msg_command_long_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    command, 
    conf, 
    0, 0, 0, 0, 0, 0, 0
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::arm_disarm(bool arm){
  // this->run_prearm_checks();

  if(arm) printf("Arming\n"); else printf("Disarming\n");
  
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_COMMAND_LONG_LEN];

  uint16_t command = 400; //arm disarm
  uint8_t conf = 0;
  float param1 = (float)arm;

  mavlink_msg_command_long_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    command, 
    conf, 
    param1, 
    0, 0, 0, 0, 0, 0
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));

  while(this->px_mode < 128);

  this->armed = true;
}

void MAVLink::takeoff(const float& height){
  printf("Taking off\n");
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_COMMAND_LONG_LEN];

  uint16_t command = 22; //takeoff
  uint8_t conf = 0;
  float param7 = height;

  mavlink_msg_command_long_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    command, 
    conf, 
    0, 0, 0, 0, 0, 0, 
    param7
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));

  this->arm_disarm(true);

  this->set_mode(MAV_MODE_AUTO_ARMED);

  // while(std::abs(global_pos_curr[2] - height) > 0.3);

  // printf("Takeoff height reached\n");

  // this->set_mode(MAV_MODE_AUTO_ARMED);

  // sleep(5);

  // this->arm_disarm(true);

  // this->set_mode(MAV_MODE_AUTO_ARMED);

}

void MAVLink::land(){
  printf("Landing\n");
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_COMMAND_LONG_LEN];

  uint16_t command = 21; //land
  uint8_t conf = 0;

  mavlink_msg_command_long_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    command, 
    conf, 
    0, 0, 0, 0, 0, 0, 0
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::set_mode(const uint16_t& mode){
  printf("Setting mode to %u", mode);
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_COMMAND_LONG_LEN];

  uint16_t command = 176; //do set mode
  uint8_t conf = 0;
  float param1 = float(mode); //auto disarmed

  mavlink_msg_command_long_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    command, 
    conf, 
    param1, 
    0, 0, 0, 0, 0, 0
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
  
  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::return_to_launch(){
  printf("Returning to launch position\n");

  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_COMMAND_LONG_LEN];

  uint16_t command = 20; //return to launch
  uint8_t conf = 0;

  mavlink_msg_command_long_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    command, 
    conf, 
    0, 0, 0, 0, 0, 0, 0
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::send_mission_count(const uint16_t& num_of_mission){
  this->mis_count = num_of_mission + 3;

  printf("Sending mission count: %u\n", num_of_mission);
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_MISSION_COUNT_LEN];

  this->mis_seq = 0;

  mavlink_msg_mission_count_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    this->mis_count, 
    MAV_MISSION_TYPE_MISSION
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::send_mission_item(){
  float lat = std::get<0>(this->waypoints.at(this->mis_seq));
  float lng = std::get<1>(this->waypoints.at(this->mis_seq));
  float hgt = std::get<2>(this->waypoints.at(this->mis_seq));
  printf("Setting waypoint lat : %f, lng : %f, height : %f\n", lat, lng, hgt);
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_MISSION_ITEM_INT_LEN];

  uint8_t frame = MAV_FRAME_GLOBAL_RELATIVE_ALT; //lat, long, altitude is relative to home altitude in meters
  uint8_t command = 16; //waypoint
  uint8_t current = 0;
  uint8_t cont = 1;
  float param1 = 1;
  float param2 = 1;
  float param3 = 0;
  float param4 = NAN;
  int32_t lat_send = lat * 1e7;
  int32_t lng_send = lng * 1e7;
  uint8_t mission_type = MAV_MISSION_TYPE_MISSION;

  mavlink_msg_mission_item_int_pack(
    this->sys_id, 
    this->comp_id, 
    &msg, 
    this->tgt_sys, 
    this->tgt_comp, 
    this->mis_seq, 
    frame, 
    command, 
    current, 
    cont, 
    param1, 
    param2, 
    param3, 
    param4, 
    lat_send, 
    lng_send, 
    hgt, 
    mission_type
  );
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));

  printf("Mission sequence %u sent", this->mis_seq);
}

void MAVLink::req_mission_list(){
  printf("Downloading mission from pixhawk\n");
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_MISSION_REQUEST_LIST_LEN];

  this->mis_seq = 0;

  mavlink_msg_mission_request_list_pack(
    this->sys_id,
    this->comp_id,
    &msg,
    this->tgt_sys,
    this->tgt_comp,
    MAV_MISSION_TYPE_MISSION
  );

  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));
}

void MAVLink::req_mission_item(){
  printf("Requesting mission item\n");
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_MISSION_REQUEST_INT_LEN];

  mavlink_msg_mission_request_int_pack(
    this->sys_id,
    this->comp_id,
    &msg,
    this->tgt_sys,
    this->tgt_comp,
    this->mis_seq,
    MAV_MISSION_TYPE_MISSION
  );

  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));

  this->mis_seq++;
}

void MAVLink::send_mission_ack(){
  printf("Mission downloading finished\n");
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MSG_ID_MISSION_ACK_LEN];

  // TODO : Better error handling here
  mavlink_msg_mission_ack_pack(
    this->sys_id,
    this->comp_id,
    &msg,
    this->tgt_sys,
    this->tgt_comp,
    MAV_MISSION_ACCEPTED,
    MAV_MISSION_TYPE_MISSION
  );

  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);

  this->bytes_sent = sendto(this->sockfd, buf, len, 0, (struct sockaddr*)&this->destAddr, sizeof(struct sockaddr_in));

  /*
  if waypoints are correct -> takeoff
  else cancel
  */
}

// TODO : Implement a better way to start mission
void MAVLink::start_mission(){
  printf("Starting mission\n");

  this->req_mission_list();
  
  /*
  If it takes off correctly but doesn't start mission, may need MAV_CMD_COMMAND_START here.
  Documentation says that drone will automatically start mission when switched to auto mode,
  with condition that mission is accepted.
  */
}