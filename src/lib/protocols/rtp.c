/*
 * rtp.c
 *
 * Copyright (C) 2009-11 - ipoque GmbH
 * Copyright (C) 2011-22 - ntop.org
 *
 * This file is part of nDPI, an open source deep packet inspection
 * library based on the OpenDPI and PACE technology by ipoque GmbH
 *
 * nDPI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nDPI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with nDPI.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "ndpi_protocol_ids.h"

#define NDPI_CURRENT_PROTO NDPI_PROTOCOL_RTP

#include "ndpi_api.h"


/* http://www.myskypelab.com/2014/05/microsoft-lync-wireshark-plugin.html */

static u_int8_t isValidMSRTPType(u_int8_t payloadType) {
  switch(payloadType) {
  case 0: /* G.711 u-Law */
  case 3: /* GSM 6.10 */
  case 4: /* G.723.1  */
  case 8: /* G.711 A-Law */
  case 9: /* G.722 */
  case 13: /* Comfort Noise */
  case 96: /* Dynamic RTP */
  case 97: /* Redundant Audio Data Payload */
  case 101: /* DTMF */
  case 103: /* SILK Narrowband */
  case 104: /* SILK Wideband */
  case 111: /* Siren */
  case 112: /* G.722.1 */
  case 114: /* RT Audio Wideband */
  case 115: /* RT Audio Narrowband */
  case 116: /* G.726 */
  case 117: /* G.722 */
  case 118: /* Comfort Noise Wideband */
  case 34: /* H.263 [MS-H26XPF] */
  case 121: /* RT Video */
  case 122: /* H.264 [MS-H264PF] */
  case 123: /* H.264 FEC [MS-H264PF] */
  case 127: /* x-data */
    return(1 /* RTP */);
    break;

  case 200: /* RTCP PACKET SENDER */
  case 201: /* RTCP PACKET RECEIVER */
  case 202: /* RTCP Source Description */
  case 203: /* RTCP Bye */
    return(2 /* RTCP */);
    break;

  default:
    return(0);
  }
}

int is_valid_rtp_payload_type(uint8_t type)
{
  /* https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml */
  return type <= 34 || (type >= 96 && type <= 127);
}

/* *************************************************************** */

static void ndpi_rtp_search(struct ndpi_detection_module_struct *ndpi_struct,
			    struct ndpi_flow_struct *flow,
			    const u_int8_t * payload, const u_int16_t payload_len) {
  u_int8_t payloadType, payload_type;
  u_int16_t d_port = ntohs(ndpi_struct->packet.udp->dest);
  
  NDPI_LOG_DBG(ndpi_struct, "search RTP\n");

  if((payload_len < 2)
     || (d_port == 5355 /* LLMNR_PORT */)
     || (d_port == 5353 /* MDNS_PORT */)     
     || flow->stun.num_binding_requests
     ) {
    NDPI_EXCLUDE_PROTO(ndpi_struct, flow);
    return;
  }

  payload_type = payload[1] & 0x7F;
  
  /* Check whether this is an RTP flow */
  if((payload_len >= 12)
     && (((payload[0] & 0xFF) == 0x80) || ((payload[0] & 0xFF) == 0xA0)) /* RTP magic byte[1] */
     && ((payload_type < 72) || (payload_type > 76))
     && (is_valid_rtp_payload_type(payload_type))
    ) {
    if(flow->l4.udp.line_pkts[0] >= 2 && flow->l4.udp.line_pkts[1] >= 2) {
      /* It seems that it is a LINE stuff; let its dissector to evaluate */
      return;
    } else {
      NDPI_LOG_INFO(ndpi_struct, "Found RTP\n");
      ndpi_set_detected_protocol(ndpi_struct, flow, NDPI_PROTOCOL_RTP, NDPI_PROTOCOL_UNKNOWN, NDPI_CONFIDENCE_DPI);
      return;
    }
  } else if((payload_len >= 12)
	    && (((payload[0] & 0xFF) == 0x80) || ((payload[0] & 0xFF) == 0xA0)) /* RTP magic byte[1] */
	    && (payloadType = isValidMSRTPType(payload[1] & 0xFF))) {
    if(payloadType == 1 /* RTP */) {
      NDPI_LOG_INFO(ndpi_struct, "Found Skype for Business (former MS Lync)\n");
      ndpi_set_detected_protocol(ndpi_struct, flow, NDPI_PROTOCOL_SKYPE_TEAMS, NDPI_PROTOCOL_UNKNOWN, NDPI_CONFIDENCE_DPI);
      return;
    }
  }

  /* No luck this time */
  NDPI_EXCLUDE_PROTO(ndpi_struct, flow);
}

/* *************************************************************** */

static void ndpi_search_rtp(struct ndpi_detection_module_struct *ndpi_struct, struct ndpi_flow_struct *flow)
{
  struct ndpi_packet_struct *packet = &ndpi_struct->packet;
  u_int16_t source = ntohs(packet->udp->source);
  u_int16_t dest = ntohs(packet->udp->dest);
  
  // printf("==> %s()\n", __FUNCTION__);
  
  /* printf("*** %s(pkt=%d)\n", __FUNCTION__, flow->packet_counter); */

  if((source != 30303) && (dest != 30303 /* Avoid to mix it with Ethereum that looks alike */)
     && (dest > 1023)
     )
    ndpi_rtp_search(ndpi_struct, flow, packet->payload, packet->payload_packet_len);
  else
    NDPI_EXCLUDE_PROTO(ndpi_struct, flow);
}

/* *************************************************************** */

void init_rtp_dissector(struct ndpi_detection_module_struct *ndpi_struct,
			u_int32_t *id, NDPI_PROTOCOL_BITMASK *detection_bitmask) {
  ndpi_set_bitmask_protocol_detection("RTP", ndpi_struct, detection_bitmask, *id,
				      NDPI_PROTOCOL_RTP,
				      ndpi_search_rtp,
				      NDPI_SELECTION_BITMASK_PROTOCOL_V4_V6_UDP_WITH_PAYLOAD,
				      SAVE_DETECTION_BITMASK_AS_UNKNOWN,
				      ADD_TO_DETECTION_BITMASK);

  *id += 1;
}
