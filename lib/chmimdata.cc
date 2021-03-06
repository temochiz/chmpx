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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <string>

#include "chmcommon.h"
#include "chmconf.h"
#include "chmstructure.tcc"
#include "chmimdata.h"
#include "chmnetdb.h"
#include "chmeventmq.h"
#include "chmeventsock.h"
#include "chmutil.h"
#include "chmregex.h"
#include "chmlock.h"
#include "chmdbg.h"

using namespace std;

//---------------------------------------------------------
// Symbols
//---------------------------------------------------------
#define	CHMSHM_SHMFILE_EXT			".chmpx"
#define	CHMSHM_K2HASH_EXT			".k2h"
#define	CHMSHM_BACKUP_EXT			"bup"
#define	CHMSHM_FILE_BASEDIR			"/tmp"

//---------------------------------------------------------
// Class Valiables
//---------------------------------------------------------
const int	ChmIMData::SYSPAGE_SIZE;

//---------------------------------------------------------
// Class Methods
//---------------------------------------------------------
bool ChmIMData::MakeFilePath(const char* groupname, short port, MKFPMODE mode, string& shmpath)
{
	if(ISEMPTYSTR(groupname)){
		ERR_CHMPRN("parameter is wrong.");
		return false;
	}
	shmpath = CHMSHM_FILE_BASEDIR;
	shmpath += "/";
	shmpath += groupname;
	if(CHM_INVALID_PORT != port){
		shmpath += "-";
		shmpath += to_string(port);
	}
	shmpath += ChmIMData::MKFILEPATH_SHM == mode ? CHMSHM_SHMFILE_EXT : CHMSHM_K2HASH_EXT;

	return true;
}

bool ChmIMData::MakeShmFilePath(const char* groupname, short port, string& shmpath)
{
	return ChmIMData::MakeFilePath(groupname, port, ChmIMData::MKFILEPATH_SHM, shmpath);
}

bool ChmIMData::MakeK2hashFilePath(const char* groupname, short port, string& shmpath)
{
	return ChmIMData::MakeFilePath(groupname, port, ChmIMData::MKFILEPATH_K2H, shmpath);
}

bool ChmIMData::CompareChmpxSvrs(PCHMPXSVR pbase, long bcount, PCHMPXSVR pmerge, long mcount, bool is_status)
{
	if(!pbase && 0L == bcount && !pmerge && 0L == mcount){
		return true;
	}
	if(!pbase || bcount <= 0L || !pmerge || mcount <= 0L){
		return false;
	}
	if(bcount != mcount){
		return false;
	}

	for(long mcnt = 0; mcnt < mcount; mcnt++){
		long	bcnt;
		for(bcnt = 0; bcnt < bcount; bcnt++){
			if(pbase[bcnt].chmpxid == pmerge[mcnt].chmpxid){
				break;
			}
		}
		if(bcount <= bcnt){
			return false;
		}
		if(	0 != strcmp(pbase[bcnt].name, pmerge[mcnt].name)	||
			pbase[bcnt].base_hash	!= pmerge[mcnt].base_hash	||
			pbase[bcnt].port		!= pmerge[mcnt].port		||
			pbase[bcnt].ctlport		!= pmerge[mcnt].ctlport		||
			!CMP_CHMPXSSL(pbase[bcnt].ssl, pmerge[mcnt].ssl)	||
			(is_status && pbase[bcnt].status != pmerge[mcnt].status) )
		{
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------
// Methods
//---------------------------------------------------------
ChmIMData::ChmIMData(bool is_chmpx_proc) : ShmPath(""), pChmShm(NULL), ShmFd(CHM_INVALID_HANDLE), ShmSize(0L), pConfObj(NULL), pK2hash(NULL), eqfd(CHM_INVALID_HANDLE), isChmpxProc(is_chmpx_proc), ChmpxPid(CHM_INVALID_PID)
{
}

ChmIMData::~ChmIMData()
{
	Close();
}

bool ChmIMData::Close()
{
	bool	result = true;

	if(isChmpxProc && pChmShm){
		chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
		tmpchminfo.Close(eqfd);
	}
	if(!CloseShm()){
		MSG_CHMPRN("Failed to close SHM, but continue...");
		result = false;
	}
	if(!CloseK2hash()){
		MSG_CHMPRN("Failed to close K2hash, but continue...");
		result = false;
	}
	pConfObj= NULL;
	eqfd	= CHM_INVALID_HANDLE;

	return result;
}

bool ChmIMData::CloseSocks(int type)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.Close(eqfd, type);
}

bool ChmIMData::Dump(stringstream& sstream) const
{
	sstream << "==========================================================================" << endl;
	sstream << "Shared memory file path   = " << ShmPath	<< endl;
	sstream << "Shared memory file size   = " << ShmSize	<< endl;
	sstream << "Shared memory file fd     = " << ShmFd		<< endl;
	sstream << "Shared memory file Object = " << pChmShm	<< endl;
	sstream << "Configration object       = " << pConfObj	<< endl;
	sstream << "K2HASH Object             = " << pK2hash->GetK2hashFilePath() << endl;
	sstream << "==========================================================================" << endl;
	sstream << "Shared memory file Object" << endl;
	sstream << "--------------------------------------------------------------------------" << endl;

	if(!IsAttachedShm()){
		sstream << "*** NOT MAPPING" << endl;
	}else{
		chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
		tmpchminfo.Dump(sstream, NULL);
	}
	sstream << "==========================================================================" << endl;

	return true;
}

bool ChmIMData::DumpSelfChmpxSvr(stringstream& sstream) const
{
	CHMPXSVR	chmpxsvr;
	if(!GetSelfChmpxSvr(&chmpxsvr)){
		return false;
	}
	sstream << "CHMPX HOSTNAME            = " << chmpxsvr.name								<< endl;
	sstream << "MODE                      = " << (IsServerMode() ? "SERVER" : "SLAVE")		<< endl;
	sstream << "PORT                      = " << chmpxsvr.port								<< endl;
	sstream << "CONTROL PORT              = " << chmpxsvr.ctlport							<< endl;
	sstream << "SSL                       = " << (chmpxsvr.ssl.is_ssl ? "YES" : "NO")		<< endl;
	if(chmpxsvr.ssl.is_ssl){
		sstream << "VERIFY PEER               = " << (chmpxsvr.ssl.verify_peer ? "YES" : "NO")	<< endl;
		sstream << "CA PATH TYPE              = " << (chmpxsvr.ssl.is_ca_file ? "FILE" : "DIR")	<< endl;
		sstream << "CA PATH                   = " << chmpxsvr.ssl.capath						<< endl;
		sstream << "SERVER CERT               = " << chmpxsvr.ssl.server_cert					<< endl;
		sstream << "SERVER PRIKEY             = " << chmpxsvr.ssl.server_prikey					<< endl;
		sstream << "SLAVE CERT                = " << chmpxsvr.ssl.slave_cert					<< endl;
		sstream << "SLAVE PRIKEY              = " << chmpxsvr.ssl.slave_prikey					<< endl;
	}
	sstream << "BASE HASH                 = " << to_hexstring(chmpxsvr.base_hash)		<< endl;
	sstream << "STATUS                    = " << STR_CHMPXSTS_FULL(chmpxsvr.status)		<< endl;

	return true;
}

PCHMINFOEX ChmIMData::DupAllChmInfo(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return NULL;
	}
	PCHMINFOEX	pinfoex;
	if(NULL == (pinfoex = reinterpret_cast<PCHMINFOEX>(calloc(1, sizeof(CHMINFOEX))))){
		ERR_CHMPRN("Could not allocation memory.");
		return NULL;
	}
	pinfoex->shmsize			= ShmSize;
	strcpy(pinfoex->shmpath,	ShmPath.c_str());
	strcpy(pinfoex->k2hashpath,	pK2hash->GetK2hashFilePath());

	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	pinfoex->pchminfo			= tmpchminfo.Dup();

	return pinfoex;
}

PCHMPX ChmIMData::DupSelfChmpxInfo(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return NULL;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.DupSelfChmpxSvr();
}

void ChmIMData::FreeDupAllChmInfo(PCHMINFOEX ptr)
{
	if(ptr){
		chminfolap	tmpchminfo;			// [NOTE] pointer is not on Shm, but we use tmpchminfo object. thus using only Free method.
		tmpchminfo.Free(ptr->pchminfo);
		K2H_Free(ptr);
	}
}

void ChmIMData::FreeDupSelfChmpxInfo(PCHMPX ptr)
{
	if(ptr){
		chmpxlap	tmpchmpx;			// [NOTE] pointer is not on Shm, but we use tmpchminfo object. thus using only Free method.
		tmpchmpx.Free(ptr);
		K2H_Free(ptr);
	}
}

//---------------------------------------------------------
// Methods for K2hash
//---------------------------------------------------------
bool ChmIMData::CloseK2hash(void)
{
	if(!IsAttachedK2hash()){
		MSG_CHMPRN("There is already no attached K2hash file.");
		return false;
	}
	string	tmppath = pK2hash->GetK2hashFilePath();
	if(!pK2hash->Detach()){
		ERR_CHMPRN("Failed to detach k2hash file %s", tmppath.c_str());
		return false;
	}
	bool	result = true;
	if(isChmpxProc){
		if(-1 == unlink(tmppath.c_str())){
			ERR_CHMPRN("Failed to unlink file %s, errno=%d", tmppath.c_str(), errno);
			result = false;
		}
	}
	CHM_Delete(pK2hash);

	return result;
}

bool ChmIMData::InitializeK2hash(void)
{
	if(IsAttachedK2hash()){
		ERR_CHMPRN("Already attach K2hash, must dettach it before initializing K2hash.");
		return false;
	}
	if(!pConfObj){
		ERR_CHMPRN("Configration object is not loaded.");
		return false;
	}

	const CHMCFGINFO*	pchmcfg = pConfObj->GetConfiguration();
	if(!pchmcfg){
		ERR_CHMPRN("Could not get configuration information structure pointer.");
		return false;
	}

	string	k2hfilepath;
	if(!ChmIMData::MakeK2hashFilePath(pchmcfg->groupname.c_str(), pchmcfg->self_ctlport, k2hfilepath)){
		ERR_CHMPRN("Failed to make k2hash file path from groupname(%s).", pchmcfg->groupname.c_str());
		return false;
	}
	if(is_file_exist(k2hfilepath.c_str())){
		// remove old file
		if(-1 == unlink(k2hfilepath.c_str())){
			ERR_CHMPRN("Failed to unlink old k2hash file %s, errno=%d", k2hfilepath.c_str(), errno);
			return false;
		}
	}

	// init
	pK2hash = new K2HShm();
	if(!pK2hash->Create(k2hfilepath.c_str(), pchmcfg->k2h_fullmap, pchmcfg->k2h_mask_bitcnt, pchmcfg->k2h_cmask_bitcnt, pchmcfg->k2h_max_element, ChmIMData::SYSPAGE_SIZE)){
		ERR_CHMPRN("Failed to create new k2hash file(%s)", k2hfilepath.c_str());
		CHM_Delete(pK2hash);
		return false;
	}

	return true;
}

//
// For Client process library
//
bool ChmIMData::AttachK2hash(void)
{
	if(IsAttachedK2hash()){
		ERR_CHMPRN("Already attach K2hash, must dettach it before initializing K2hash.");
		return false;
	}
	if(!pConfObj){
		ERR_CHMPRN("Configration object is not loaded.");
		return false;
	}

	const CHMCFGINFO*	pchmcfg = pConfObj->GetConfiguration();
	if(!pchmcfg){
		ERR_CHMPRN("Could not get configuration information structure pointer.");
		return false;
	}

	string	k2hfilepath;
	if(!ChmIMData::MakeK2hashFilePath(pchmcfg->groupname.c_str(), pchmcfg->self_ctlport, k2hfilepath)){
		ERR_CHMPRN("Failed to make k2hash file path from groupname(%s).", pchmcfg->groupname.c_str());
		return false;
	}

	if(!is_file_exist(k2hfilepath.c_str())){
		ERR_CHMPRN("K2hash file(%s) does not exist.", k2hfilepath.c_str());
		return false;
	}

	// init
	pK2hash = new K2HShm();
	if(!pK2hash->Attach(k2hfilepath.c_str(), false, false, false, pchmcfg->k2h_fullmap, pchmcfg->k2h_mask_bitcnt, pchmcfg->k2h_cmask_bitcnt, pchmcfg->k2h_max_element, ChmIMData::SYSPAGE_SIZE)){
		ERR_CHMPRN("Failed to attach k2hash file(%s)", k2hfilepath.c_str());
		CHM_Delete(pK2hash);
		return false;
	}

	return true;
}

//---------------------------------------------------------
// Methods for ChmShm
//---------------------------------------------------------
bool ChmIMData::Initialize(CHMConf* cfgobj, int eventqfd, bool is_chmpx_proc)
{
	if(!cfgobj){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(CHM_INVALID_HANDLE == eventqfd){
		WAN_CHMPRN("Event fd is invalid.");
	}

	if(is_chmpx_proc != isChmpxProc){
		MSG_CHMPRN("Change \"is in CHMPX process\" flag to %s.", is_chmpx_proc ? "true" : "false");
		isChmpxProc = is_chmpx_proc;
	}

	// Set conf object.
	pConfObj = cfgobj;

	// SHM & K2hash
	if(isChmpxProc){
		// Initialize SHM
		if(!InitializeShm()){
			ERR_CHMPRN("Failed to initialize SHM.");
			pConfObj = NULL;
			return false;
		}

		// Initialize K2hash
		if(!InitializeK2hash()){
			ERR_CHMPRN("Failed to initialize k2hash file.");
			pConfObj = NULL;
			return false;
		}

		// Lock chmpx pid offset
		if(!LockChmpxPid()){
			ERR_CHMPRN("Failed to lock chmpx pid offset.");
			pConfObj = NULL;
			return false;
		}
	}else{
		// Attach SHM
		if(!AttachShm()){
			ERR_CHMPRN("Failed to initialize SHM.");
			pConfObj = NULL;
			return false;
		}

		// Check chmpx process running
		if(!IsChmpxProcessRunning(ChmpxPid)){
			ERR_CHMPRN("Chmpx process is not running.");
			pConfObj = NULL;
			return false;
		}

		// Attach K2hash
		if(!AttachK2hash()){
			ERR_CHMPRN("Failed to initialize k2hash file.");
			pConfObj = NULL;
			return false;
		}
	}

	// Initialize Other
	if(!InitializeOther()){
		ERR_CHMPRN("Failed to initialize other.");
		pConfObj = NULL;
		return false;
	}

	// backup
	eqfd = eventqfd;

	return true;
}

bool ChmIMData::CloseShm(void)
{
	if(!IsAttachedShm()){
		MSG_CHMPRN("There is already no CHMSHM.");
		return false;
	}

	bool	result = true;
	if(isChmpxProc){
		if(-1 == unlink(ShmPath.c_str())){
			ERR_CHMPRN("Failed to unlink file %s, errno=%d", ShmPath.c_str(), errno);
			result = false;
		}

		// Unlock chmpx pid offset
		if(!UnlockChmpxPid()){
			ERR_CHMPRN("Failed to unlock chmpx pid offset, but continue...");
			result = false;
		}
	}
	CHM_MUMMAP(ShmFd, pChmShm, ShmSize);
	ChmpxPid = CHM_INVALID_PID;

	return result;
}

bool ChmIMData::InitializeShm(void)
{
	if(IsAttachedShm()){
		ERR_CHMPRN("Already attach SHM, must dettach it before initializing SHM.");
		return false;
	}
	if(!pConfObj){
		ERR_CHMPRN("Configration object is not loaded.");
		return false;
	}

	const CHMCFGINFO*	pchmcfg = pConfObj->GetConfiguration();
	if(!pchmcfg){
		ERR_CHMPRN("Could not get configuration information structure pointer.");
		return false;
	}

	// Check mq size
	//
	// If chmpx process is mq receiver for all mq sender(client processes), mq size should be max mq count.
	//
	long	maxmsg = pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt;
	if(!ChmEventMq::InitializeMaxMqSystemSize(maxmsg)){
		ERR_CHMPRN("Could not get mq size = %ld for chmpx process.", pchmcfg->max_client_mq_cnt);
		return false;
	}

	CHMNODE_CFGINFO	self;
	if(!pConfObj->GetSelfNodeInfo(self)){
		ERR_CHMPRN("Could not get self node information.");
		return false;
	}

	return InitializeShmEx(pchmcfg, &self);
}

bool ChmIMData::InitializeShmEx(const CHMCFGINFO* pchmcfg, const CHMNODE_CFGINFO* pself)
{
	if(	!pchmcfg || MAX_GROUP_LENGTH <= pchmcfg->groupname.length() || MAX_CHMPX_COUNT < pchmcfg->max_chmpx_count || 
		MAX_SERVER_MQ_CNT < pchmcfg->max_server_mq_cnt || MAX_CLIENT_MQ_CNT < pchmcfg->max_client_mq_cnt || 
		MAX_MQ_PER_CLIENT < pchmcfg->max_mq_per_client || MAX_HISTLOG_COUNT < pchmcfg->max_histlog_count )
	{
		ERR_CHMPRN("Configuration information are wrong.");
		return false;
	}
	string	shmpath;
	if(!ChmIMData::MakeShmFilePath(pchmcfg->groupname.c_str(), pchmcfg->self_ctlport, shmpath)){
		ERR_CHMPRN("Failed to make chmshm file path from groupname(%s).", pchmcfg->groupname.c_str());
		return false;
	}

	// check existed file
	if(is_file_exist(shmpath.c_str())){
		// found old file, move it.
		if(!move_file_to_backup(shmpath.c_str(), CHMSHM_BACKUP_EXT)){
			ERR_CHMPRN("Failed to move(backup) file %s.", shmpath.c_str());
			return false;
		}
	}

	// make new file
	int	fd;
	if(CHM_INVALID_HANDLE == (fd = open(shmpath.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH))){
		ERR_CHMPRN("Could not create file(%s), errno = %d", shmpath.c_str(), errno);
		return false;
	}

	// file total size
	size_t	total_shmsize =	sizeof(CHMSHM) + 																						// CHMSHM (this structure is alignmented)
							sizeof(CHMPXLIST) * pchmcfg->max_chmpx_count + 															// CHMPX Area
							sizeof(PCHMPX) * pchmcfg->max_chmpx_count * 2 +															// PCHMPX array Area
							sizeof(MQMSGHEADLIST) * (pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt) + 					// MQUEUE Area
							sizeof(CLTPROCLIST) * MAX_CLTPROCLIST_COUNT(pchmcfg->max_client_mq_cnt, pchmcfg->mqcnt_per_attach) +	// CLTPROCLIST Area
							sizeof(CHMLOGRAW) * pchmcfg->max_histlog_count +														// LOG Area
							sizeof(CHMSOCKLIST) * pchmcfg->max_chmpx_count * pchmcfg->max_sock_pool * 2;							// SOCK array Area([NOTICE] twice for mergin)

	// truncate with filling zero
	if(!truncate_filling_zero(fd, total_shmsize, ChmIMData::SYSPAGE_SIZE)){
		ERR_CHMPRN("Could not truncate file(%s) with filling zero.", shmpath.c_str());
		CHM_CLOSE(fd);
		return false;
	}

	// mmap
	void*	shmbase;
	if(MAP_FAILED == (shmbase = mmap(NULL, total_shmsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))){
		ERR_CHMPRN("Could not mmap file(%s), errno = %d", shmpath.c_str(), errno);
		CHM_CLOSE(fd);
		return false;
	}

	// initialize data in shm
	PCHMSHM	pChmBase = reinterpret_cast<PCHMSHM>(shmbase);
	PCHMPX*	rel_pchmpxarr_base;
	PCHMPX*	rel_pchmpxarr_pend;
	{
		PCHMPXLIST		rel_chmpxarea			= reinterpret_cast<PCHMPXLIST>(		sizeof(CHMSHM));
		PCHMPX*			rel_pchmpxarrarea		= reinterpret_cast<PCHMPX*>(		sizeof(CHMSHM) +
																					sizeof(CHMPXLIST) * pchmcfg->max_chmpx_count);
		PMQMSGHEADLIST	rel_chmpxmsgarea		= reinterpret_cast<PMQMSGHEADLIST>(	sizeof(CHMSHM) +
																					sizeof(CHMPXLIST) * pchmcfg->max_chmpx_count +
																					sizeof(PCHMPX) * pchmcfg->max_chmpx_count * 2);
		PCLTPROCLIST	rel_chmpxpidarea		= reinterpret_cast<PCLTPROCLIST>(	sizeof(CHMSHM) +
																					sizeof(CHMPXLIST) * pchmcfg->max_chmpx_count +
																					sizeof(PCHMPX) * pchmcfg->max_chmpx_count * 2 +
																					sizeof(MQMSGHEADLIST) * (pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt));
		PCHMLOGRAW		rel_lograwarea			= reinterpret_cast<PCHMLOGRAW>(		sizeof(CHMSHM) +
																					sizeof(CHMPXLIST) * pchmcfg->max_chmpx_count +
																					sizeof(PCHMPX) * pchmcfg->max_chmpx_count * 2 +
																					sizeof(MQMSGHEADLIST) * (pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt) +
																					sizeof(CLTPROCLIST) * MAX_CLTPROCLIST_COUNT(pchmcfg->max_client_mq_cnt, pchmcfg->mqcnt_per_attach));
		PCHMSOCKLIST	rel_chmsockarea			= reinterpret_cast<PCHMSOCKLIST>(	sizeof(CHMSHM) +
																					sizeof(CHMPXLIST) * pchmcfg->max_chmpx_count +
																					sizeof(PCHMPX) * pchmcfg->max_chmpx_count * 2 +
																					sizeof(MQMSGHEADLIST) * (pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt) +
																					sizeof(CLTPROCLIST) * MAX_CLTPROCLIST_COUNT(pchmcfg->max_client_mq_cnt, pchmcfg->mqcnt_per_attach) +
																					sizeof(CHMLOGRAW) * pchmcfg->max_histlog_count);
		rel_pchmpxarr_base						= rel_pchmpxarrarea;
		rel_pchmpxarr_pend						= CHM_OFFSET(rel_pchmpxarrarea, static_cast<off_t>(sizeof(PCHMPX) * pchmcfg->max_chmpx_count), PCHMPX*);

		// initializing each erea
		{
			PCHMPXLIST	chmpxlist = CHM_ABS(shmbase, rel_chmpxarea, PCHMPXLIST);
			for(long cnt = 0L; cnt < pchmcfg->max_chmpx_count; cnt++){
				PCHMPXLIST	prev = 0 < cnt ? &chmpxlist[cnt - 1] : NULL;
				PCHMPXLIST	next = (cnt + 1 < pchmcfg->max_chmpx_count) ? &chmpxlist[cnt + 1] : NULL;

				chmpxlistlap	tmpchmpxlist(&chmpxlist[cnt], NULL, NULL, NULL, NULL, NULL, shmbase);		// absmapptr, chmpx*s are NULL, these can allow only here(calling only Initialize()).
				if(!tmpchmpxlist.Initialize(prev, next)){
					ERR_CHMPRN("Failed to initialize No.%ld CHMPXLIST.", cnt);
					CHM_MUMMAP(fd, shmbase, total_shmsize);
					return false;
				}
			}
		}
		{
			PCHMPX*	pchmpxarr = CHM_ABS(shmbase, rel_pchmpxarrarea, PCHMPX*);
			for(long cnt = 0L; cnt < (pchmcfg->max_chmpx_count * 2); cnt++){
				pchmpxarr[cnt] = NULL;
			}
		}
		{
			PMQMSGHEADLIST	mqmsglist = CHM_ABS(shmbase, rel_chmpxmsgarea, PMQMSGHEADLIST);
			for(long cnt = 0L; cnt < (pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt); cnt++){
				PMQMSGHEADLIST	prev = (0 < cnt) ? &mqmsglist[cnt - 1] : NULL;
				PMQMSGHEADLIST	next = (cnt + 1 < (pchmcfg->max_server_mq_cnt + pchmcfg->max_client_mq_cnt)) ? &mqmsglist[cnt + 1] : NULL;

				mqmsgheadlistlap	tmpmqmsgheadlist(&mqmsglist[cnt], shmbase);
				if(!tmpmqmsgheadlist.Initialize(prev, next)){
					ERR_CHMPRN("Failed to initialize No.%ld MQMSGHEADLIST.", cnt);
					CHM_MUMMAP(fd, shmbase, total_shmsize);
					return false;
				}
			}
		}
		{
			PCLTPROCLIST	cltproclist		= CHM_ABS(shmbase, rel_chmpxpidarea, PCLTPROCLIST);
			long			cltproclist_cnt	= MAX_CLTPROCLIST_COUNT(pchmcfg->max_client_mq_cnt, pchmcfg->mqcnt_per_attach);
			for(long cnt = 0L; cnt < cltproclist_cnt; cnt++){
				PCLTPROCLIST	prev = (0 < cnt) ? &cltproclist[cnt - 1] : NULL;
				PCLTPROCLIST	next = (cnt + 1 < cltproclist_cnt) ? &cltproclist[cnt + 1] : NULL;

				cltproclistlap	tmpcltproclist(&cltproclist[cnt], shmbase);
				if(!tmpcltproclist.Initialize(prev, next)){
					ERR_CHMPRN("Failed to initialize No.%ld PCLTPROCLIST.", cnt);
					CHM_MUMMAP(fd, shmbase, total_shmsize);
					return false;
				}
			}
		}
		{
			PCHMLOGRAW	lograw = CHM_ABS(shmbase, rel_lograwarea, PCHMLOGRAW);
			for(long cnt = 0L; cnt < pchmcfg->max_histlog_count; cnt++){
				chmlograwlap	tmplograw(&lograw[cnt], shmbase);
				if(!tmplograw.Initialize()){
					ERR_CHMPRN("Failed to initialize No.%ld CHMLOGRAW.", cnt);
					CHM_MUMMAP(fd, shmbase, total_shmsize);
					return false;
				}
			}
		}
		{
			PCHMSOCKLIST	chmsocklist		= CHM_ABS(shmbase, rel_chmsockarea, PCHMSOCKLIST);
			long			chmsocklist_cnt	= pchmcfg->max_chmpx_count * pchmcfg->max_sock_pool * 2;
			for(long cnt = 0L; cnt < chmsocklist_cnt; cnt++){
				PCHMSOCKLIST	prev = (0 < cnt) ? &chmsocklist[cnt - 1] : NULL;
				PCHMSOCKLIST	next = (cnt + 1 < chmsocklist_cnt) ? &chmsocklist[cnt + 1] : NULL;

				chmsocklistlap	tmpchmsocklist(&chmsocklist[cnt], shmbase);
				if(!tmpchmsocklist.Initialize(prev, next)){
					ERR_CHMPRN("Failed to initialize No.%ld PCHMSOCKLIST.", cnt);
					CHM_MUMMAP(fd, shmbase, total_shmsize);
					return false;
				}
			}
		}

		// CHMSHM
		pChmBase->rel_chmpxarea		= rel_chmpxarea;
		pChmBase->rel_pchmpxarrarea	= rel_pchmpxarrarea;
		pChmBase->rel_chmpxmsgarea	= rel_chmpxmsgarea;
		pChmBase->rel_chmpxpidarea	= rel_chmpxpidarea;
		pChmBase->rel_chmsockarea	= rel_chmsockarea;

		// CHMSHM.CHMLOG
		chmloglap	tmpchmlog(&pChmBase->chmpxlog, shmbase);
		if(!tmpchmlog.Initialize(rel_lograwarea, pchmcfg->max_histlog_count)){
			ERR_CHMPRN("Failed to initialize CHMLOG.");
			CHM_MUMMAP(fd, shmbase, total_shmsize);
			return false;
		}

		// CHMSHM.CHMINFO
		chminfolap	tmpchminfo(&pChmBase->info, shmbase);
		if(!tmpchminfo.Initialize(pchmcfg, rel_chmpxmsgarea, pself, rel_chmpxarea, rel_chmpxpidarea, rel_chmsockarea, rel_pchmpxarr_base, rel_pchmpxarr_pend)){
			ERR_CHMPRN("Failed to initialize CHMINFO.");
			CHM_MUMMAP(fd, shmbase, total_shmsize);
			return false;
		}
	}
	ShmPath	= shmpath;
	pChmShm	= pChmBase;
	ShmFd	= fd;
	ShmSize	= total_shmsize;

	return true;
}

//
// For Client process library
//
bool ChmIMData::AttachShm(void)
{
	if(IsAttachedShm()){
		ERR_CHMPRN("Already attach SHM, must dettach it before initializing SHM.");
		return false;
	}
	if(!pConfObj){
		ERR_CHMPRN("Configration object is not loaded.");
		return false;
	}

	// get config
	const CHMCFGINFO*	pchmcfg = pConfObj->GetConfiguration();
	if(!pchmcfg){
		ERR_CHMPRN("Could not get configuration information structure pointer.");
		return false;
	}
	if(MAX_GROUP_LENGTH <= pchmcfg->groupname.length()){
		ERR_CHMPRN("Configuration information are wrong.");
		return false;
	}

	// Check mq size
	//
	long	maxmsg = pchmcfg->max_mq_per_client + pchmcfg->max_server_mq_cnt;
	if(!ChmEventMq::InitializeMaxMqSystemSize(maxmsg)){
		ERR_CHMPRN("Could not set mq size = %ld for client process.", maxmsg);
		return false;
	}

	// make shm path
	string	shmpath;
	if(!ChmIMData::MakeShmFilePath(pchmcfg->groupname.c_str(), pchmcfg->self_ctlport, shmpath)){
		ERR_CHMPRN("Failed to make chmshm file path from groupname(%s).", pchmcfg->groupname.c_str());
		return false;
	}

	// shm file size
	size_t	total_shmsize;
	if(!get_file_size(shmpath.c_str(), total_shmsize)){
		ERR_CHMPRN("ChmShm file(%s) does not exist or failed to read file size.", shmpath.c_str());
		return false;
	}

	// open shm file
	int	fd;
	if(CHM_INVALID_HANDLE == (fd = open(shmpath.c_str(), O_RDWR))){
		ERR_CHMPRN("Could not open file(%s), errno = %d", shmpath.c_str(), errno);
		return false;
	}

	// mmap
	void*	shmbase;
	if(MAP_FAILED == (shmbase = mmap(NULL, total_shmsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))){
		ERR_CHMPRN("Could not mmap file(%s), errno = %d", shmpath.c_str(), errno);
		CHM_CLOSE(fd);
		return false;
	}

	// initialize data
	ShmPath	= shmpath;
	pChmShm = reinterpret_cast<PCHMSHM>(shmbase);
	ShmFd	= fd;
	ShmSize	= total_shmsize;

	return true;
}

//---------------------------------------------------------
// Methods for Initializing other
//---------------------------------------------------------
bool ChmIMData::InitializeOther(void)
{
	if(!pConfObj){
		ERR_CHMPRN("Configration object is not loaded.");
		return false;
	}
	const CHMCFGINFO*	pchmcfg = pConfObj->GetConfiguration();
	if(!pchmcfg){
		ERR_CHMPRN("Could not get configuration information structure pointer.");
		return false;
	}
	// clear hostanme:ctlport cache
	FREE_HNAMESSLMAP(hnamesslmap);

	return true;
}

bool ChmIMData::ReloadConfigration(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	if(!pConfObj){
		ERR_CHMPRN("Configration object is not loaded.");
		return false;
	}
	const CHMCFGINFO*	pchmcfg = pConfObj->GetConfiguration();
	if(!pchmcfg){
		ERR_CHMPRN("Could not get configuration information structure pointer.");
		return false;
	}

	// reload
	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	if(!tmpchminfo.ReloadConfigration(pchmcfg)){
		ERR_CHMPRN("Failed to reload configuration file.");
		return false;
	}
	return true;
}

//---------------------------------------------------------
// Methods for Accessing MQ
//---------------------------------------------------------
// For locking MQ, use free_msg_count member address in CHMSHM.
// This method is utility.
//
off_t ChmIMData::GetLockOffsetForMQ(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	long*	rel_freemsgcnt = CHM_REL(pChmShm, &(pChmShm->info.free_msg_count), long*);
	return reinterpret_cast<off_t>(rel_freemsgcnt);
}

msgid_t ChmIMData::GetBaseMsgId(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_READ, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetBaseMsgId();
}

bool ChmIMData::FreeMsg(msgid_t msgid)
{
	if(CHM_INVALID_MSGID == msgid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	if(!tmpchminfo.FreeMsg(msgid)){
		ERR_CHMPRN("Failed to retrive msgid(0x%016" PRIx64 ").", msgid);
		return false;
	}
	return true;
}

bool ChmIMData::IsMsgidActivated(msgid_t msgid) const
{
	if(CHM_INVALID_MSGID == msgid){
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);

	return tmpchminfo.IsMsgidActivated(msgid);
}

msgid_t ChmIMData::AssignMsg(bool is_chmpx, bool is_activated)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_MSGID;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.AssignMsg(is_chmpx, is_activated);
}

bool ChmIMData::ActivateMsgEx(msgid_t msgid, bool is_activate)
{
	if(CHM_INVALID_MSGID == msgid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	bool		result;
	if(is_activate){
		result = tmpchminfo.SetMqActivated(msgid);
	}else{
		result = tmpchminfo.SetMqDisactivated(msgid);
	}
	return result;
}

msgid_t ChmIMData::GetRandomMsgId(bool is_chmpx)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_MSGID;
	}
	ChmLock	AutoLock(CHMLT_READ, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetRandomMsgId(is_chmpx, true);					// Only activated msgid
}

bool ChmIMData::GetMsgidListByPid(pid_t pid, msgidlist_t& list)
{
	if(CHM_INVALID_PID == pid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMsgidListByPid(pid, list, true);
}

bool ChmIMData::GetMsgidListByUniqPid(msgidlist_t& list)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMsgidListByUniqPid(list, true);
}

bool ChmIMData::FreeMsgs(const pidlist_t& pidlist, msgidlist_t& freedmsgids)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_WRITE, ShmFd, GetLockOffsetForMQ());			// Lock

	freedmsgids.clear();

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);

	// get all msgids
	msgidlist_t	msgidlist;
	for(pidlist_t::const_iterator iter = pidlist.begin(); iter != pidlist.end(); ++iter){
		if(!tmpchminfo.GetMsgidListByPid(*iter, msgidlist, false)){
			ERR_CHMPRN("Something error occured during getting msgids by pid, but continue...");
		}
	}

	// Free msgids
	for(msgidlist_t::iterator iter = msgidlist.begin(); iter != msgidlist.end(); ++iter){
		if(!tmpchminfo.FreeMsg(*iter)){
			ERR_CHMPRN("Something error occured during free msgid(0x%016" PRIx64 "), but continue...", *iter);
			continue;
		}
		freedmsgids.push_back(*iter);
	}
	return true;
}

//---------------------------------------------------------
// Methods for Accessing CHMSHM
//---------------------------------------------------------
bool ChmIMData::GetSelfChmpxSvr(PCHMPXSVR chmpxsvr) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfChmpxSvr(chmpxsvr);
}

bool ChmIMData::GetChmpxSvr(chmpxid_t chmpxid, PCHMPXSVR chmpxsvr) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetChmpxSvr(chmpxid, chmpxsvr);
}

bool ChmIMData::GetChmpxSvrs(PCHMPXSVR* ppchmpxsvrs, long& count) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetChmpxSvrs(ppchmpxsvrs, count);
}

bool ChmIMData::CompareChmpxSvrs(PCHMPXSVR pchmpxsvrs, long count)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	PCHMPXSVR	pbasechmpxsvrs	= NULL;
	long		basecount		= 0L;
	if(!tmpchminfo.GetChmpxSvrs(&pbasechmpxsvrs, basecount)){
		ERR_CHMPRN("Could not get now chmpx servers information.");
		return false;
	}

	// check
	if(!ChmIMData::CompareChmpxSvrs(pbasechmpxsvrs, basecount, pchmpxsvrs, count)){
		CHM_Free(pbasechmpxsvrs);
		return false;
	}
	CHM_Free(pbasechmpxsvrs);
	return true;
}

bool ChmIMData::MergeChmpxSvrs(PCHMPXSVR pchmpxsvrs, long count, bool is_remove, bool is_init_process, int eqfd)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.MergeChmpxSvrs(pchmpxsvrs, count, is_remove, is_init_process, eqfd);
}

bool ChmIMData::MergeChmpxSvrsForStatusUpdate(PCHMPXSVR pchmpxsvrs, long count, int eqfd)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	PCHMPXSVR	pbasechmpxsvrs	= NULL;
	long		basecount		= 0L;
	if(!tmpchminfo.GetChmpxSvrs(&pbasechmpxsvrs, basecount)){
		ERR_CHMPRN("Could not get now chmpx servers information.");
		return false;
	}

	// check
	if(!ChmIMData::CompareChmpxSvrs(pbasechmpxsvrs, basecount, pchmpxsvrs, count, false)){	// without checking status
		ERR_CHMPRN("For merging, could not merge from new chmpx servers information to now one, there are many difference.");
		CHM_Free(pbasechmpxsvrs);
		return false;
	}

	// merge
	//
	// Lock in following methos.
	//
	if(!MergeChmpxSvrs(pchmpxsvrs, count, false, false, eqfd)){
		ERR_CHMPRN("Failed to merge chmpx server information, try to recover...");
		if(!MergeChmpxSvrs(pbasechmpxsvrs, basecount, false, false, eqfd)){
			ERR_CHMPRN("Failed to recover merging chmpx server information, no more do nothing...");
		}
		CHM_Free(pbasechmpxsvrs);
		return false;
	}
	CHM_Free(pbasechmpxsvrs);

	return true;
}

bool ChmIMData::GetGroup(string& group) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetGroup(group);
}

bool ChmIMData::IsRandomDeliver(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsRandomDeliver();
}

bool ChmIMData::IsAutoMergeConf(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsAutoMergeConf();
}

bool ChmIMData::IsDoMergeConf(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsDoMergeConf();
}

int ChmIMData::GetSocketThreadCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSocketThreadCount();
}

int ChmIMData::GetMQThreadCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMQThreadCount();
}

int ChmIMData::GetMaxSockPool(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMaxSockPool();
}

time_t ChmIMData::GetSockPoolTimeout(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSockPoolTimeout();
}

long ChmIMData::GetMaxMQCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMaxMQCount();
}

long ChmIMData::GetChmpxMQCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetChmpxMQCount();
}

long ChmIMData::GetMaxQueuePerChmpxMQ(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMaxQueuePerChmpxMQ();
}

long ChmIMData::GetMaxQueuePerClientMQ(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMaxQueuePerClientMQ();
}

long ChmIMData::GetMQPerClient(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMQPerClient();
}

long ChmIMData::GetMQPerAttach(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMQPerAttach();
}

bool ChmIMData::IsAckMQ(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsAckMQ();
}

int ChmIMData::GetSockRetryCnt(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSockRetryCnt();
}

suseconds_t ChmIMData::GetSockTimeout(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSockTimeout();
}

suseconds_t ChmIMData::GetConnectTimeout(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetConnectTimeout();
}

int ChmIMData::GetMQRetryCnt(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMQRetryCnt();
}

long ChmIMData::GetMQTimeout(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMQTimeout();
}

time_t ChmIMData::GetMergeTimeout(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMergeTimeout();
}

bool ChmIMData::IsServerMode(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsServerMode();
}

bool ChmIMData::IsSlaveMode(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsSlaveMode();
}

long ChmIMData::GetReplicaCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return DEFAULT_REPLICA_COUNT;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetReplicaCount();
}

long ChmIMData::GetMaxHistoryLogCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	// This value is set only at initializing, so not need locking
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMaxHistoryLogCount();
}

chmpxid_t ChmIMData::GetSelfChmpxId(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfChmpxId();
}

long ChmIMData::GetServerCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerCount();
}

long ChmIMData::GetSlaveCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSlaveCount();
}

long ChmIMData::GetUpServerCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetUpServerCount();
}

chmpxpos_t ChmIMData::GetSelfServerPos(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXLISTPOS;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfServerPos();
}

chmpxpos_t ChmIMData::GetNextServerPos(chmpxpos_t startpos, chmpxpos_t nowpos, bool is_skip_self, bool is_cycle) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXLISTPOS;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetNextServerPos(startpos, nowpos, is_skip_self, is_cycle);
}

bool ChmIMData::GetNextServerBase(string& name, chmpxid_t& chmpxid, short& port, short& ctlport) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	chmpxpos_t	nextpos;
	if(CHM_INVALID_CHMPXLISTPOS == (nextpos = tmpchminfo.GetNextServerPos(GetSelfServerPos(), GetSelfServerPos(), false, true))){
		ERR_CHMPRN("Could not get next chmpx pos in server list.");
		return false;
	}
	return tmpchminfo.GetServerBase(nextpos, name, chmpxid, port, ctlport);
}

chmpxid_t ChmIMData::GetNextServerChmpxId(void) const
{
	string		name;
	chmpxid_t	chmpxid = CHM_INVALID_CHMPXID;
	short		port;
	short		ctlport;
	if(!GetNextServerBase(name, chmpxid, port, ctlport)){
		return CHM_INVALID_CHMPXID;
	}
	return chmpxid;
}

chmpxid_t ChmIMData::GetNextRingChmpxId(chmpxid_t chmpxid) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetNextRingChmpxId(chmpxid);
}

bool ChmIMData::IsServerChmpxId(chmpxid_t chmpxid) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsServerChmpxId(chmpxid);
}

chmpxid_t ChmIMData::GetChmpxIdBySock(int sock, int type) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetChmpxIdBySock(sock, type);
}

chmpxid_t ChmIMData::GetChmpxIdByToServerName(const char* hostname, short ctlport) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetChmpxIdByToServerName(hostname, ctlport);
}

chmpxid_t ChmIMData::GetChmpxIdByStatus(chmpxsts_t status, bool part_match) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetChmpxIdByStatus(status, part_match);
}

chmpxid_t ChmIMData::GetRandomServerChmpxId(bool is_up_servers, bool without_suspend)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetRandomServerChmpxId(is_up_servers, without_suspend);
}

chmpxid_t ChmIMData::GetServerChmpxIdByHash(chmhash_t hash) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHM_INVALID_CHMPXID;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerChmpxIdByHash(hash);
}

bool ChmIMData::GetServerChmHashsByHashs(chmhash_t hash, chmhashlist_t& basehashs, bool with_pending, bool without_down, bool without_suspend)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerChmHashsByHashs(hash, basehashs, with_pending, without_down, without_suspend);
}

bool ChmIMData::GetServerChmpxIdByHashs(chmhash_t hash, chmpxidlist_t& chmpxids, bool with_pending, bool without_down, bool without_suspend)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerChmpxIdByHashs(hash, chmpxids, with_pending, without_down, without_suspend);
}

//
// [NOTE]
// This method returns special chmpxid list for merging.
// The server which is assigned main base hash has replicated another server's base hash.
// And another server has this base hash too.
// So this method returns all server chmpxid list which are related to this base hash's server.
// 
long ChmIMData::GetServerChmpxIdByBaseHash(chmhash_t basehash, chmpxidlist_t& chmpxids) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap		tmpchminfo(&pChmShm->info, pChmShm);
	long			replcnt		= tmpchminfo.GetReplicaCount();
	chmhash_t		max_base	= 0;					// not use
	chmhash_t		max_pending	= 0;
	tmpchminfo.GetMaxHashCount(max_base, max_pending);

	chmhash_t		another_basehash;
	if(static_cast<chmhash_t>(replcnt) <= basehash){
		another_basehash = basehash - static_cast<chmhash_t>(replcnt);
	}else{
		another_basehash = basehash + max_base - static_cast<chmhash_t>(replcnt);
	}

	// get two type chmpxid list(without pending, with down server, with suspend server)
	chmpxidlist_t	another_chmpxids;
	tmpchminfo.GetServerChmpxIdByHashs(basehash, chmpxids, false, true, true);
	tmpchminfo.GetServerChmpxIdByHashs(another_basehash, another_chmpxids, false, true, true);

	// merge two list to result list.
	bool	found;
	for(chmpxidlist_t::const_iterator iter1 = another_chmpxids.begin(); iter1 != another_chmpxids.end(); ++iter1){
		found = false;
		for(chmpxidlist_t::const_iterator iter2 = chmpxids.begin(); iter2 != chmpxids.end(); ++iter2){
			if((*iter1) == (*iter2)){
				found = true;
				break;
			}
		}
		if(!found){
			chmpxids.push_back(*iter1);
		}
	}
	return chmpxids.size();
}

long ChmIMData::GetServerChmpxIdForMerge(chmpxidlist_t& list) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerChmpxIds(list, false, true, true);						// without pending, without down server, without suspend server
}

long ChmIMData::GetServerChmpxIds(chmpxidlist_t& list) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerChmpxIds(list, true, true, true);						// with pending, without down server, without suspend server
}

bool ChmIMData::GetServerBase(long pos, string& name, chmpxid_t& chmpxid, short& port, short& ctlport) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerBase(pos, name, chmpxid, port, ctlport);
}

bool ChmIMData::GetServerBase(chmpxid_t chmpxid, std::string& name, short& port, short& ctlport) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerBase(chmpxid, name, port, ctlport);
}

bool ChmIMData::GetServerBase(chmpxid_t chmpxid, CHMPXSSL& ssl) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerBase(chmpxid, ssl);
}

bool ChmIMData::GetServerSocks(chmpxid_t chmpxid, socklist_t& socklist, int& ctlsock) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerSocks(chmpxid, socklist, ctlsock);
}

bool ChmIMData::GetServerHash(chmpxid_t chmpxid, chmhash_t& base, chmhash_t& pending) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerHash(chmpxid, base, pending);
}

bool ChmIMData::GetMaxHashCount(chmhash_t& basehash, chmhash_t& pendinghash) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetMaxHashCount(basehash, pendinghash);
}

bool ChmIMData::IsPendingExchangeData(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chmhash_t	basehash		= CHM_INVALID_HASHVAL;
	chmhash_t	pendinghash		= CHM_INVALID_HASHVAL;
	chmhash_t	max_basehash	= CHM_INVALID_HASHVAL;
	chmhash_t	max_pendinghash	= CHM_INVALID_HASHVAL;

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	tmpchminfo.GetMaxHashCount(max_basehash, max_pendinghash);
	tmpchminfo.GetSelfHash(basehash, pendinghash);

	return (basehash != pendinghash || max_basehash != max_pendinghash);
}

long ChmIMData::GetReceiverChmpxids(chmhash_t hash, c2ctype_t c2ctype, chmpxidlist_t& chmpxids)
{
	chmpxid_t	selfchmpxid = GetSelfChmpxId();

	if(IS_C2CTYPE_ROUTING(c2ctype)){
		if(IsRandomDeliver()){
			// Random --> terminal chmpxid is one
			chmpxid_t	tmpchmpxid;
			if(CHM_INVALID_CHMPXID == (tmpchmpxid = GetRandomServerChmpxId(true, true))){	// only up server, not suspend server
				ERR_CHMPRN("Could not get random server chmpxid.");
				return -1L;
			}
			chmpxids.push_back(tmpchmpxid);
		}else{
			// from hash --> terminal chmpxid is some
			chmpxidlist_t	tmpchmpxids;
			if(!GetServerChmpxIdByHashs(hash, tmpchmpxids)){
				ERR_CHMPRN("Could not get chmpxid by hash(0x%016" PRIx64 ").", hash);
				return -1L;
			}

			// set first chmpxid
			chmpxid_t	tmpchmpxid = CHM_INVALID_CHMPXID;
			do{
				tmpchmpxid = tmpchmpxids.front();
				// except self chmpxid
				if(IS_C2CTYPE_NOT_SELF(c2ctype)){
					if(selfchmpxid == tmpchmpxid){
						tmpchmpxid = CHM_INVALID_CHMPXID;
					}
				}
			}while(tmpchmpxid == CHM_INVALID_CHMPXID && 0 < tmpchmpxids.size());

			if(CHM_INVALID_CHMPXID == tmpchmpxid){
				ERR_CHMPRN("Could not get chmpxid by hash(0x%016" PRIx64 ") because target chmpxid is probabry down or suspend.", hash);
				return -1L;
			}

			// one chmpxid is set
			chmpxids.push_back(tmpchmpxid);
		}

	}else if(IS_C2CTYPE_BROADCAST(c2ctype)){
		// any type --> terminal chmpxid is all
		if(!GetServerChmpxIds(chmpxids)){
			ERR_CHMPRN("Could not get all server chmpxids.");
			return -1L;
		}
		// except self chmpxid
		if(IS_C2CTYPE_NOT_SELF(c2ctype)){
			for(chmpxidlist_t::iterator iter = chmpxids.begin(); iter != chmpxids.end(); ){
				if(selfchmpxid == *iter){
					iter = chmpxids.erase(iter);
					continue;
				}
				++iter;
			}
		}

	}else if(IS_C2CTYPE_RBROADCAST(c2ctype)){
		// any type --> terminal chmpxid is all routing chmpxids for hash value

		// get target chmpxids
		if(!GetServerChmpxIdByHashs(hash, chmpxids, true, true, true)){					// with pending, without down and suspend
			ERR_CHMPRN("Could not get chmpxid by hash(0x%016" PRIx64 ") because all target chmpxid is probabry down or suspend.", hash);
			return -1L;
		}
		// except self chmpxid
		if(IS_C2CTYPE_NOT_SELF(c2ctype)){
			for(chmpxidlist_t::iterator iter = chmpxids.begin(); iter != chmpxids.end(); ){
				if(selfchmpxid == *iter){
					iter = chmpxids.erase(iter);
					continue;
				}
				++iter;
			}
		}

	}else{	// COM_C2C_NORMAL or COM_C2C_IGNORE
		if(!IS_C2CTYPE_NORMAL(c2ctype)){
			WAN_CHMPRN("COM_C2C type(%s) should be COM_C2C_NORMAL, so continue as COM_C2C_NORMAL.", STRCOMC2CTYPE(c2ctype));
		}

		chmpxid_t	tmpchmpxid = CHM_INVALID_CHMPXID;
		if(IsRandomDeliver()){
			// Random --> terminal chmpxid is one
			if(CHM_INVALID_CHMPXID == (tmpchmpxid = GetRandomServerChmpxId(true, true))){	// only up server, not suspend server
				ERR_CHMPRN("Could not get random server chmpxid.");
				return -1L;
			}
		}else{
			// from hash --> terminal chmpxid is one
			if(CHM_INVALID_CHMPXID == (tmpchmpxid = GetServerChmpxIdByHash(hash))){
				// could not get main target hash chmpx(down or suspend),
				// so try to get another chmpx when hash & replication mode.
				//
				if(0 < GetReplicaCount()){
					chmpxidlist_t	tmpchmpxids;
					// find chmpxs assigned by target hash
					if(!GetServerChmpxIdByHashs(hash, tmpchmpxids, false) || 0 == tmpchmpxids.size()){		// without assigned pending hash
						if(!GetServerChmpxIdByHashs(hash, tmpchmpxids, true) || 0 == tmpchmpxids.size()){	// without assign pending hash
							ERR_CHMPRN("Could not get chmpxid by hash(0x%016" PRIx64 ") because target chmpxid is probabry down or suspend.", hash);
							return -1L;
						}
					}
					// set first chmpxid
					do{
						tmpchmpxid = tmpchmpxids.front();
						// except self chmpxid
						if(IS_C2CTYPE_NOT_SELF(c2ctype)){
							if(selfchmpxid == tmpchmpxid){
								tmpchmpxid = CHM_INVALID_CHMPXID;
							}
						}
					}while(tmpchmpxid == CHM_INVALID_CHMPXID && 0 < tmpchmpxids.size());

				}else{
					ERR_CHMPRN("Replica count is invalid, so could not get chmpxid by hash(0x%016" PRIx64 ").", hash);
					return -1L;
				}
			}
		}
		if(CHM_INVALID_CHMPXID == tmpchmpxid){
			ERR_CHMPRN("Could not get chmpxid by hash(0x%016" PRIx64 ") because target chmpxid is probabry down or suspend.", hash);
			return -1L;
		}
		// one chmpxid is set
		chmpxids.push_back(tmpchmpxid);
	}
	return static_cast<long>(chmpxids.size());
}

chmpxsts_t ChmIMData::GetServerStatus(chmpxid_t chmpxid) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHMPXSTS_SRVOUT_DOWN_NORMAL;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetServerStatus(chmpxid);
}

bool ChmIMData::GetSelfPorts(short& port, short& ctlport) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfPorts(port, ctlport);
}

bool ChmIMData::GetSelfSocks(int& sock, int& ctlsock) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfSocks(sock, ctlsock);
}

bool ChmIMData::GetSelfHash(chmhash_t& base, chmhash_t& pending) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfHash(base, pending);
}

chmpxsts_t ChmIMData::GetSelfStatus(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHMPXSTS_SLAVE_DOWN_NORMAL;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfStatus();
}

bool ChmIMData::GetSelfSsl(CHMPXSSL& ssl) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSelfSsl(ssl);
}

long ChmIMData::GetSlaveChmpxIds(chmpxidlist_t& list) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSlaveChmpxIds(list);
}

bool ChmIMData::GetSlaveBase(chmpxid_t chmpxid, std::string& name, short& ctlport) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSlaveBase(chmpxid, name, ctlport);
}

bool ChmIMData::GetSlaveSock(chmpxid_t chmpxid, socklist_t& socklist) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSlaveSock(chmpxid, socklist);
}

chmpxsts_t ChmIMData::GetSlaveStatus(chmpxid_t chmpxid) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return CHMPXSTS_SLAVE_DOWN_NORMAL;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetSlaveStatus(chmpxid);
}

bool ChmIMData::SetServerSocks(chmpxid_t chmpxid, int sock, int ctlsock, int type)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetServerSocks(chmpxid, sock, ctlsock, type);
}

bool ChmIMData::RemoveServerSock(chmpxid_t chmpxid, int sock)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.RemoveServerSock(chmpxid, sock);
}

bool ChmIMData::SetServerHash(chmpxid_t chmpxid, chmhash_t base, chmhash_t pending, int type)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetServerHash(chmpxid, base, pending, type);
}

bool ChmIMData::SetServerStatus(chmpxid_t chmpxid, chmpxsts_t status)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetServerStatus(chmpxid, status);
}

bool ChmIMData::IsOperating(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsOperating();
}

bool ChmIMData::UpdatePendingHash(bool is_allow_operating)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.UpdatePendingHash(is_allow_operating);
}

bool ChmIMData::SetSelfSocks(int sock, int ctlsock)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetSelfSocks(sock, ctlsock);
}

bool ChmIMData::SetSelfHash(chmhash_t base, chmhash_t pending, int type)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetSelfHash(base, pending, type);
}

bool ChmIMData::SetSelfStatus(chmpxsts_t status)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetSelfStatus(status);
}

bool ChmIMData::SetSlaveBase(chmpxid_t chmpxid, const char* hostname, short ctlport, const PCHMPXSSL pssl)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetSlaveBase(chmpxid, hostname, ctlport, pssl);
}

bool ChmIMData::SetSlaveSock(chmpxid_t chmpxid, int sock)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetSlaveSock(chmpxid, sock);
}

bool ChmIMData::SetSlaveStatus(chmpxid_t chmpxid, chmpxsts_t status)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.SetSlaveStatus(chmpxid, status);
}

// [NOTE]
// Must call this method instead of SetSlaveBase and SetSlaveSock and SetSlaveStatus.
// Because we run chmpx on multi-thread, so this method processes these function with locking.
//
bool ChmIMData::SetSlaveAll(chmpxid_t chmpxid, const char* hostname, short ctlport, const PCHMPXSSL pssl, int sock, chmpxsts_t status)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);

	// check for existing
	string		tmpname;
	short		tmpctlport = CHM_INVALID_PORT;
	if(!tmpchminfo.GetSlaveBase(chmpxid, tmpname, tmpctlport) || tmpname != hostname || tmpctlport != ctlport){
		// there is no same slave in chmshm, so set new slave.
		if(!tmpchminfo.SetSlaveBase(chmpxid, hostname, ctlport, pssl)){
			return false;
		}
	}else{
		// found same slave in chmshm
	}
	// set sock & status
	if(!tmpchminfo.SetSlaveSock(chmpxid, sock) || !tmpchminfo.SetSlaveStatus(chmpxid, status)){
		return false;
	}
	return true;
}

bool ChmIMData::RemoveSlaveSock(chmpxid_t chmpxid, int sock)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.RemoveSlaveSock(chmpxid, sock);
}

bool ChmIMData::RemoveSlave(chmpxid_t chmpxid)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.RemoveSlave(chmpxid, eqfd);
}

//---------------------------------------------------------
// Methods for stat
//---------------------------------------------------------
bool ChmIMData::AddStat(chmpxid_t chmpxid, bool is_sent, size_t bodylength, const struct timespec& elapsed_time)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.AddStat(chmpxid, is_sent, bodylength, elapsed_time);
}

bool ChmIMData::GetStat(PCHMSTAT pserver, PCHMSTAT pslave) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetStat(pserver, pslave);
}

//---------------------------------------------------------
// Methods for Trace(History)
//---------------------------------------------------------
long ChmIMData::GetTraceCount(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return 0L;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chmloglap	tmpchmlog(&pChmShm->chmpxlog, pChmShm);
	return tmpchmlog.GetHistoryCount();
}

bool ChmIMData::IsTraceEnable(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	// This method is called many times, but there is little that this value is changed.
	//ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);			// Lock

	chmloglap	tmpchmlog(&pChmShm->chmpxlog, pChmShm);
	return tmpchmlog.IsEnable();
}

bool ChmIMData::EnableTrace(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chmloglap	tmpchmlog(&pChmShm->chmpxlog, pChmShm);
	return tmpchmlog.Enable();
}

bool ChmIMData::DisableTrace(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chmloglap	tmpchmlog(&pChmShm->chmpxlog, pChmShm);
	return tmpchmlog.Disable();
}

bool ChmIMData::AddTrace(logtype_t logtype, size_t length, const struct timespec& start, const struct timespec& fin)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_WRITE);			// Lock

	chmloglap	tmpchmlog(&pChmShm->chmpxlog, pChmShm);
	return tmpchmlog.Add(logtype, length, start, fin);
}

bool ChmIMData::GetTrace(PCHMLOGRAW plograwarr, long& arrsize, logtype_t dirmask, logtype_t devmask) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}
	ChmLock	AutoLock(CHMLT_IMDATA, CHMLT_READ);				// Lock

	chmloglap	tmpchmlog(&pChmShm->chmpxlog, pChmShm);
	return tmpchmlog.Get(plograwarr, arrsize, dirmask, devmask);
}

//---------------------------------------------------------
// Methods for PIDs
//---------------------------------------------------------
//
// Lock Client pid list address in CHMSHM.
//
// Because client_pids and free_pids list are accessed by chmpx and client processes.
// Then we use hard lock those area, but those area does not access often.
//
bool ChmIMData::RawLockClientPidList(FLRwlRcsv& lockobj, bool is_read) const
{
	if(!IsAttachedShm()){
		return false;
	}
	// get offset
	chminfolap		tmpchminfo(&pChmShm->info, pChmShm);
	off_t			offset = reinterpret_cast<off_t>(tmpchminfo.GetClientPidListOffset());

	return lockobj.Lock(ShmFd, offset, 1L, is_read);
}

bool ChmIMData::RetriveClientPid(pid_t pid)
{
	if(CHM_INVALID_PID == pid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	FLRwlRcsv	lockobj;
	if(!WriteLockClientPidList(lockobj)){
		return false;
	}
	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.RetriveClientPid(pid);
}

bool ChmIMData::AddClientPid(pid_t pid)
{
	if(CHM_INVALID_PID == pid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	FLRwlRcsv	lockobj;
	if(!WriteLockClientPidList(lockobj)){
		return false;
	}
	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.AddClientPid(pid);
}

bool ChmIMData::GetAllPids(pidlist_t& list)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	FLRwlRcsv	lockobj;
	if(!ReadLockClientPidList(lockobj)){
		return false;
	}
	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.GetAllPids(list);
}

bool ChmIMData::IsClientPids(void) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	FLRwlRcsv	lockobj;
	if(!ReadLockClientPidList(lockobj)){
		return false;
	}
	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	return tmpchminfo.IsClientPids();
}

//
// Lock Chmpx pid address in CHMSHM.
//
bool ChmIMData::LockChmpxPid(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	off_t		offset;
	{
		// [NOTICE] now pid address offset is 0.
		if(NULL == tmpchminfo.GetChmpxSvrPidAddr(true)){
			ERR_CHMPRN("Could not chmpx pid address(offset) in CHMSHM.");
			return false;
		}
		offset = reinterpret_cast<off_t>(tmpchminfo.GetChmpxSvrPidAddr(false));		// get offset
	}

	return (0 == fullock_rwlock_wrlock(ShmFd, offset, 1L));
}

//
// Unlock Chmpx pid address in CHMSHM.
//
bool ChmIMData::UnlockChmpxPid(void)
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	off_t		offset;
	{
		// [NOTICE] now pid address offset is 0.
		if(NULL == tmpchminfo.GetChmpxSvrPidAddr(true)){
			ERR_CHMPRN("Could not chmpx pid address(offset) in CHMSHM.");
			return false;
		}
		offset = reinterpret_cast<off_t>(tmpchminfo.GetChmpxSvrPidAddr(false));		// get offset
	}

	return (0 == fullock_rwlock_unlock(ShmFd, offset, 1L));
}

//
// Check lock status for Chmpx pid address in CHMSHM.
//
bool ChmIMData::IsChmpxProcessRunning(pid_t& pid) const
{
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	chminfolap	tmpchminfo(&pChmShm->info, pChmShm);
	off_t		offset;
	{
		// [NOTICE] now pid address offset is 0.
		if(NULL == tmpchminfo.GetChmpxSvrPidAddr(true)){
			ERR_CHMPRN("Could not chmpx pid address(offset) in CHMSHM.");
			return false;
		}
		offset = reinterpret_cast<off_t>(tmpchminfo.GetChmpxSvrPidAddr(false));		// get offset
	}

	if(!fullock_rwlock_islocked(ShmFd, offset, 1L)){
		MSG_CHMPRN("Chmpx Pid address is not Locked: fd(%d), offset(%jd)", ShmFd, static_cast<intmax_t>(offset));
		return false;
	}
	//MSG_CHMPRN("Chmpx Pid address is Locked: fd(%d), offset(%jd)", ShmFd, static_cast<intmax_t>(offset));

	pid = tmpchminfo.GetChmpxSvrPid();

	return true;
}

bool ChmIMData::IsNeedDettach(void) const
{
	pid_t	pid = CHM_INVALID_PID;
	if(IsChmpxProcessRunning(pid) && ChmpxPid == pid){
		//MSG_CHMPRN("Not need to dettach chmshm.");
		return false;
	}
	MSG_CHMPRN("Need to dettach chmshm.");
	return true;
}

//---------------------------------------------------------
// Methods for Others
//---------------------------------------------------------
bool ChmIMData::IsAllowHostname(const char* hostname, const short* pport, PCHMPXSSL* ppssl)
{
	if(CHMEMPTYSTR(hostname)){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(!IsAttachedShm()){
		ERR_CHMPRN("There is no attached ChmShm.");
		return false;
	}

	// for cache key
	string	strhname(hostname);
	if(pport && CHM_INVALID_PORT != *pport){
		strhname += ":";
		strhname += to_string(*pport);
	}

	// check cache
	if(hnamesslmap.end() != hnamesslmap.find(strhname)){
		// found in cache
		if(ppssl){
			*ppssl = hnamesslmap[strhname];
		}
		return true;
	}

	// check name in server node list.
	chminfolap		tmpchminfo(&pChmShm->info, pChmShm);
	hnamesslmap_t	info;
	if(!tmpchminfo.GetAllServerName(info)){
		ERR_CHMPRN("Could not get server node names.");
		FREE_HNAMESSLMAP(info);
		return false;
	}
	if(info.end() != info.find(strhname)){
		// found & set cache
		hnamesslmap[strhname] = info[strhname];
		if(ppssl){
			*ppssl = hnamesslmap[strhname];
		}
		info[strhname] = NULL;
		FREE_HNAMESSLMAP(info);
		return true;
	}
	FREE_HNAMESSLMAP(info);

	// check name in configration server/slave list.
	//
	// [NOTE]
	// At first using hostname in list(means useing cache), next check DNS for server name if the first checking failed.
	//
	CHMNODE_CFGINFO	nodeinfo;
	if(pConfObj->GetNodeInfo(hostname, pport ? *pport : CHM_INVALID_PORT, nodeinfo, false, false) || pConfObj->GetNodeInfo(hostname, pport ? *pport : CHM_INVALID_PORT, nodeinfo, false, true)){
		// found
		if(pport){
			PCHMPXSSL	pssl = new CHMPXSSL;
			CVT_SSL_STRUCTURE(*pssl, nodeinfo);
			hnamesslmap[strhname] = pssl;
		}else{
			hnamesslmap[strhname] = NULL;
		}
		if(ppssl){
			*ppssl = hnamesslmap[strhname];
		}
		return true;
	}
	return false;
}

/*
 * VIM modelines
 *
 * vim:set ts=4 fenc=utf-8:
 */
