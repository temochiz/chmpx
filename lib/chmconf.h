/*
 * CHMPX
 *
 * Copyright 2014 Yahoo! JAPAN corporation.
 *
 * CHMPX is inprocess data exchange by MQ with consistent hashing.
 * CHMPX is made for the purpose of the construction of
 * original messaging system and the offer of the client
 * library.
 * CHMPX transfers messages between the client and the server/
 * slave. CHMPX based servers are dispersed by consistent
 * hashing and are automatically layouted. As a result, it
 * provides a high performance, a high scalability.
 *
 * For the full copyright and license information, please view
 * the LICENSE file that was distributed with this source code.
 *
 * AUTHOR:   Takeshi Nakatani
 * CREATE:   Tue July 1 2014
 * REVISION:
 *
 */
#ifndef	CHMCONF_H
#define	CHMCONF_H

#include <k2hash/k2hshm.h>
#include <string>
#include <list>

#include "chmcommon.h"
#include "chmutil.h"
#include "chmstructure.h"
#include "chmeventbase.h"

//---------------------------------------------------------
// Symbols
//---------------------------------------------------------
#define	UNINITIALIZE_REVISION			(-1)

//---------------------------------------------------------
// Structures
//---------------------------------------------------------
// configuration information for node
typedef struct chm_node_cfg_info{
	std::string		name;
	short			port;
	short			ctlport;
	bool			is_ssl;
	bool			verify_peer;		// verify ssl client peer on server
	bool			is_ca_file;
	std::string		capath;
	std::string		server_cert;
	std::string		server_prikey;
	std::string		slave_cert;
	std::string		slave_prikey;

	chm_node_cfg_info() : name(""), port(0), ctlport(0), is_ssl(false), verify_peer(false), is_ca_file(false), capath(""), server_cert(""), server_prikey(""), slave_cert(""), slave_prikey("") {}

	bool compare(const struct chm_node_cfg_info& other) const
	{
		if(	name			== other.name			&&
			port			== other.port			&&
			ctlport			== other.ctlport		&&
			is_ssl			== other.is_ssl			&&
			verify_peer		== other.verify_peer	&&
			is_ca_file		== other.is_ca_file		&&
			capath			== other.capath			&&
			server_cert		== other.server_cert	&&
			server_prikey	== other.server_prikey	&&
			slave_cert		== other.slave_cert		&&
			slave_prikey	== other.slave_prikey	)
		{
			return true;
		}
		return false;
	}
	bool operator==(const struct chm_node_cfg_info& other) const
	{
		return compare(other);
	}
	bool operator!=(const struct chm_node_cfg_info& other) const
	{
		return !compare(other);
	}
}CHMNODE_CFGINFO, *PCHMNODE_CFGINFO;

typedef std::list<CHMNODE_CFGINFO>	chmnode_cfginfos_t;

struct chm_node_cfg_info_sort
{
	bool operator()(const CHMNODE_CFGINFO& lchmnodecfginfo, const CHMNODE_CFGINFO& rchmnodecfginfo) const
    {
		if(lchmnodecfginfo.name == rchmnodecfginfo.name){
			if(lchmnodecfginfo.port == rchmnodecfginfo.port){
				return lchmnodecfginfo.ctlport < rchmnodecfginfo.ctlport;
			}else{
				return lchmnodecfginfo.port < rchmnodecfginfo.port;
			}
		}else{
			return lchmnodecfginfo.name < rchmnodecfginfo.name;
		}
    }
};

struct chm_node_cfg_info_same_name
{
	bool operator()(const CHMNODE_CFGINFO& lchmnodecfginfo, const CHMNODE_CFGINFO& rchmnodecfginfo) const
    {
		return lchmnodecfginfo.name == rchmnodecfginfo.name;
    }
};

struct chm_node_cfg_info_same_name_port
{
	bool operator()(const CHMNODE_CFGINFO& lchmnodecfginfo, const CHMNODE_CFGINFO& rchmnodecfginfo) const
    {
		return (lchmnodecfginfo.name == rchmnodecfginfo.name && lchmnodecfginfo.ctlport == rchmnodecfginfo.ctlport);
    }
};

// configuration information for all
typedef struct chm_cfg_info{
	std::string			groupname;
	long				revision;
	bool				is_server_mode;
	bool				is_random_mode;
	short				self_ctlport;
	long				max_chmpx_count;
	long				replica_count;
	long				max_server_mq_cnt;			// MQ count for server/slave node
	long				max_client_mq_cnt;			// MQ count for client process
	long				mqcnt_per_attach;			// MQ count at each attaching from each client process
	long				max_q_per_servermq;			// Queue count in one MQ on server/slave node
	long				max_q_per_clientmq;			// Queue count in one MQ on client process
	long				max_mq_per_client;			// max MQ count for one client process
	long				max_histlog_count;
	int					retrycnt;
	int					mq_retrycnt;
	bool				mq_ack;
	int					timeout_wait_socket;
	int					timeout_wait_connect;
	int					timeout_wait_mq;
	bool				is_auto_merge;
	bool				is_do_merge;
	time_t				timeout_merge;
	int					sock_thread_cnt;
	int					mq_thread_cnt;
	int					max_sock_pool;
	time_t				sock_pool_timeout;
	bool				k2h_fullmap;
	int					k2h_mask_bitcnt;
	int					k2h_cmask_bitcnt;
	int					k2h_max_element;
	time_t				date;
	chmnode_cfginfos_t	servers;
	chmnode_cfginfos_t	slaves;

	chm_cfg_info() :	groupname(""), revision(UNINITIALIZE_REVISION), is_server_mode(false), is_random_mode(false), self_ctlport(CHM_INVALID_PORT),
						max_chmpx_count(0L), replica_count(0L), max_server_mq_cnt(0L), max_client_mq_cnt(0L), mqcnt_per_attach(0L), max_q_per_servermq(0L),
						max_q_per_clientmq(0L), max_mq_per_client(0L), max_histlog_count(0L), retrycnt(-1), mq_retrycnt(-1), mq_ack(true), timeout_wait_socket(-1),
						timeout_wait_connect(-1), timeout_wait_mq(-1), is_auto_merge(false), is_do_merge(false), timeout_merge(0), sock_thread_cnt(0),
						mq_thread_cnt(0), max_sock_pool(1), sock_pool_timeout(0), k2h_fullmap(true), k2h_mask_bitcnt(K2HShm::DEFAULT_MASK_BITCOUNT), 
						k2h_cmask_bitcnt(K2HShm::DEFAULT_COLLISION_MASK_BITCOUNT), k2h_max_element(K2HShm::DEFAULT_MAX_ELEMENT_CNT), date(0L) {}

	bool compare(const struct chm_cfg_info& other) const
	{
		if(	groupname			== other.groupname				&&
			revision			== other.revision				&&
			is_server_mode		== other.is_server_mode			&&
			is_random_mode		== other.is_random_mode			&&
			self_ctlport		== other.self_ctlport			&&
			max_chmpx_count		== other.max_chmpx_count		&&
			replica_count		== other.replica_count			&&
			max_server_mq_cnt	== other.max_server_mq_cnt		&&
			max_client_mq_cnt	== other.max_client_mq_cnt		&&
			mqcnt_per_attach	== other.mqcnt_per_attach		&&
			max_q_per_servermq	== other.max_q_per_servermq		&&
			max_q_per_clientmq	== other.max_q_per_clientmq		&&
			max_mq_per_client	== other.max_mq_per_client		&&
			max_histlog_count	== other.max_histlog_count		&&
			retrycnt			== other.retrycnt				&&
			mq_retrycnt			== other.mq_retrycnt			&&
			mq_ack				== other.mq_ack					&&
			timeout_wait_socket	== other.timeout_wait_socket	&&
			timeout_wait_connect== other.timeout_wait_connect	&&
			timeout_wait_mq		== other.timeout_wait_mq		&&
			is_auto_merge		== other.is_auto_merge			&&
			is_do_merge			== other.is_do_merge			&&
			timeout_merge		== other.timeout_merge			&&
			sock_thread_cnt		== other.sock_thread_cnt		&&
			mq_thread_cnt		== other.mq_thread_cnt			&&
			max_sock_pool		== other.max_sock_pool			&&
			sock_pool_timeout	== other.sock_pool_timeout		&&
			//k2h_fullmap		== other.k2h_fullmap			&&	// [NOTICE] k2hash parameter is not compared
			//k2h_mask_bitcnt	== other.k2h_mask_bitcnt		&&
			//k2h_cmask_bitcnt	== other.k2h_cmask_bitcnt		&&
			//k2h_max_element	== other.k2h_max_element		&&
			//date				== other.date					&&	// [NOTICE] date is not compared
			servers				== other.servers				&&
			slaves				== other.slaves					)
		{
			return true;
		}
		return false;
	}
	bool operator==(const struct chm_cfg_info& other) const
	{
		return compare(other);
	}
	bool operator!=(const struct chm_cfg_info& other) const
	{
		return !compare(other);
	}
}CHMCFGINFO, *PCHMCFGINFO;

// raw all configuration
typedef struct cfg_raw{
	strmap_t	global;
	strmaparr_t	server_nodes;
	strmaparr_t	slave_nodes;
}CFGRAW, *PCFGRAW;

// For using temporary carry common values for loading
//
typedef struct chm_conf_common_carry_value{
	short		port;
	short		ctlport;
	bool		server_mode;
	bool		is_ssl;
	bool		verify_peer;
	bool		is_ca_file;
	std::string	capath;
	std::string	server_cert;
	std::string	server_prikey;
	std::string	slave_cert;
	std::string	slave_prikey;
	bool		is_server_by_ctlport;			// for check server/slave mode by checking control port and server name.
	bool		found_ssl;
	bool		found_ssl_verify_peer;

	chm_conf_common_carry_value() :	port(CHM_INVALID_PORT), ctlport(CHM_INVALID_PORT), server_mode(false), is_ssl(false), verify_peer(false),
									is_ca_file(false), capath(""), server_cert(""), server_prikey(""), slave_cert(""), slave_prikey(""),
									is_server_by_ctlport(false), found_ssl(false), found_ssl_verify_peer(false) {}
}CHMCONF_CCV, *PCHMCONF_CCV;

//---------------------------------------------------------
// Class CHMConf
//---------------------------------------------------------
class CHMConf : public ChmEventBase
{
	public:
		enum ConfType{
			CONF_UNKNOWN = 0,
			CONF_INI,
			CONF_JSON,
			CONF_JSON_STR,	// JSON string type(not file), On this case, this class does not use inotify.
			CONF_YAML
		};

	protected:
		std::string			cfgfile;
		std::string			strjson;
		short				ctlport_param;
		CHMConf::ConfType	type;
		int					inotifyfd;
		int					watchfd;
		PCHMCFGINFO			pchmcfginfo;

	protected:
		CHMConf(int eventqfd = CHM_INVALID_HANDLE, ChmCntrl* pcntrl = NULL, const char* file = NULL, short ctlport = CHM_INVALID_PORT, const char* pJson = NULL);

		virtual bool LoadConfigration(CHMCFGINFO& chmcfginfo) const = 0;

		bool GetServerInfo(const char* hostname, short ctlport, CHMNODE_CFGINFO& svrnodeinfo, bool is_check_update = false);
		bool GetSelfServerInfo(CHMNODE_CFGINFO& svrnodeinfo, bool is_check_update = false);
		bool GetSlaveInfo(const char* hostname, short ctlport, CHMNODE_CFGINFO& slvnodeinfo, bool is_check_update = false);
		bool GetSelfSlaveInfo(CHMNODE_CFGINFO& slvnodeinfo, bool is_check_update = false);

		bool IsWatching(void) const { return (CHM_INVALID_HANDLE != watchfd); }
		uint CheckNotifyEvent(void);
		bool ResetEventQueue(void);

		bool IsFileType(void) const { return (CONF_INI == type || CONF_YAML == type || CONF_JSON == type); }
		bool IsJsonStringType(void) const { return (CONF_JSON_STR == type); }

	public:
		static CHMConf* GetCHMConf(int eventqfd, ChmCntrl* pcntrl, const char* config, short ctlport = CHM_INVALID_PORT, bool is_check_env = true, std::string* normalize_config = NULL);	// Class Factory
		virtual ~CHMConf();

		virtual bool Clean(void);

		virtual bool GetEventQueueFds(event_fds_t& fds);
		virtual bool SetEventQueue(void);
		virtual bool UnsetEventQueue(void);
		virtual bool IsEventQueueFd(int fd);

		virtual bool Send(PCOMPKT pComPkt, const unsigned char* pbody, size_t blength);
		virtual bool Receive(int fd);
		virtual bool NotifyHup(int fd);
		virtual bool Processing(PCOMPKT pComPkt);

		bool CheckConfFile(void) const;
		bool CheckUpdate(void);
		const CHMCFGINFO* GetConfiguration(bool is_check_update = false);

		bool GetNodeInfo(const char* hostname, short ctlport, CHMNODE_CFGINFO& nodeinfo, bool is_only_server, bool is_check_update = false);
		bool GetSelfNodeInfo(CHMNODE_CFGINFO& nodeinfo, bool is_check_update = false);

		bool GetServerList(strlst_t& server_list);
		bool IsServerList(const char* hostname, std::string& fqdn);
		bool IsServerList(std::string& fqdn);

		bool GetSlaveList(strlst_t& slave_list);
		bool IsSlaveList(const char* hostname, std::string& fqdn);
		bool IsSlaveList(std::string& fqdn);
		bool IsSsl(void) const;
};

//---------------------------------------------------------
// Class CHMIniConf
//---------------------------------------------------------
class CHMIniConf : public CHMConf
{
	friend class CHMConf;

	protected:
		CHMIniConf(int eventqfd = CHM_INVALID_HANDLE, ChmCntrl* pcntrl = NULL, const char* file = NULL, short ctlport = CHM_INVALID_PORT);

		virtual bool LoadConfigration(CHMCFGINFO& chmcfginfo) const;
		bool LoadConfigrationRaw(CFGRAW& chmcfgraw) const;
		bool ReadFileContents(const std::string& filename, strlst_t& linelst, strlst_t& allfiles) const;

	public:
		virtual ~CHMIniConf();
};

//---------------------------------------------------------
// Class CHMYamlBaseConf
//---------------------------------------------------------
class CHMYamlBaseConf : public CHMConf
{
	friend class CHMConf;

	protected:
		CHMYamlBaseConf(int eventqfd = CHM_INVALID_HANDLE, ChmCntrl* pcntrl = NULL, const char* file = NULL, short ctlport = CHM_INVALID_PORT, const char* pJson = NULL);

		virtual bool LoadConfigration(CHMCFGINFO& chmcfginfo) const;

	public:
		virtual ~CHMYamlBaseConf();
};

//---------------------------------------------------------
// Class CHMJsonConf
//---------------------------------------------------------
class CHMJsonConf : public CHMYamlBaseConf
{
	friend class CHMConf;

	protected:
		CHMJsonConf(int eventqfd = CHM_INVALID_HANDLE, ChmCntrl* pcntrl = NULL, const char* file = NULL, short ctlport = CHM_INVALID_PORT);

	public:
		virtual ~CHMJsonConf();
};

//---------------------------------------------------------
// Class CHMJsonStringConf
//---------------------------------------------------------
class CHMJsonStringConf : public CHMYamlBaseConf
{
	friend class CHMConf;

	protected:
		CHMJsonStringConf(int eventqfd = CHM_INVALID_HANDLE, ChmCntrl* pcntrl = NULL, const char* pJson = NULL, short ctlport = CHM_INVALID_PORT);

	public:
		virtual ~CHMJsonStringConf();
};

//---------------------------------------------------------
// Class CHMYamlConf
//---------------------------------------------------------
class CHMYamlConf : public CHMYamlBaseConf
{
	friend class CHMConf;

	protected:
		CHMYamlConf(int eventqfd = CHM_INVALID_HANDLE, ChmCntrl* pcntrl = NULL, const char* file = NULL, short ctlport = CHM_INVALID_PORT);

	public:
		virtual ~CHMYamlConf();
};

#endif	// CHMCONF_H

/*
 * VIM modelines
 *
 * vim:set ts=4 fenc=utf-8:
 */
