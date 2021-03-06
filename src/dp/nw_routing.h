/* Copyright (c) 2016 Mellanox Technologies, Ltd. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
* 3. Neither the names of the copyright holders nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
*
* Alternatively, this software may be distributed under the terms of the
* GNU General Public License ("GPL") version 2 as published by the Free
* Software Foundation.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*  Project:             NPS400 ALVS application
*  File:                nw_routing.h
*  Desc:                network infrastructure file containing routing functionality
*/

#ifndef NW_ROUTING_H_
#define NW_ROUTING_H_

#include "nw_utils.h"

/******************************************************************************
 * \brief         send frames to network ports
 * \return        void
 */
static __always_inline
void nw_send_frame_to_network(ezframe_t __cmem * frame,
			      uint8_t __cmem * frame_base,
			      uint32_t port_id)
{
	/*do hash on destination mac - for lag calculation*/
	uint32_t hash_value = ezdp_hash(((uint32_t *)frame_base)[0],
					((uint32_t *)frame_base)[1],
					LOG2(NUM_OF_LAG_MEMBERS),
					sizeof(struct ether_addr),
					0,
					EZDP_HASH_BASE_MATRIX_HASH_BASE_MATRIX_0,
					EZDP_HASH_PERMUTATION_0);

	if (nw_interface_lookup(port_id + hash_value) != 0) {
		alvs_write_log(LOG_ERR, "network interface = %d lookup fail", port_id + hash_value);
		/* drop frame!! */
		alvs_discard_and_stats(ALVS_ERROR_SEND_FRAME_FAIL);
		return;
	}

	ezframe_send_to_if(frame, cmem_nw.interface_result.output_channel, 0);
}


/******************************************************************************
 * \brief         perform arp lookup and modify l2 header before transmission
 * \return        void
 */
static __always_inline
void nw_arp_processing(ezframe_t __cmem * frame,
		       uint8_t __cmem * buffer_base,
		       in_addr_t dest_ip,
		       uint32_t	frame_buff_size)
{
	 uint32_t rc;
	 uint32_t found_result_size;
	 struct nw_arp_result *arp_res_ptr;


	 cmem_nw.arp_key.real_server_address = dest_ip;

	 rc = ezdp_lookup_hash_entry(&shared_cmem_nw.arp_struct_desc,
				     (void *)&cmem_nw.arp_key,
				     sizeof(struct nw_arp_key),
				     (void **)&arp_res_ptr, &found_result_size,
				     0, cmem_wa.nw_wa.arp_hash_wa,
				     sizeof(cmem_wa.nw_wa.arp_hash_wa));

	if (likely(rc == 0)) {
		struct ether_addr *dmac = (struct ether_addr *)buffer_base;

		/*copy dst mac*/
		ezdp_mem_copy(dmac, arp_res_ptr->dest_mac_addr.ether_addr_octet, sizeof(struct ether_addr));
		/*copy src mac*/
		ezdp_mem_copy((uint8_t *)dmac+sizeof(struct ether_addr), cmem_nw.interface_result.mac_address.ether_addr_octet, sizeof(struct ether_addr));

		/* Store modified segment data */
		ezframe_store_buf(frame,
				  buffer_base,
			   frame_buff_size,
			   0);

		nw_send_frame_to_network(frame,
					 buffer_base,
					 arp_res_ptr->base_logical_id);
	} else {
		alvs_write_log(LOG_DEBUG, "dest_ip = 0x%x ARP lookup FAILED", dest_ip);
		nw_interface_inc_counter(NW_IF_STATS_FAIL_ARP_LOOKUP);
		nw_discard_frame();
		return;
	}
}


/******************************************************************************
 * \brief         perform FIB lookup and get dest_ip for transmission
 * \return        dest IP or 0 for host frame
 */
static __always_inline
uint32_t nw_fib_processing(in_addr_t dest_ip)
{
	enum nw_fib_type     result_type;
	uint32_t             res_dest_ip;
	struct ezdp_lookup_int_tcam_retval tcam_retval;

	/* read iTCAM */
	cmem_nw.fib_key.rsv0    = 0;
	cmem_nw.fib_key.rsv1    = 0;
	cmem_nw.fib_key.dest_ip = dest_ip;
	tcam_retval.raw_data = ezdp_lookup_int_tcam(NW_FIB_TCAM_SIDE,
						   NW_FIB_TCAM_PROFILE,
						   &cmem_nw.fib_key,
						   sizeof(struct nw_fib_key),
						   &cmem_nw.int_tcam_result);

	/* check matching */
	if (unlikely(tcam_retval.assoc_data.match == 0)) {
		alvs_write_log(LOG_ERR, "FIB lookup failed. key dest_ip = 0x%08x", dest_ip);
		nw_interface_inc_counter(NW_IF_STATS_FAIL_FIB_LOOKUP);
		return 0;
	}
	result_type = cmem_nw.fib_result.result_type;
	res_dest_ip = cmem_nw.fib_result.dest_ip;

	/* get dest_ip */
	if (likely(result_type == NW_FIB_NEIGHBOR)) {
		/* Destination IP is neighbor. use it for ARP */
		alvs_write_log(LOG_DEBUG, "NW_FIB_NEIGHBOR: using origin dest IP 0x%08x", dest_ip);
		return dest_ip;
	} else if (result_type == NW_FIB_GW) {
		/* Destination IP is GW. use result IP */
		alvs_write_log(LOG_DEBUG, "NW_FIB_GW: using result IP 0x%08x", res_dest_ip);
		return res_dest_ip;
	} else if (result_type == NW_FIB_DROP) {
		alvs_write_log(LOG_DEBUG, "NW_FIB_DROP: Drop frame.");
		nw_interface_inc_counter(NW_IF_STATS_REJECT_BY_FIB);
		return 0;
	}

	/* Unknown result type.*/
	alvs_write_log(LOG_ERR, "Unsupported FIB result type. dropping packet");
	nw_interface_inc_counter(NW_IF_STATS_UNKNOWN_FIB_RESULT);
	return 0;
}


/******************************************************************************
 * \brief         perform nw route
 * \return        void
 */
static __always_inline
void nw_do_route(ezframe_t __cmem * frame,
		 uint8_t *buffer_base,
		 in_addr_t dest_ip,
		 uint32_t frame_buff_size)
{
	uint32_t fib_dest_ip;

	fib_dest_ip = nw_fib_processing(dest_ip);
	if (fib_dest_ip == 0) {
		/* Drop frame */
		nw_discard_frame();
		return;
	}

	nw_arp_processing(frame, buffer_base, fib_dest_ip, frame_buff_size);
}

#endif /* NW_ROUTING_H_ */
