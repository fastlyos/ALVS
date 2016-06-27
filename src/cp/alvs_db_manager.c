/***************************************************************************
*
* Copyright (c) 2016 Mellanox Technologies, Ltd. All rights reserved.
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
*
*  Project:             NPS400 ALVS application
*  File:                alvs_db_manager.c
*  Desc:                Performs the main process of ALVS DB management
*
****************************************************************************/

/* linux includes */
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <netlink/netlink-compat.h>
#include <linux/sockios.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/ip_vs.h>
#include <linux/if_link.h>
#include <linux/netfilter.h>

/* libnl-3 includes */
#include <netlink/msg.h>
#include <netlink/genl/mngt.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

/* Project includes */
#include "log.h"
#include "defs.h"
#include "alvs_db.h"
#include "alvs_db_manager.h"
#include "infrastructure.h"

bool *alvs_db_manager_cancel_application_flag;
int raw_sock;
int family;

void alvs_db_manager_delete(void);
void alvs_db_manager_init(void);
void alvs_db_manager_table_init(void);
void alvs_db_manager_poll(void);

void process_packet(uint8_t *buffer, int size, struct sockaddr *saddr);
void alvs_nl_init(void);
static int alvs_msg_parser(struct nl_cache_ops *cache_ops, struct genl_cmd *cmd, struct genl_info *info, void *arg);
static int alvs_genl_parse_service(struct nlattr *nla, struct ip_vs_service_user *ret_svc, bool need_full_svc);
static int alvs_genl_parse_dest(struct nlattr *nla, struct ip_vs_dest_user *ret_dest, bool need_full_dest);
struct ip_vs_get_services *alvs_get_services(void);
struct ip_vs_get_dests *alvs_get_dests(struct ip_vs_service_entry *svc);

/* Max NL message size = 32K. used for buffer definition */
#define MAX_MSG_SIZE 0x8000

/* Policy used for command attributes */
static struct nla_policy alvs_cmd_policy[IPVS_CMD_ATTR_MAX + 1] = {
	[IPVS_CMD_ATTR_SERVICE]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_DEST]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_DAEMON]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_TIMEOUT_TCP]	= { .type = NLA_U32 },
	[IPVS_CMD_ATTR_TIMEOUT_TCP_FIN]	= { .type = NLA_U32 },
	[IPVS_CMD_ATTR_TIMEOUT_UDP]	= { .type = NLA_U32 },
};

#define ALVS_CMD_COUNT            7

static struct genl_cmd alvs_cmds[ALVS_CMD_COUNT] = {
	{
		.c_id           = IPVS_CMD_NEW_SERVICE,
		.c_name	        = "IPVS CMD NEW SERVICE",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser   = &alvs_msg_parser,
	},
	{
		.c_id           = IPVS_CMD_DEL_SERVICE,
		.c_name	        = "IPVS CMD DEL SERVICE",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser    = &alvs_msg_parser,
	},
	{
		.c_id           = IPVS_CMD_SET_SERVICE,
		.c_name	        = "IPVS CMD SET SERVICE",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser   = &alvs_msg_parser,
	},
	{
		.c_id           = IPVS_CMD_NEW_DEST,
		.c_name	        = "IPVS CMD NEW DEST",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser   = &alvs_msg_parser,
	},
	{
		.c_id           = IPVS_CMD_DEL_DEST,
		.c_name	        = "IPVS CMD DEL DEST",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser   = &alvs_msg_parser,
	},
	{
		.c_id           = IPVS_CMD_SET_DEST,
		.c_name	        = "IPVS CMD SET DEST",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser   = &alvs_msg_parser,
	},
	{
		.c_id           = IPVS_CMD_FLUSH,
		.c_name	        = "IPVS CMD FLUSH",
		.c_maxattr      = IPVS_CMD_ATTR_MAX,
		.c_attr_policy  = alvs_cmd_policy,
		.c_msg_parser   = &alvs_msg_parser,
	},
};
static struct genl_ops alvs_genl_ops = {
	.o_name  = IPVS_GENL_NAME,
	.o_cmds  = alvs_cmds,
	.o_ncmds = ALVS_CMD_COUNT,
};

/* Policy used for attributes in nested attribute IPVS_CMD_ATTR_SERVICE */
static struct nla_policy alvs_svc_policy[IPVS_SVC_ATTR_MAX + 1] = {
	[IPVS_SVC_ATTR_AF]		= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_PROTOCOL]	= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_ADDR]		= { .type = NLA_UNSPEC,
					    .maxlen = sizeof(union nf_inet_addr) },
	[IPVS_SVC_ATTR_PORT]		= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_FWMARK]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_SCHED_NAME]	= { .type = NLA_STRING,
					    .maxlen = IP_VS_SCHEDNAME_MAXLEN },
	[IPVS_SVC_ATTR_PE_NAME]		= { .type = NLA_STRING,
					    .maxlen = IP_VS_PENAME_MAXLEN },
	[IPVS_SVC_ATTR_FLAGS]		= { .type = NLA_UNSPEC,
					    .maxlen = sizeof(struct ip_vs_flags),
					    .minlen = sizeof(struct ip_vs_flags) },
	[IPVS_SVC_ATTR_TIMEOUT]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_NETMASK]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_STATS]		= { .type = NLA_NESTED },
};

/* Policy used for attributes in nested attribute IPVS_CMD_ATTR_DEST */
static struct nla_policy alvs_dest_policy[IPVS_DEST_ATTR_MAX + 1] = {
	[IPVS_DEST_ATTR_ADDR]		= { .type = NLA_UNSPEC,
					    .maxlen = sizeof(union nf_inet_addr) },
	[IPVS_DEST_ATTR_PORT]		= { .type = NLA_U16 },
	[IPVS_DEST_ATTR_FWD_METHOD]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_WEIGHT]		= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_U_THRESH]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_L_THRESH]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_ACTIVE_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_INACT_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_PERSIST_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_STATS]		= { .type = NLA_NESTED },
};


/******************************************************************************
 * \brief         ALVS thread main application.
 *
 * \return        void
 */
void alvs_db_manager_main(bool *cancel_application_flag)
{
	alvs_db_manager_cancel_application_flag = cancel_application_flag;
	write_log(LOG_DEBUG, "alvs_db_manager_init...\n");
	alvs_db_manager_init();
	write_log(LOG_DEBUG, "alvs_db_manager_table_init...\n");
	alvs_db_manager_table_init();
	write_log(LOG_INFO, "alvs_db_manager_poll...\n");
	alvs_db_manager_poll();
	alvs_db_manager_delete();
}

/******************************************************************************
 * \brief         poll on socket & process all received messages
 *
 * \return        void
 */
void alvs_db_manager_poll(void)
{
	int saddr_size, data_size;
	struct sockaddr saddr;
	uint8_t *buffer = malloc(MAX_MSG_SIZE);

	if (buffer == NULL) {
		write_log(LOG_CRIT, "Failed to allocate buffer\n");
		alvs_db_manager_exit_with_error();
	}
	saddr_size = sizeof(saddr);

	while (*alvs_db_manager_cancel_application_flag == false) {
		data_size = recvfrom(raw_sock, buffer, MAX_MSG_SIZE, 0, &saddr, (socklen_t *)&saddr_size);
		if (data_size > 0) {
			process_packet(buffer, data_size, &saddr);
		}
	}
	free(buffer);
}

/******************************************************************************
 * \brief	  Initialization of ALVS.
 *            Also masks SIGTERM signal, will be received in main thread only.
 *
 * \return	  void
 */
void alvs_db_manager_init(void)
{
	sigset_t sigs_to_block;

	sigemptyset(&sigs_to_block);
	sigaddset(&sigs_to_block, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);

	raw_sock = 0;

	alvs_nl_init();
	if (alvs_db_init(alvs_db_manager_cancel_application_flag) != ALVS_DB_OK) {
		write_log(LOG_CRIT, "Failed to create SQL DB.\n");
		alvs_db_manager_exit_with_error();
	}

}

/******************************************************************************
 * \brief         Initialization of all tables handled by the DB manager
 *                Sends get services request. For each service received,
 *                Adds it to DB & sends get dests request. For each server
 *                received, adds it to DB.
 *
 * \return        void
 */
void alvs_db_manager_table_init(void)
{
	struct ip_vs_get_services *get_svcs;
	struct ip_vs_get_dests *dests;
	int i, j;

	/* Get all services */
	get_svcs = alvs_get_services();
	if (get_svcs == NULL) {
		write_log(LOG_CRIT, "Failed to receive IPVS service DB from kernel.\n");
		alvs_db_manager_exit_with_error();
	}
	write_log(LOG_DEBUG, "Initial BD build. service count = %d\n", get_svcs->num_services);
	for (i = 0; i < get_svcs->num_services; i++) {
		/* Add service */
		if (alvs_db_add_service((struct ip_vs_service_user *)(&get_svcs->entrytable[i])) == ALVS_DB_INTERNAL_ERROR) {
			write_log(LOG_CRIT, "Failed to add service during table init - received internal error.\n");
			free(get_svcs);
			alvs_db_manager_exit_with_error();
		}
		/* Get all servers of the current service */
		dests = alvs_get_dests(&get_svcs->entrytable[i]);
		if (dests == NULL) {
			write_log(LOG_CRIT, "Failed to receive IPVS server DB from kernel.\n");
			free(get_svcs);
			alvs_db_manager_exit_with_error();
		}
		for (j = 0; j < dests->num_dests; j++) {
			/* Add server */
			if (alvs_db_add_server((struct ip_vs_service_user *)(&get_svcs->entrytable[i]),
				       (struct ip_vs_dest_user *)(&dests->entrytable[j])) ==  ALVS_DB_INTERNAL_ERROR) {
				write_log(LOG_CRIT, "Failed to add server during table init - received internal error.\n");
				free(get_svcs);
				free(dests);
				alvs_db_manager_exit_with_error();
			}
		}
		free(dests);
	}
	free(get_svcs);
}

/******************************************************************************
 * \brief         Delete ALVS DB & close socket
 *
 * \return        void
 */
void alvs_db_manager_delete(void)
{
	write_log(LOG_DEBUG, "delete ALVS DB manager...\n");
	genl_unregister_family(&alvs_genl_ops);
	if (raw_sock != 0) {
		close(raw_sock);
	}
	alvs_db_destroy();
}

/******************************************************************************
 * \brief    Raises SIGTERM signal to main thread and exits the thread.
 *           Deletes the DB manager.
 *
 * \return   void
 */
void alvs_db_manager_exit_with_error(void)
{
	*alvs_db_manager_cancel_application_flag = true;
	alvs_db_manager_delete();
	kill(getpid(), SIGTERM);
	pthread_exit(NULL);
}

/******************************************************************************
 * \brief         Process packet received from raw socket.
 *                Filters received packets using msg_type = family.
 *                Uses genl_handle_msg to parse the message & call the
 *                registered callback functions.
 *
 * \return        void
 */
void process_packet(uint8_t *buffer, int size, struct sockaddr *saddr)
{
	struct nlmsghdr *hdr = (struct nlmsghdr *)buffer;
	struct sockaddr_nl *nla = (struct sockaddr_nl *)saddr;
	struct nl_msg *msg = NULL;

	if (hdr->nlmsg_type == family && (hdr->nlmsg_flags & NLM_F_REQUEST)) {
		msg = nlmsg_convert(hdr);
		if (msg == NULL) {
			write_log(LOG_ERR, "Error in nlmsg_convert\n");
			return;
		}
		nlmsg_set_proto(msg, NETLINK_GENERIC);
		nlmsg_set_src(msg, nla);
		genl_handle_msg(msg, NULL);
		free(msg);
	}
}

/******************************************************************************
 * \brief         Sends NL message.
 *                Can be used for family initialization & connection test
 *                using msg = NULL
 *
 * \return        int - ret code
 */
int alvs_nl_send_message(struct nl_msg *msg, nl_recvmsg_msg_cb_t func, void *arg)
{
	struct nl_sock *nl_sock = NULL;

	nl_sock = nl_socket_alloc();
	if (nl_sock == NULL) {
		nlmsg_free(msg);
		write_log(LOG_CRIT, "Failed to allocate NL socket\n");
		alvs_db_manager_exit_with_error();
	}
	if (genl_connect(nl_sock) < 0) {
		nlmsg_free(msg);
		nl_socket_free(nl_sock);
		write_log(LOG_CRIT, "Failed to connect to socket\n");
		alvs_db_manager_exit_with_error();
	}
	family = genl_ctrl_resolve(nl_sock, IPVS_GENL_NAME);
	if (family <= 0) {
		nlmsg_free(msg);
		nl_close(nl_sock);
		nl_socket_free(nl_sock);
		write_log(LOG_CRIT, "Unable to resolve family\n");
		alvs_db_manager_exit_with_error();
	}
	/* To test connections and set the family */
	if (msg == NULL) {
		nl_close(nl_sock);
		nl_socket_free(nl_sock);
		return 0;
	}
	if (nl_socket_modify_cb(nl_sock, NL_CB_VALID, NL_CB_CUSTOM, func, arg) != 0) {
		write_log(LOG_ERR, "Unable to modify NL callback\n");
		nlmsg_free(msg);
		nl_close(nl_sock);
		nl_socket_free(nl_sock);
		return 1;
	}
	if (nl_send_auto_complete(nl_sock, msg) < 0) {
		write_log(LOG_ERR, "Unable to send NL message\n");
		nlmsg_free(msg);
		nl_close(nl_sock);
		nl_socket_free(nl_sock);
		return 1;
	}
	if (nl_recvmsgs_default(nl_sock) < 0) {
		write_log(LOG_ERR, "Unable to receive NL message\n");
		nlmsg_free(msg);
		nl_close(nl_sock);
		nl_socket_free(nl_sock);
		return 1;
	}

	nlmsg_free(msg);
	nl_close(nl_sock);
	nl_socket_free(nl_sock);
	return 0;
}

/******************************************************************************
 * \brief         NL initialization.
 *                Allocates, configures and connects to socket.
 *
 * \return        void
 */
void alvs_nl_init(void)
{
	int retcode;
	struct sockaddr_ll sll;
	struct timeval tv;
	struct packet_mreq mreq;
	uint32_t val;

	retcode = genl_register_family(&alvs_genl_ops);
	if (retcode != 0) {
		write_log(LOG_CRIT, "Failed to register protocol family\n");
		alvs_db_manager_exit_with_error();
	}

	/* Connect to netlink to test connection and set the family*/
	if (alvs_nl_send_message(NULL, NULL, NULL) != 0) {
		write_log(LOG_CRIT, "Failed to send NL message.\n");
	}
	write_log(LOG_DEBUG, "IPVS NL family 0x%x\n", family);
	alvs_genl_ops.o_id = family;

	/* Open raw socket */
	raw_sock = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
	if (raw_sock < 0) {
		write_log(LOG_CRIT, "Socket Allocation Error\n");
		alvs_db_manager_exit_with_error();
	}

	bzero(&sll, sizeof(struct sockaddr_ll));
	/* Get the Interface Index */
	sll.sll_ifindex = if_nametoindex("nlmon0");
	if (sll.sll_ifindex == 0) {
		write_log(LOG_CRIT, "Could not find netlink IF nlmon0\n");
		alvs_db_manager_exit_with_error();
	}

	/* Bind our raw socket to this interface */
	sll.sll_family = PF_PACKET;
	sll.sll_protocol = htons(ETH_P_ALL);
	sll.sll_pkttype = PACKET_HOST;
	retcode = bind(raw_sock, (struct sockaddr *)&sll, sizeof(sll));
	if (retcode < 0) {
		write_log(LOG_CRIT, "Error binding raw socket to interface\n");
		alvs_db_manager_exit_with_error();
	}

	/* Set timeout on the raw socket */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(raw_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		write_log(LOG_CRIT, "Error setting socket timeout options\n");
		alvs_db_manager_exit_with_error();
	}

	/* Set raw socket options (taken from tcpdump trace) */
	bzero(&mreq, sizeof(struct packet_mreq));
	mreq.mr_ifindex = sll.sll_ifindex;
	mreq.mr_type = PACKET_MR_PROMISC;
	mreq.mr_alen = 0;
	retcode = setsockopt(raw_sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	if (retcode < 0) {
		write_log(LOG_CRIT, "Error setting socket PACKET_ADD_MEMBERSHIP options\n");
		alvs_db_manager_exit_with_error();
	}
	val = 1;
	retcode = setsockopt(raw_sock, SOL_PACKET, PACKET_AUXDATA, &val, sizeof(val));
	if (retcode < 0) {
		write_log(LOG_CRIT, "Error setting socket PACKET_AUXDATA options\n");
		alvs_db_manager_exit_with_error();
	}
	val = 2;
	retcode = setsockopt(raw_sock, SOL_PACKET, PACKET_VERSION, &val, sizeof(val));
	if (retcode < 0) {
		write_log(LOG_CRIT, "Error setting socket PACKET_VERSION options\n");
		alvs_db_manager_exit_with_error();
	}
	val = 4;
	retcode = setsockopt(raw_sock, SOL_PACKET, PACKET_RESERVE, &val, sizeof(val));
	if (retcode < 0) {
		write_log(LOG_CRIT, "Error setting socket PACKET_RESERVE options\n");
		alvs_db_manager_exit_with_error();
	}
	retcode = fcntl(raw_sock, F_SETFL, O_RDWR|O_NONBLOCK);
	if (retcode != 0) {
		write_log(LOG_CRIT, "Error setting socket to be nonblocking & RDRW\n");
		alvs_db_manager_exit_with_error();
	}

}

/******************************************************************************
 * \brief         Creates IPVS NL message
 *
 * \return        struct nl_msg
 */
struct nl_msg *alvs_nl_message(int cmd, int flags)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();
	if (msg == NULL)
		return NULL;

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, flags,
		    cmd, IPVS_GENL_VERSION);

	return msg;
}

/******************************************************************************
 * \brief         Callback function for all registered IPVS commands
 *                Parses NL messages.
 *
 * \return        int - ret code
 */
static int alvs_msg_parser(struct nl_cache_ops *cache_ops, struct genl_cmd *cmd, struct genl_info *info, void *arg)
{
	int ret = 0;
	enum alvs_db_rc alvs_ret;
	bool need_full_dest = false;
	bool need_full_svc = false;
	struct ip_vs_service_user svc;
	struct ip_vs_dest_user dest;

	write_log(LOG_DEBUG, "Received command %s\n", cmd->c_name);
	/* Flush request */
	if (cmd->c_id == IPVS_CMD_FLUSH) {
		alvs_ret = alvs_db_clear();
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_ERR, "Problem flushing all data.\n");
		}
		return NL_OK;
	}
	if (cmd->c_id == IPVS_CMD_NEW_SERVICE || cmd->c_id == IPVS_CMD_SET_SERVICE) {
		need_full_svc = true;
	}
	/* For all requests need to parse service (except for flush) */
	ret = alvs_genl_parse_service(info->attrs[IPVS_CMD_ATTR_SERVICE], &svc, need_full_svc);
	if (ret < 0)
		return NL_SKIP;

	write_log(LOG_DEBUG, "received service: protocol = %d, addr = 0x%08X, port = %d, fwmark = %d, sched_name = %s\n", svc.protocol, svc.addr, svc.port, svc.fwmark, svc.sched_name);

	if (cmd->c_id == IPVS_CMD_NEW_DEST || cmd->c_id == IPVS_CMD_SET_DEST || cmd->c_id == IPVS_CMD_DEL_DEST) {
		/* For all destination related requests need to parse dest (server) */
		if (cmd->c_id != IPVS_CMD_DEL_DEST) {
			need_full_dest = true;
		}
		ret = alvs_genl_parse_dest(info->attrs[IPVS_CMD_ATTR_DEST], &dest, need_full_dest);
		if (ret < 0)
			return NL_SKIP;
		write_log(LOG_DEBUG, "received dest: addr = 0x%08X, port = %d, weight = %d flags = 0x%08X\n", dest.addr, dest.port, dest.weight, dest.conn_flags);
	}

	switch (cmd->c_id) {
	case IPVS_CMD_NEW_SERVICE:
		alvs_ret = alvs_db_add_service(&svc);
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_NOTICE, "Problem adding service: protocol = %d, addr = 0x%08X, port = %d, sched_name = %s retcode = %d\n", svc.protocol, svc.addr, svc.port, svc.sched_name, alvs_ret);
		}
		break;
	case IPVS_CMD_SET_SERVICE:
		alvs_ret = alvs_db_modify_service(&svc);
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_NOTICE, "Problem updating service: protocol = %d, addr = 0x%08X, port = %d, sched_name = %s retcode = %d\n", svc.protocol, svc.addr, svc.port, svc.sched_name, alvs_ret);
		}
		break;
	case IPVS_CMD_DEL_SERVICE:
		alvs_ret = alvs_db_delete_service(&svc);
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_NOTICE, "Problem deleting service: protocol = %d, addr = 0x%08X, port = %d, sched_name = %s retcode = %d\n", svc.protocol, svc.addr, svc.port, svc.sched_name, alvs_ret);
		}
		break;
	case IPVS_CMD_NEW_DEST:
		alvs_ret = alvs_db_add_server(&svc, &dest);
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_NOTICE, "Problem adding server: addr = 0x%08X, port = %d, weight = %d flags = 0x%08X retcode = %d\n", dest.addr, dest.port, dest.weight, dest.conn_flags, alvs_ret);
		}
		break;
	case IPVS_CMD_SET_DEST:
		alvs_ret = alvs_db_modify_server(&svc, &dest);
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_NOTICE, "Problem updating server: addr = 0x%08X, port = %d, weight = %d flags = 0x%08X retcode = %d\n", dest.addr, dest.port, dest.weight, dest.conn_flags, alvs_ret);
		}
		break;
	case IPVS_CMD_DEL_DEST:
		alvs_ret = alvs_db_delete_server(&svc, &dest);
		if (alvs_ret != ALVS_DB_OK) {
			write_log(LOG_NOTICE, "Problem deleting server: addr = 0x%08X, port = %d, weight = %d flags = 0x%08X retcode = %d\n", dest.addr, dest.port, dest.weight, dest.conn_flags, alvs_ret);
		}
		break;
	default:
		alvs_ret = ALVS_DB_NOT_SUPPORTED;
		write_log(LOG_ERR, "Bad command received. command ID = %d\n", cmd->c_id);
	}


	if (alvs_ret == ALVS_DB_INTERNAL_ERROR || alvs_ret == ALVS_DB_NPS_ERROR) {
		write_log(LOG_CRIT, "Received fatal error from DBs. exiting.\n");
		alvs_db_manager_exit_with_error();
	}

	return NL_OK;
}

/******************************************************************************
 * \brief         Parse get services response message.
 *
 * \return        int - ret code
 */
static int alvs_services_parse_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[IPVS_CMD_ATTR_MAX + 1];
	struct nlattr *svc_attrs[IPVS_SVC_ATTR_MAX + 1];
	struct ip_vs_get_services **getp = (struct ip_vs_get_services **)arg;
	struct ip_vs_get_services *get = (struct ip_vs_get_services *)*getp;
	struct ip_vs_flags flags;
	int i = get->num_services;
	int retcode;

	retcode = genlmsg_parse(nlh, 0, attrs, IPVS_CMD_ATTR_MAX, alvs_cmd_policy);
	if (retcode != 0) {
		write_log(LOG_ERR, "Error parsing get services response message\n");
		return -1;
	}

	if (attrs[IPVS_CMD_ATTR_SERVICE] == NULL) {
		write_log(LOG_ERR, "Error parsing get services response message - no service attributes\n");
		return -1;
	}

	retcode = nla_parse_nested(svc_attrs, IPVS_SVC_ATTR_MAX, attrs[IPVS_CMD_ATTR_SERVICE], alvs_svc_policy);
	if (retcode != 0) {
		write_log(LOG_ERR, "Error parsing get services response message - problem parsing services\n");
		return -1;
	}

	memset(&(get->entrytable[i]), 0, sizeof(get->entrytable[i]));

	if (!(svc_attrs[IPVS_SVC_ATTR_AF] &&
	      (svc_attrs[IPVS_SVC_ATTR_FWMARK] ||
	       (svc_attrs[IPVS_SVC_ATTR_PROTOCOL] &&
		svc_attrs[IPVS_SVC_ATTR_ADDR] &&
		svc_attrs[IPVS_SVC_ATTR_PORT])) &&
	      svc_attrs[IPVS_SVC_ATTR_SCHED_NAME] &&
	      svc_attrs[IPVS_SVC_ATTR_NETMASK] &&
	      svc_attrs[IPVS_SVC_ATTR_TIMEOUT] &&
	      svc_attrs[IPVS_SVC_ATTR_FLAGS])) {
		write_log(LOG_ERR, "Bad service attributes\n");
		return -1;
	}

	if (svc_attrs[IPVS_SVC_ATTR_FWMARK]) {
		get->entrytable[i].fwmark = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_FWMARK]);
	} else {
		get->entrytable[i].protocol = nla_get_u16(svc_attrs[IPVS_SVC_ATTR_PROTOCOL]);
		memcpy(&(get->entrytable[i].addr), nla_data(svc_attrs[IPVS_SVC_ATTR_ADDR]),
		       sizeof(get->entrytable[i].addr));
		get->entrytable[i].port = nla_get_u16(svc_attrs[IPVS_SVC_ATTR_PORT]);
	}

	strncpy(get->entrytable[i].sched_name,
		nla_get_string(svc_attrs[IPVS_SVC_ATTR_SCHED_NAME]),
		IP_VS_SCHEDNAME_MAXLEN);

	get->entrytable[i].netmask = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_NETMASK]);
	get->entrytable[i].timeout = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_TIMEOUT]);
	nla_memcpy(&flags, svc_attrs[IPVS_SVC_ATTR_FLAGS], sizeof(flags));
	get->entrytable[i].flags = flags.flags & flags.mask;

	get->entrytable[i].num_dests = 0;

	i++;

	get->num_services = i;
	get = realloc(get, sizeof(*get)
	      + sizeof(struct ip_vs_service_entry) * (get->num_services + 1));
	*getp = get;
	return 0;
}

/******************************************************************************
 * \brief         Parse get dests response message.
 *
 * \return        int - ret code
 */
static int alvs_dests_parse_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[IPVS_DEST_ATTR_MAX + 1];
	struct nlattr *dest_attrs[IPVS_SVC_ATTR_MAX + 1];
	struct ip_vs_get_dests **dp = (struct ip_vs_get_dests **)arg;
	struct ip_vs_get_dests *d = (struct ip_vs_get_dests *)*dp;
	int i = d->num_dests;
	int retcode;

	retcode = genlmsg_parse(nlh, 0, attrs, IPVS_CMD_ATTR_MAX, alvs_cmd_policy);
	if (retcode != 0) {
		write_log(LOG_ERR, "Cannot parse get dests CB message\n");
		return -1;
	}

	if (attrs[IPVS_CMD_ATTR_DEST] == NULL) {
		write_log(LOG_ERR, "Get dests CB message has no attributes\n");
		return -1;
	}

	retcode = nla_parse_nested(dest_attrs, IPVS_DEST_ATTR_MAX, attrs[IPVS_CMD_ATTR_DEST], alvs_dest_policy);
	if (retcode != 0) {
		write_log(LOG_ERR, "Get dests CB message - parse nested problem\n");
		return -1;
	}

	memset(&(d->entrytable[i]), 0, sizeof(d->entrytable[i]));

	if (!(dest_attrs[IPVS_DEST_ATTR_ADDR] &&
	      dest_attrs[IPVS_DEST_ATTR_PORT] &&
	      dest_attrs[IPVS_DEST_ATTR_FWD_METHOD] &&
	      dest_attrs[IPVS_DEST_ATTR_WEIGHT] &&
	      dest_attrs[IPVS_DEST_ATTR_U_THRESH] &&
	      dest_attrs[IPVS_DEST_ATTR_L_THRESH] &&
	      dest_attrs[IPVS_DEST_ATTR_ACTIVE_CONNS] &&
	      dest_attrs[IPVS_DEST_ATTR_INACT_CONNS] &&
	      dest_attrs[IPVS_DEST_ATTR_PERSIST_CONNS])) {
		write_log(LOG_ERR, "Get dests CB message - bad attributes\n");
		return -1;
	}

	memcpy(&(d->entrytable[i].addr),
	       nla_data(dest_attrs[IPVS_DEST_ATTR_ADDR]),
	       sizeof(d->entrytable[i].addr));
	d->entrytable[i].port = nla_get_u16(dest_attrs[IPVS_DEST_ATTR_PORT]);
	d->entrytable[i].conn_flags = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_FWD_METHOD]);
	d->entrytable[i].weight = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_WEIGHT]);
	d->entrytable[i].u_threshold = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_U_THRESH]);
	d->entrytable[i].l_threshold = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_L_THRESH]);
	d->entrytable[i].activeconns = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_ACTIVE_CONNS]);
	d->entrytable[i].inactconns = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_INACT_CONNS]);
	d->entrytable[i].persistconns = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_PERSIST_CONNS]);

	i++;

	d->num_dests = i;
	d = realloc(d, sizeof(*d) + sizeof(struct ip_vs_dest_entry) * (d->num_dests + 1));
	*dp = d;
	return 0;
}

/******************************************************************************
 * \brief         Send get services request to kernel using NL message.
 *                returns the service list received and parsed by the callback.
 *
 * \return        struct ip_vs_get_services
 */
struct ip_vs_get_services *alvs_get_services(void)
{
	struct ip_vs_get_services *get;
	socklen_t len;
	struct nl_msg *msg;

	len = sizeof(*get) + sizeof(struct ip_vs_service_entry);
	get = malloc(len);
	if (get == NULL) {
		write_log(LOG_ERR, "Failed to allocate memory for service list\n");
		return NULL;
	}
	get->num_services = 0;

	msg = alvs_nl_message(IPVS_CMD_GET_SERVICE, NLM_F_DUMP);
	if (msg == NULL) {
		write_log(LOG_ERR, "Failed to allocate NL message\n");
		free(get);
		return NULL;
	}

	if (alvs_nl_send_message(msg, alvs_services_parse_cb, &get) == 0)
		return get;


	write_log(LOG_ERR, "Failed to send NL IPVS_CMD_GET_SERVICE message\n");
	free(get);
	return NULL;
}

/******************************************************************************
 * \brief         Parses destination received in NL message using dest attr
 *
 * \return        int - ret code
 */
static int alvs_genl_parse_service(struct nlattr *nla, struct ip_vs_service_user *ret_svc, bool need_full_svc)
{
	struct nlattr *attrs[IPVS_SVC_ATTR_MAX + 1];
	struct nlattr *nla_af, *nla_port, *nla_fwmark, *nla_protocol, *nla_addr;
	struct nlattr *nla_sched, *nla_flags, *nla_timeout, *nla_netmask;
	struct ip_vs_flags flags;

	/* Parse mandatory identifying service fields first */
	if (nla == NULL || nla_parse_nested(attrs, IPVS_SVC_ATTR_MAX, nla, alvs_svc_policy)) {
		printf("Error parsing service attributed");
		return -1;
	}

	nla_af          = attrs[IPVS_SVC_ATTR_AF];
	nla_protocol    = attrs[IPVS_SVC_ATTR_PROTOCOL];
	nla_addr        = attrs[IPVS_SVC_ATTR_ADDR];
	nla_port        = attrs[IPVS_SVC_ATTR_PORT];
	nla_fwmark      = attrs[IPVS_SVC_ATTR_FWMARK];

	if (!(nla_af && (nla_fwmark || (nla_port && nla_protocol && nla_addr)))) {
		printf("Error - bad service attribute");
		return -1;
	}

	memset(ret_svc, 0, sizeof(struct ip_vs_service_user));

	if (nla_get_u16(nla_af) != AF_INET) {
		printf("Error - Not IPV4");
		return -1;
	}

	if (nla_fwmark) {
		ret_svc->protocol = IPPROTO_TCP;
		ret_svc->fwmark = nla_get_u32(nla_fwmark);
	} else {
		ret_svc->protocol = nla_get_u16(nla_protocol);
		nla_memcpy(&ret_svc->addr, nla_addr, sizeof(ret_svc->addr));
		ret_svc->port = nla_get_u16(nla_port); /* was nla_get_be16 */
		ret_svc->fwmark = 0;
	}

	/* If a full entry was requested, check for the additional fields */
	if (need_full_svc) {
		nla_sched = attrs[IPVS_SVC_ATTR_SCHED_NAME];
		nla_flags = attrs[IPVS_SVC_ATTR_FLAGS];
		nla_timeout = attrs[IPVS_SVC_ATTR_TIMEOUT];
		nla_netmask = attrs[IPVS_SVC_ATTR_NETMASK];
		nla_memcpy(&flags, nla_flags, sizeof(flags));

		/* Prefill flags from service if it already exists */
		alvs_db_get_service_flags(ret_svc);

		/* set new flags from user */
		ret_svc->flags = (ret_svc->flags & ~flags.mask) | (flags.flags & flags.mask);
		memcpy(ret_svc->sched_name, nla_data(nla_sched), nla_len(nla_sched));
		ret_svc->timeout = nla_get_u32(nla_timeout);
		ret_svc->netmask = nla_get_u32(nla_netmask);	/* was nla_get_be32 */
	}

	return 0;
}
/******************************************************************************
 * \brief         Parses destination received in NL message using dest attr
 *
 * \return        int - ret code
 */
static int alvs_genl_parse_dest(struct nlattr *nla, struct ip_vs_dest_user *ret_dest, bool need_full_dest)
{
	struct nlattr *attrs[IPVS_DEST_ATTR_MAX + 1];
	struct nlattr *nla_addr, *nla_port;
	struct nlattr *nla_fwd, *nla_weight, *nla_u_thresh,  *nla_l_thresh;

	/* Parse mandatory identifying destination fields first */
	if (nla == NULL || nla_parse_nested(attrs, IPVS_DEST_ATTR_MAX, nla, alvs_dest_policy)) {
		printf("Error parsing dest attributes\n");
		return -1;
	}

	nla_addr = attrs[IPVS_DEST_ATTR_ADDR];
	nla_port = attrs[IPVS_DEST_ATTR_PORT];

	if (!(nla_addr && nla_port)) {
		printf("Error - bad dest attribute");
		return -1;
	}

	memset(ret_dest, 0, sizeof(struct ip_vs_dest_user));
	nla_memcpy(&ret_dest->addr, nla_addr, sizeof(ret_dest->addr));
	ret_dest->port = nla_get_u16(nla_port); /* was nla_get_be16 */

	/* If a full entry was requested, check for the additional fields */
	if (need_full_dest) {
		nla_fwd	= attrs[IPVS_DEST_ATTR_FWD_METHOD];
		nla_weight = attrs[IPVS_DEST_ATTR_WEIGHT];
		nla_u_thresh = attrs[IPVS_DEST_ATTR_U_THRESH];
		nla_l_thresh = attrs[IPVS_DEST_ATTR_L_THRESH];
		ret_dest->conn_flags = nla_get_u32(nla_fwd) & IP_VS_CONN_F_FWD_MASK;
		ret_dest->weight = nla_get_u32(nla_weight);
		ret_dest->u_threshold = nla_get_u32(nla_u_thresh);
		ret_dest->l_threshold = nla_get_u32(nla_l_thresh);
	}

	return 0;
}

/******************************************************************************
 * \brief         Send get dests request to kernel using NL message.
 *                returns the destination (server) list received and parsed by
 *                the callback.
 *
 * \return        struct ip_vs_get_dests
 */
struct ip_vs_get_dests *alvs_get_dests(struct ip_vs_service_entry *svc)
{
	struct ip_vs_get_dests *d;
	socklen_t len;
	struct nl_msg *msg;
	struct nlattr *nl_service;
	union nf_inet_addr addr;

	len = sizeof(*d) + sizeof(struct ip_vs_dest_entry) * svc->num_dests;
	d = malloc(len);
	if (d == NULL) {
		write_log(LOG_ERR, "Failed to allocate destination list\n");
		return NULL;
	}

	if (svc->num_dests == 0) {
		d = realloc(d, sizeof(*d) + sizeof(struct ip_vs_dest_entry));
		if (d == NULL) {
			write_log(LOG_ERR, "Failed to allocate dest\n");
			return NULL;
		}
	}
	d->fwmark = svc->fwmark;
	d->protocol = svc->protocol;
	d->addr = svc->addr;
	d->port = svc->port;
	d->num_dests = svc->num_dests;

	msg = alvs_nl_message(IPVS_CMD_GET_DEST, NLM_F_DUMP);
	if (msg == NULL) {
		write_log(LOG_ERR, "Failed to allocate message\n");
		free(d);
		return NULL;
	}

	nl_service = nla_nest_start(msg, IPVS_CMD_ATTR_SERVICE);
	if (nl_service == NULL) {
		write_log(LOG_ERR, "Failed to write service to message\n");
		nlmsg_free(msg);
		free(d);
		return NULL;
	}
	NLA_PUT_U16(msg, IPVS_SVC_ATTR_AF, AF_INET);
	if (svc->fwmark) {
		NLA_PUT_U32(msg, IPVS_SVC_ATTR_FWMARK, ntohl(svc->fwmark));
	} else {
		addr.ip = svc->addr;
		write_log(LOG_DEBUG, "Fill service details into message: protocol = 0x%x addr = 0x%x port = 0x%x\n", svc->protocol, svc->addr, svc->port);
		NLA_PUT_U16(msg, IPVS_SVC_ATTR_PROTOCOL, svc->protocol);
		NLA_PUT(msg, IPVS_SVC_ATTR_ADDR, sizeof(union nf_inet_addr), &addr);
		NLA_PUT_U16(msg, IPVS_SVC_ATTR_PORT, svc->port);
	}

	write_log(LOG_DEBUG, "Send get dests message\n");
	nla_nest_end(msg, nl_service);
	if (alvs_nl_send_message(msg, alvs_dests_parse_cb, &d) != 0) {
		write_log(LOG_ERR, "Failed to send get dests message\n");
		free(d);
		return NULL;
	}

	return d;

nla_put_failure:
	nlmsg_free(msg);
	free(d);
	return NULL;
}

/******************************************************************************
 * \brief    Constructor function for all ALVS data bases.
 *           This function is called not from the network thread but from the
 *           main thread on NPS configuration bringup.
 *
 * \return   bool
 */
bool alvs_db_constructor(void)
{
	struct infra_hash_params hash_params;
	struct infra_table_params table_params;
	bool retcode;

	write_log(LOG_DEBUG, "Creating service classification table.\n");
	hash_params.key_size = sizeof(struct alvs_service_classification_key);
	hash_params.result_size = sizeof(struct alvs_service_classification_result);
	hash_params.max_num_of_entries = ALVS_SERVICES_MAX_ENTRIES;
	hash_params.hash_size = 0;
	hash_params.updated_from_dp = false;
	retcode = infra_create_hash(STRUCT_ID_ALVS_SERVICE_CLASSIFICATION, INFRA_EMEM_SEARCH_HASH_HEAP, &hash_params);
	if (retcode == false) {
		write_log(LOG_CRIT, "Failed to create alvs service classification hash.\n");
		return false;
	}

	write_log(LOG_DEBUG, "Creating service info table.\n");
	table_params.key_size = sizeof(struct alvs_service_info_key);
	table_params.result_size = sizeof(struct alvs_service_info_result);
	table_params.max_num_of_entries = ALVS_SERVICES_MAX_ENTRIES;
	retcode = infra_create_table(STRUCT_ID_ALVS_SERVICE_INFO, INFRA_X4_CLUSTER_SEARCH_HEAP, &table_params);
	if (retcode == false) {
		write_log(LOG_CRIT, "Failed to create alvs service info table.\n");
		return false;
	}

	write_log(LOG_DEBUG, "Creating scheduling info table.\n");
	table_params.key_size = sizeof(struct alvs_sched_info_key);
	table_params.result_size = sizeof(struct alvs_sched_info_result);
	table_params.max_num_of_entries = ALVS_SCHED_MAX_ENTRIES;
	retcode = infra_create_table(STRUCT_ID_ALVS_SCHED_INFO, INFRA_EMEM_SEARCH_TABLE_HEAP, &table_params);
	if (retcode == false) {
		write_log(LOG_CRIT, "Failed to create alvs scheduling info table.\n");
		return false;
	}

	write_log(LOG_DEBUG, "Creating server info table.\n");
	table_params.key_size = sizeof(struct alvs_server_info_key);
	table_params.result_size = sizeof(struct alvs_server_info_result);
	table_params.max_num_of_entries = ALVS_SERVERS_MAX_ENTRIES;
	retcode = infra_create_table(STRUCT_ID_ALVS_SERVER_INFO, INFRA_EMEM_SEARCH_TABLE_HEAP, &table_params);
	if (retcode == false) {
		write_log(LOG_CRIT, "Failed to create alvs server info table.\n");
		return false;
	}

	write_log(LOG_DEBUG, "Creating connection classification table.\n");
	hash_params.key_size = sizeof(struct alvs_conn_classification_key);
	hash_params.result_size = sizeof(struct alvs_conn_classification_result);
	hash_params.max_num_of_entries = ALVS_CONN_MAX_ENTRIES;
	hash_params.hash_size = 26;
	hash_params.updated_from_dp = true;
	hash_params.sig_pool_id = 0;
	hash_params.result_pool_id = 1;
	retcode = infra_create_hash(STRUCT_ID_ALVS_CONN_CLASSIFICATION, INFRA_EMEM_SEARCH_HASH_HEAP, &hash_params);
	if (retcode == false) {
		write_log(LOG_CRIT, "Failed to create alvs conn classification hash.\n");
		return false;
	}

	printf("Creating connection info table.\n");
	table_params.key_size = sizeof(struct alvs_conn_info_key);
	table_params.result_size = sizeof(struct alvs_conn_info_result);
	table_params.max_num_of_entries = ALVS_CONN_MAX_ENTRIES;
	retcode = infra_create_table(STRUCT_ID_ALVS_CONN_INFO, INFRA_EMEM_SEARCH_TABLE_HEAP, &table_params);
	if (retcode == false) {
		write_log(LOG_CRIT, "Failed to create alvs conn info table.\n");
		return false;
	}

	return true;
}
