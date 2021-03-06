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
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <poll.h>
#include <time.h>
#include <assert.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "chmcommon.h"
#include "chmeventsock.h"
#include "chmcomstructure.h"
#include "chmstructure.tcc"
#include "chmcntrl.h"
#include "chmsigcntrl.h"
#include "chmnetdb.h"
#include "chmlock.h"
#include "chmutil.h"
#include "chmdbg.h"

using namespace	std;

//---------------------------------------------------------
// Utility Macros
//---------------------------------------------------------
#define	CVT_ESTR_NULL(pstr)					(CHMEMPTYSTR(pstr) ? NULL : pstr)

//---------------------------------------------------------
// Symbols
//---------------------------------------------------------
#define	CHMEVSOCK_MERGE_THREAD_NAME			"ChmEventSock-Merge"
#define	CHMEVSOCK_SOCK_THREAD_NAME			"ChmEventSock-Socket"

// Control Commands
#define	CTL_COMMAND_MAX_LENGTH				8192
#define	CTL_RECEIVE_MAX_LENGTH				(CTL_COMMAND_MAX_LENGTH * 2)

#define	CTL_COMMAND_DUMP_IMDATA				"DUMP"
#define	CTL_COMMAND_START_MERGE				"MERGE"
#define	CTL_COMMAND_STOP_MERGE				"ABORTMERGE"
#define	CTL_COMMAND_COMPLETE_MERGE			"COMPMERGE"
#define	CTL_COMMAND_SERVICE_IN				"SERVICEIN"
#define	CTL_COMMAND_SERVICE_OUT				"SERVICEOUT"
#define	CTL_COMMAND_SELF_STATUS				"SELFSTATUS"
#define	CTL_COMMAND_ALLSVR_STATUS			"ALLSTATUS"
#define	CTL_COMMAND_TRACE_SET				"TRACE"
#define	CTL_COMMAND_TRACE_VIEW				"TRACEVIEW"

#define	CTL_COMMAND_TRACE_SET_ENABLE1		"ENABLE"
#define	CTL_COMMAND_TRACE_SET_ENABLE2		"YES"
#define	CTL_COMMAND_TRACE_SET_ENABLE3		"ON"
#define	CTL_COMMAND_TRACE_SET_DISABLE1		"DISABLE"
#define	CTL_COMMAND_TRACE_SET_DISABLE2		"NO"
#define	CTL_COMMAND_TRACE_SET_DISABLE3		"OFF"
#define	CTL_COMMAND_TRACE_VIEW_DIR			"DIR="
#define	CTL_COMMAND_TRACE_VIEW_DEV			"DEV="
#define	CTL_COMMAND_TRACE_VIEW_ALL			"ALL"
#define	CTL_COMMAND_TRACE_VIEW_IN			"IN"
#define	CTL_COMMAND_TRACE_VIEW_OUT			"OUT"
#define	CTL_COMMAND_TRACE_VIEW_SOCK			"SOCK"
#define	CTL_COMMAND_TRACE_VIEW_MQ			"MQ"

#define	CTL_RES_SUCCESS						"SUCCEED\n"
#define	CTL_RES_SUCCESS_NOSERVER			"SUCCEED: There no server on RING.\n"
#define	CTL_RES_SUCCESS_STATUS_NOTICE		"SUCCEES: Send status notice to no server on RING.\n"
#define	CTL_RES_ERROR						"ERROR: Something error is occured.\n"
#define	CTL_RES_ERROR_PARAMETER				"ERROR: Parameters are wrong.\n"
#define	CTL_RES_ERROR_COMMUNICATION			"ERROR: Something error is occured in sending/receiveing data on RING.\n"
#define	CTL_RES_ERROR_SERVICE_OUT_PARAM		"ERROR: SERVICEOUT command must have parameter as server/ctlport.(ex: \"SERVICEOUT servername.fqdn:ctlport\")\n"
#define	CTL_RES_ERROR_MERGE_START			"ERROR: Failed to start merging.\n"
#define	CTL_RES_ERROR_MERGE_ABORT			"ERROR: Failed to stop merging.\n"
#define	CTL_RES_ERROR_MERGE_COMPLETE		"ERROR: Failed to complete merging.\n"
#define	CTL_RES_ERROR_MERGE_AUTO			"ERROR: Failed to start merging or complete merging.\n"
#define	CTL_RES_ERROR_NOT_FOUND_SVR			"ERROR: There is no such as server.\n"
#define	CTL_RES_ERROR_NOT_SERVERMODE		"ERROR: Command must run on server mode.\n"
#define	CTL_RES_ERROR_NOT_SRVIN				"ERROR: Server is not \"service in\" on RING.\n"
#define	CTL_RES_ERROR_OPERATING				"ERROR: Could not change status, because server is operating now.\n"
#define	CTL_RES_ERROR_STATUS_NOT_ALLOWED	"ERROR: Could not change status, because status is not allowed.\n"
#define	CTL_RES_ERROR_STATUS_HAS_SUSPEND	"ERROR: Could not change status, because status has SUSPEND.\n"
#define	CTL_RES_ERROR_SOME_SERVER_SUSPEND	"ERROR: Could not change status, because some servers status is SUSPEND.\n"
#define	CTL_RES_ERROR_COULD_NOT_SERVICE_OUT	"ERROR: Server could not set SERVICE OUT on RING.\n"
#define	CTL_RES_ERROR_NOT_CHANGE_STATUS		"ERROR: Server can not be changed status.\n"
#define	CTL_RES_ERROR_CHANGE_STATUS			"ERROR: Failed to change server status.\n"
#define	CTL_RES_ERROR_TRANSFER				"ERROR: Failed to transfer command to other target server, something error occured on target server.\n"
#define	CTL_RES_ERROR_STATUS_NOTICE			"ERROR: Failed to send status notice to other servers.\n"
#define	CTL_RES_ERROR_GET_CHMPXSVR			"ERROR: Failed to get all information.\n"
#define	CTL_RES_ERROR_GET_STAT				"ERROR: Failed to get all stat.\n"
#define	CTL_RES_ERROR_TRACE_SET_PARAM		"ERROR: TRACE command must have parameter as enable/disable.(ex: \"TRACE enable\")\n"
#define	CTL_RES_ERROR_TRACE_SET_ALREADY		"ERROR: TRACE is already enabled/disabled.\n"
#define	CTL_RES_ERROR_TRACE_SET_FAILED		"ERROR: Failed to set TRACE enable/disable.\n"
#define	CTL_RES_ERROR_TRACE_VIEW_PARAM		"ERROR: TRACEVIEW command parameter is \"TRACEVIEW [DIR=IN/OUT/ALL] [DEV=SOCK/MQ/ALL] [COUNT]\"\n"
#define	CTL_RES_ERROR_TRACE_VIEW_NOTRACE	"ERROR: Now trace count is 0.\n"
#define	CTL_RES_ERROR_TRACE_VIEW_NODATA		"ERROR: There is no matched trace log now.\n"
#define	CTL_RES_ERROR_TRACE_VIEW_INTERR		"ERROR: Internal error is occured for TRACEVIEW.\n"
#define	CTL_RES_INT_ERROR					"INTERNAL ERROR: Something error is occured.\n"
#define	CTL_RES_INT_ERROR_NOTGETCHMPX		"INTERNAL ERROR: Could not get chmpx servers information.\n"

// SSL
#define	CHMEVENTSOCK_SSL_VP_DEPTH			3
#define	CHMEVENTSOCK_VCB_INDEX_STR			"verify_cb_data_index"

// CheckResultSSL function type
#define	CHKRESULTSSL_TYPE_CON				0						// SSL_accept / SSL_connect
#define	CHKRESULTSSL_TYPE_RW				1						// SSL_read / SSL_write
#define	CHKRESULTSSL_TYPE_SD				2						// SSL_shutdown
#define	IS_SAFE_CHKRESULTSSL_TYPE(type)		(CHKRESULTSSL_TYPE_CON == type || CHKRESULTSSL_TYPE_RW == type || CHKRESULTSSL_TYPE_SD == type)

//---------------------------------------------------------
// Class valiable
//---------------------------------------------------------
const int			ChmEventSock::DEFAULT_SOCK_THREAD_CNT;
const int			ChmEventSock::DEFAULT_MAX_SOCK_POOL;
const time_t		ChmEventSock::DEFAULT_SOCK_POOL_TIMEOUT;
const time_t		ChmEventSock::NO_SOCK_POOL_TIMEOUT;
const int			ChmEventSock::DEFAULT_KEEPIDLE;
const int			ChmEventSock::DEFAULT_KEEPINTERVAL;
const int			ChmEventSock::DEFAULT_KEEPCOUNT;
const int			ChmEventSock::DEFAULT_LISTEN_BACKLOG;
const int			ChmEventSock::DEFAULT_RETRYCNT;
const int			ChmEventSock::DEFAULT_RETRYCNT_CONNECT;
const suseconds_t	ChmEventSock::DEFAULT_WAIT_SOCKET;
const suseconds_t	ChmEventSock::DEFAULT_WAIT_CONNECT;
int					ChmEventSock::CHM_SSL_VERIFY_DEPTH		= CHMEVENTSOCK_SSL_VP_DEPTH;
const char*			ChmEventSock::strVerifyCBDataIndex		= CHMEVENTSOCK_VCB_INDEX_STR;
int					ChmEventSock::verify_cb_data_index		= -1;
int					ChmEventSock::ssl_session_id			= static_cast<int>(getpid());
bool				ChmEventSock::is_self_sigined			= false;

//------------------------------------------------------
// Class Method for SSL
//------------------------------------------------------
int ChmEventSock::VerifyCallBackSSL(int preverify_ok, X509_STORE_CTX* store_ctx)
{
	char			strerr[256];
	X509*			err_cert	= X509_STORE_CTX_get_current_cert(store_ctx);
	int				err_code	= X509_STORE_CTX_get_error(store_ctx);
	int				depth		= X509_STORE_CTX_get_error_depth(store_ctx);
	SSL*			ssl			= reinterpret_cast<SSL*>(X509_STORE_CTX_get_ex_data(store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
	SSL_CTX*		ssl_ctx		= SSL_get_SSL_CTX(ssl);
	const int*		pchm_depth	= reinterpret_cast<const int*>(SSL_CTX_get_ex_data(ssl_ctx, ChmEventSock::verify_cb_data_index));

	// error string
	X509_NAME_oneline(X509_get_subject_name(err_cert), strerr, sizeof(strerr));

	// depth
	if(!pchm_depth || *pchm_depth < depth){
		// Force changing error code.
		preverify_ok= 0;
		err_code	= X509_V_ERR_CERT_CHAIN_TOO_LONG;
		X509_STORE_CTX_set_error(store_ctx, err_code);
	}

	// Message
	if(!preverify_ok){
		ERR_CHMPRN("VERIFY ERROR: depth=%d, errnum=%d(%s), string=%s", depth, err_code, X509_verify_cert_error_string(err_code), strerr);
	}else{
		MSG_CHMPRN("VERIFY OK: depth=%d, string=%s", depth, strerr);
	}

	// check error code
	switch(err_code){
		case	X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
			X509_NAME_oneline(X509_get_issuer_name(store_ctx->current_cert), strerr, 256);
			ERR_CHMPRN("DETAIL: X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT: issuer=%s", strerr);
			preverify_ok = 0;
			break;

		case	X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
			if(ChmEventSock::is_self_sigined){
				// For DEBUG
				WAN_CHMPRN("SKIP ERROR(DEBUG): X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT: self signed certificate is ERROR.");
				preverify_ok = 1;
			}else{
				ERR_CHMPRN("DETAIL: X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT: self signed certificate is ERROR.");
				preverify_ok = 0;
			}
			break;

		case	X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
			X509_STORE_CTX_set_error(store_ctx, X509_V_OK);
			WAN_CHMPRN("SKIP ERROR: X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN: Verified with no error in self signed certificate chain.");
			preverify_ok = 1;
			break;

		case	X509_V_ERR_CERT_NOT_YET_VALID:
			ERR_CHMPRN("DETAIL: X509_V_ERR_CERT_NOT_YET_VALID: certificate is not yet valid(date is after the current time).");
			preverify_ok = 0;
			break;

		case	X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
			ERR_CHMPRN("DETAIL: X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD: certificate not before field contains an invalid time.");
			preverify_ok = 0;
			break;

		case	X509_V_ERR_CERT_HAS_EXPIRED:
			ERR_CHMPRN("DETAIL: X509_V_ERR_CERT_HAS_EXPIRED: certificate has expired.");
			preverify_ok = 0;
			break;

		case	X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
			ERR_CHMPRN("DETAIL: X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD: certificate not after field contains an invalid time.");
			preverify_ok = 0;
			break;

		default:
			break;
	}
	return preverify_ok;
}

bool ChmEventSock::CheckResultSSL(int sock, SSL* ssl, long action_result, int type, bool& is_retry, bool& is_close, int retrycnt, suseconds_t waittime)
{
	if(CHM_INVALID_SOCK == sock || !ssl || !IS_SAFE_CHKRESULTSSL_TYPE(type)){
		ERR_CHMPRN("Parameters are wrong.");
		is_retry = false;
		return false;
	}
	if(CHMEVENTSOCK_RETRY_DEFAULT == retrycnt){
		retrycnt = ChmEventSock::DEFAULT_RETRYCNT;
		waittime = ChmEventSock::DEFAULT_WAIT_SOCKET;
	}
	is_retry = true;
	is_close = false;

	bool	result = true;
	int		werr;
	if(action_result <= 0){
		int	ssl_result = SSL_get_error(ssl, action_result);
		switch(ssl_result){
			case	SSL_ERROR_NONE:
				// Succeed.
				MSG_CHMPRN("SSL action result(%ld): ssl result(%d: %s), succeed.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
				break;

			case	SSL_ERROR_SSL:
				if(CHKRESULTSSL_TYPE_SD == type){
					WAN_CHMPRN("SSL action result(%ld): ssl result(%d: %s). so retry to shutdown.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					result	= false;
				}else{
					ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s), so something error occured(errno=%d).", action_result, ssl_result, ERR_error_string(ssl_result, NULL), errno);
					is_retry= false;
					is_close= true;
					result	= false;
				}
				break;

			case	SSL_ERROR_WANT_WRITE:
				// Wait for up
				if(0 != (werr = ChmEventSock::WaitForReady(sock, WAIT_WRITE_FD, retrycnt, false, waittime))){		// not check SO_ERROR
					ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s), and Failed to wait write.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					is_retry = false;
					if(ETIMEDOUT != werr){
						is_close= true;
					}
				}else{
					WAN_CHMPRN("SSL action result(%ld): ssl result(%d: %s), and Succeed to wait write.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
				}
				result = false;
				break;

			case	SSL_ERROR_WANT_READ:
				// Wait for up
				if(0 != (werr = ChmEventSock::WaitForReady(sock, WAIT_READ_FD, retrycnt, false, waittime))){		// not check SO_ERROR
					ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s), and Failed to wait read.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					is_retry = false;
					if(ETIMEDOUT != werr){
						is_close= true;
					}
				}else{
					WAN_CHMPRN("SSL action result(%ld): ssl result(%d: %s), and Succeed to wait read.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
				}
				result = false;
				break;

			case	SSL_ERROR_SYSCALL:
				if(action_result < 0){
					ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s), errno(%d).", action_result, ssl_result, ERR_error_string(ssl_result, NULL), errno);
					is_retry= false;
					result	= false;
				}else{	// action_result == 0
					if(CHKRESULTSSL_TYPE_CON == type){
						MSG_CHMPRN("SSL action result(%ld): ssl result(%d: %s), so this case is received illigal EOF after calling connect/accept, but no error(no=%d).", action_result, ssl_result, ERR_error_string(ssl_result, NULL), errno);
						result	= true;
					}else if(CHKRESULTSSL_TYPE_SD == type){
						WAN_CHMPRN("SSL action result(%ld): ssl result(%d: %s). so retry to shutdown.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
						result	= false;
					}else{
						ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s).", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
						is_retry= false;
						is_close= true;
						result	= false;
					}
				}
				break;

			case	SSL_ERROR_ZERO_RETURN:
				if(CHKRESULTSSL_TYPE_SD == type){
					WAN_CHMPRN("SSL action result(%ld): ssl result(%d: %s). so retry to shutdown.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					result	= false;
				}else{
					ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s), so the peer is closed.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					is_retry= false;
					is_close= true;
					result	= false;
				}
				break;

			default:
				if(CHKRESULTSSL_TYPE_SD == type){
					WAN_CHMPRN("SSL action result(%ld): ssl result(%d: %s). so retry to shutdown.", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					result	= false;
				}else{
					ERR_CHMPRN("SSL action result(%ld): ssl result(%d: %s).", action_result, ssl_result, ERR_error_string(ssl_result, NULL));
					is_retry= false;
					result	= false;
				}
				break;
		}
	}else{
		// Result is Success!
	}
	return result;
}

SSL_CTX* ChmEventSock::MakeSSLContext(const char* CApath, const char* CAfile, const char* server_cert, const char* server_prikey, const char* slave_cert, const char* slave_prikey, bool is_verify_peer)
{
	if(!CHMEMPTYSTR(CApath) && !CHMEMPTYSTR(CAfile)){
		ERR_CHMPRN("Parameters are wrong.");
		return NULL;
	}
	if((CHMEMPTYSTR(server_cert) || CHMEMPTYSTR(server_prikey)) && (CHMEMPTYSTR(slave_cert) || CHMEMPTYSTR(slave_prikey))){
		ERR_CHMPRN("Parameters are wrong.");
		return NULL;
	}

	// Make context data index
	if(-1 == ChmEventSock::verify_cb_data_index){
		// make new index
		ChmEventSock::verify_cb_data_index = SSL_CTX_get_ex_new_index(0, const_cast<char*>(ChmEventSock::strVerifyCBDataIndex), NULL, NULL, NULL);
	}

	SSL_CTX*	ctx;

	// Make context(without SSLv2)
	if(NULL == (ctx = SSL_CTX_new(SSLv23_method()))){
		ERR_CHMPRN("Could not make SSL Context.");
		return NULL;
	}
	SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);

	// Options/Modes
	SSL_CTX_set_options(ctx, SSL_OP_ALL);
	SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);			// Non blocking
	SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);		// Non blocking

	// Load CA cert
	if((CHMEMPTYSTR(CAfile) && CHMEMPTYSTR(CApath)) || 1 != SSL_CTX_load_verify_locations(ctx, CAfile, CApath)){
		if(!CHMEMPTYSTR(CAfile) || !CHMEMPTYSTR(CApath)){
			// Failed loading -> try to default CA
			WAN_CHMPRN("Failed to load CA certs, CApath=%s, CAfile=%s", CApath, CAfile);
		}
		// Load default CA
		if(1 != SSL_CTX_set_default_verify_paths(ctx)){
			ERR_CHMPRN("Failed to load default certs.");
			SSL_CTX_free(ctx);
			return NULL;
		}
	}

	// Set cert
	if(!CHMEMPTYSTR(server_cert) && !CHMEMPTYSTR(server_prikey)){
		// Set server cert
		if(1 != SSL_CTX_use_certificate_chain_file(ctx, server_cert)){
			// Failed loading server cert
			ERR_CHMPRN("Failed to set server cert(%s)", server_cert);
			SSL_CTX_free(ctx);
			return NULL;
		}
		// Set server private keys(no pass)
		if(1 != SSL_CTX_use_PrivateKey_file(ctx, server_prikey, SSL_FILETYPE_PEM)){		// **** Not use following functions for passwd and RSA ****
			// Failed loading server private key
			ERR_CHMPRN("Failed to load private key file(%s)", server_prikey);
			SSL_CTX_free(ctx);
			return NULL;
		}
		// Verify cert
		if(1 != SSL_CTX_check_private_key(ctx)){
			// Not success to verify private key.
			ERR_CHMPRN("Failed to verify server cert(%s) & server private key(%s)", server_cert, server_prikey);
			SSL_CTX_free(ctx);
			return NULL;
		}

		// Set session id to context
		SSL_CTX_set_session_id_context(ctx, reinterpret_cast<const unsigned char*>(&ChmEventSock::ssl_session_id), sizeof(ChmEventSock::ssl_session_id));

		// Set CA list for client(slave)
		if(!CHMEMPTYSTR(CAfile)){
			STACK_OF(X509_NAME)*	cert_names;
			if(NULL != (cert_names = SSL_load_client_CA_file(CAfile))){
				SSL_CTX_set_client_CA_list(ctx, cert_names);
			}else{
				WAN_CHMPRN("Failed to load client(slave) CA certs(%s)", CAfile);
			}
		}

	}else if(!CHMEMPTYSTR(slave_cert) && !CHMEMPTYSTR(slave_prikey)){
		// Set slave cert
		if(1 != SSL_CTX_use_certificate_chain_file(ctx, slave_cert)){
			// Failed loading slave cert
			ERR_CHMPRN("Failed to set slave cert(%s)", slave_cert);
			SSL_CTX_free(ctx);
			return NULL;
		}
		// Set slave private keys(no pass)
		if(1 != SSL_CTX_use_PrivateKey_file(ctx, slave_prikey, SSL_FILETYPE_PEM)){		// **** Not use following functions for passwd and RSA ****
			// Failed loading slave private key
			ERR_CHMPRN("Failed to load private key file(%s)", slave_prikey);
			SSL_CTX_free(ctx);
			return NULL;
		}
		// Verify cert
		if(1 != SSL_CTX_check_private_key(ctx)){
			// Not success to verify private key.
			ERR_CHMPRN("Failed to verify slave cert(%s) & slave private key(%s)", slave_cert, slave_prikey);
			SSL_CTX_free(ctx);
			return NULL;
		}

		// slave SSL context does not need verify flag.
		is_verify_peer = false;
	}

	// Make verify peer mode
	int	verify_mode = is_verify_peer ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE) : SSL_VERIFY_NONE;

	// Set callback
	SSL_CTX_set_verify(ctx, verify_mode, ChmEventSock::VerifyCallBackSSL);								// Set verify callback
	SSL_CTX_set_verify_depth(ctx, ChmEventSock::CHM_SSL_VERIFY_DEPTH + 1);								// Verify depth
	SSL_CTX_set_ex_data(ctx, ChmEventSock::verify_cb_data_index, &ChmEventSock::CHM_SSL_VERIFY_DEPTH);	// Set external data

	return ctx;
}

SSL* ChmEventSock::HandshakeSSL(SSL_CTX* ctx, int sock, bool is_accept, int con_retrycnt, suseconds_t con_waittime)
{
	if(!ctx || CHM_INVALID_SOCK == sock){
		ERR_CHMPRN("Parameters are wrong.");
		return NULL;
	}
	if(CHMEVENTSOCK_RETRY_DEFAULT == con_retrycnt){
		con_retrycnt = ChmEventSock::DEFAULT_RETRYCNT_CONNECT;
		con_waittime = ChmEventSock::DEFAULT_WAIT_CONNECT;
	}

	// make SSL object
	SSL*	ssl = SSL_new(ctx);
	if(!ssl){
		ERR_CHMPRN("Could not make SSL object from context.");
		return NULL;
	}
	if(is_accept){
		SSL_set_accept_state(ssl);
	}else{
	    SSL_set_connect_state(ssl);
	}
	SSL_set_fd(ssl, sock);

	// accept/connect
	bool	is_retry;
	bool	is_close = false;	// Not used this value.
	long	action_result;
	for(int tmp_retrycnt = con_retrycnt; 0 < tmp_retrycnt; --tmp_retrycnt){
		// accept
		if(is_accept){
			action_result = SSL_accept(ssl);
		}else{
			action_result = SSL_connect(ssl);
		}

		is_retry = true;
		if(ChmEventSock::CheckResultSSL(sock, ssl, action_result, CHKRESULTSSL_TYPE_CON, is_retry, is_close, con_retrycnt, con_waittime)){
			// success
			return ssl;
		}
		if(!is_retry){
			break;
		}
	}
	ERR_CHMPRN("Failed to %s SSL.", (is_accept ? "accept" : "connect"));

	// shutdown SSL
	if(!ChmEventSock::ShutdownSSL(sock, ssl, con_retrycnt, con_waittime)){
		ERR_CHMPRN("Failed to shutdown SSL, but continue...");
	}
	SSL_free(ssl);

	return NULL;
}

bool ChmEventSock::ShutdownSSL(int sock, SSL* ssl, int con_retrycnt, suseconds_t con_waittime)
{
	if(CHM_INVALID_SOCK == sock || !ssl){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(CHMEVENTSOCK_RETRY_DEFAULT == con_retrycnt){
		con_retrycnt = ChmEventSock::DEFAULT_RETRYCNT_CONNECT;
		con_waittime = ChmEventSock::DEFAULT_WAIT_CONNECT;
	}

	bool	is_retry;
	long	action_result;
	bool	is_close = false;		// Not used this value.
	for(int tmp_retrycnt = con_retrycnt; 0 < tmp_retrycnt; --tmp_retrycnt){
		// accept
		action_result	= SSL_shutdown(ssl);
		is_retry		= true;

		if(ChmEventSock::CheckResultSSL(sock, ssl, action_result, CHKRESULTSSL_TYPE_SD, is_retry, is_close, con_retrycnt, con_waittime)){
			// success
			return true;
		}
		if(!is_retry){
			break;
		}
	}
	ERR_CHMPRN("Failed to shutdown SSL for sock(%d).", sock);

	return false;
}

//---------------------------------------------------------
// Class Methods
//---------------------------------------------------------
bool ChmEventSock::SetNonblocking(int sock)
{
	if(CHM_INVALID_SOCK == sock){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	int	flags = fcntl(sock, F_GETFL, 0);
	if(0 != (flags & O_NONBLOCK)){
		//MSG_CHMPRN("sock(%d) already set nonblocking.", sock);
		return true;
	}
	if(-1 == fcntl(sock, F_SETFL, flags | O_NONBLOCK)){
		ERR_CHMPRN("Could not set nonblocking flag to sock(%d), errno=%d.", sock, errno);
		return false;
	}
	return true;
}

int ChmEventSock::WaitForReady(int sock, int type, int retrycnt, bool is_check_so_error, suseconds_t waittime)
{
	// [NOTE] For perforamnce
	// signal mask does not change after launching processes.
	// (because it is static member in ChmSignalCntrl)
	// So, we duplicate it's value as static value in this method.
	// And we do not lock at initializing this value.
	//
	static sigset_t	sigset;
	static bool		is_sigset_init = false;
	if(!is_sigset_init){
		ChmSigCntrl	sigcntrl;
		if(!sigcntrl.GetSignalMask(sigset)){
			ERR_CHMPRN("Could not get signal mask value.");
			return EPERM;
		}
		is_sigset_init = true;
	}

	if(CHM_INVALID_SOCK == sock || !ISSAFE_WAIT_FD(type) || (CHMEVENTSOCK_RETRY_DEFAULT != retrycnt && retrycnt < 0)){
		ERR_CHMPRN("Parameters are wrong.");
		return EPERM;
	}

	if(CHMEVENTSOCK_RETRY_DEFAULT == retrycnt){
		retrycnt = ChmEventSock::DEFAULT_RETRYCNT;
		waittime = ChmEventSock::DEFAULT_WAIT_SOCKET;
	}

	// setup fds for ppoll
	struct pollfd	fds;
	{
		fds.fd		= sock;
		fds.events	= (IS_WAIT_READ_FD(type) ? (POLLIN | POLLPRI | POLLRDHUP) : 0) | (IS_WAIT_WRITE_FD(type) ? (POLLOUT | POLLRDHUP) : 0);
		fds.revents	= 0;
	}
	struct timespec	ts;
	int				cnt;

	for(cnt = 0; cnt < (retrycnt + 1); cnt++){
		SET_TIMESPEC(&ts, 0, (waittime * 1000));	// if waittime is default(CHMEVENTSOCK_TIMEOUT_DEFAULT), no sleeping.

		// check ready
		int	rtcode = ppoll(&fds, 1, &ts, &sigset);
		if(-1 == rtcode){
			if(EINTR == errno){
				//MSG_CHMPRN("Waiting connected sock(%d) result is -1(and EINTR), try again.", sock);
				continue;
			}
			// something error
			MSG_CHMPRN("Failed to wait fd up for sock=%d by errno=%d.", sock, errno);
			return errno;

		}else if(0 != rtcode){
			if(is_check_so_error){
				int			conerr = 0;
				socklen_t	length = sizeof(int);
				if(0 > getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<void*>(&conerr), &length)){
					MSG_CHMPRN("Failed to getsockopt for sock(%d), errno=%d.", sock, errno);
					return errno;
				}
				if(EINPROGRESS == conerr || EINTR == conerr){
					MSG_CHMPRN("Waiting connected sock(%d) result is errno=%d, but this error needs to wait again.", sock, conerr);
					continue;
				}else if(0 != conerr){
					MSG_CHMPRN("Waiting connected sock(%d) result is errno=%d", sock, conerr);
					return conerr;
				}
				// OK
			}

			// [NOTE]
			// we do not check POLLERR, POLLNVAL and POLLHUP status.
			// These are not an error since it is one of the factors for canceling the wait state.
			// And if socket status is somthing wrong, probabry it can be caught after return this
			// method.
			//

			//MSG_CHMPRN("Succeed to wait up for connected sock=%d by rtcode=%d.", sock, rtcode);
			return 0;
		}
		// timeout -> retry
		//MSG_CHMPRN("Waiting connected sock(%d) result is ETIMEOUT(%ldus), try again.", sock, waittime);
	}
	//MSG_CHMPRN("Waiting connected sock(%d) result is ETIMEOUT(%ldus * %d).", sock, waittime, retrycnt);

	return ETIMEDOUT;
}

int ChmEventSock::Listen(const char* hostname, short port)
{
	if(CHM_INVALID_PORT == port){
		ERR_CHMPRN("Parameters are wrong.");
		return CHM_INVALID_SOCK;
	}

	int	sockfd = CHM_INVALID_SOCK;
	if(CHMEMPTYSTR(hostname)){
		struct addrinfo*	paddrinfo = NULL;
		// First, test for INET6
		if(!ChmNetDb::GetAnyAddrInfo(port, &paddrinfo, true)){
			WAN_CHMPRN("Failed to get IN6ADDR_ANY_INIT addrinfo, but continue for INADDR_ANY.");
		}else{
			sockfd = ChmEventSock::RawListen(paddrinfo);
			freeaddrinfo(paddrinfo);
		}
		// check
		if(CHM_INVALID_SOCK == sockfd){
			// failed to bind/listen by INET6, so try INET4
			if(!ChmNetDb::GetAnyAddrInfo(port, &paddrinfo, false)){
				ERR_CHMPRN("Failed to get INADDR_ANY addrinfo, so both IN6ADDR_ANY_INIT and INADDR_ANY are failed.");
				return CHM_INVALID_SOCK;
			}
			sockfd = ChmEventSock::RawListen(paddrinfo);
			freeaddrinfo(paddrinfo);
		}

	}else{
		struct addrinfo*	paddrinfo = NULL;
		if(!ChmNetDb::Get()->GetAddrInfo(hostname, port, &paddrinfo, true)){		// if "localhost", convert fqdn.
			ERR_CHMPRN("Failed to get addrinfo for %s:%d.", hostname, port);
			return CHM_INVALID_SOCK;
		}
		sockfd = ChmEventSock::RawListen(paddrinfo);
		freeaddrinfo(paddrinfo);
	}

	if(CHM_INVALID_SOCK == sockfd){
		ERR_CHMPRN("Could not make socket and listen on %s:%d.", CHMEMPTYSTR(hostname) ? "ADDR_ANY" : hostname, port);
		return CHM_INVALID_SOCK;
	}
	return sockfd;
}

int ChmEventSock::RawListen(struct addrinfo* paddrinfo)
{
	const int	opt_yes = 1;

	if(!paddrinfo){
		ERR_CHMPRN("Parameter is wrong.");
		return CHM_INVALID_SOCK;
	}

	// make socket, bind, listen
	int	sockfd = CHM_INVALID_SOCK;
	for(struct addrinfo* ptmpaddrinfo = paddrinfo; ptmpaddrinfo && CHM_INVALID_SOCK == sockfd; ptmpaddrinfo = ptmpaddrinfo->ai_next){
		if(IPPROTO_TCP != ptmpaddrinfo->ai_protocol){
			MSG_CHMPRN("protocol in addrinfo does not TCP, so check next addrinfo...");
			continue;
		}
		// socket
		if(-1 == (sockfd = socket(ptmpaddrinfo->ai_family, ptmpaddrinfo->ai_socktype, ptmpaddrinfo->ai_protocol))){
			ERR_CHMPRN("Failed to make socket by errno=%d, but continue to make next addrinfo...", errno);
			// sockfd = CHM_INVALID_SOCK;
			continue;
		}
		// sockopt(keepalive, etc)
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const void*>(&opt_yes), sizeof(int));
#ifdef SO_REUSEPORT
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const void*>(&opt_yes), sizeof(int));
#endif
		setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const void*>(&opt_yes), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const void*>(&opt_yes), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, reinterpret_cast<const void*>(&ChmEventSock::DEFAULT_KEEPIDLE), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, reinterpret_cast<const void*>(&ChmEventSock::DEFAULT_KEEPINTERVAL), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, reinterpret_cast<const void*>(&ChmEventSock::DEFAULT_KEEPCOUNT), sizeof(int));
		ChmEventSock::SetNonblocking(sockfd);	// NONBLOCKING

		// bind
		if(-1 == bind(sockfd, ptmpaddrinfo->ai_addr, ptmpaddrinfo->ai_addrlen)){
			ERR_CHMPRN("Failed to bind by errno=%d, but continue to make next addrinfo...", errno);
			CHM_CLOSESOCK(sockfd);
			continue;
		}

		// listen
		if(-1 == listen(sockfd, ChmEventSock::DEFAULT_LISTEN_BACKLOG)){
			ERR_CHMPRN("Failed to listen by errno=%d, but continue to make next addrinfo...", errno);
			CHM_CLOSESOCK(sockfd);
			continue;
		}
	}

	if(CHM_INVALID_SOCK == sockfd){
		ERR_CHMPRN("Could not make socket and listen.");
		return CHM_INVALID_SOCK;
	}
	return sockfd;
}

int ChmEventSock::Connect(const char* hostname, short port, bool is_blocking, int con_retrycnt, suseconds_t con_waittime)
{
	const int	opt_yes = 1;

	if(CHMEMPTYSTR(hostname) || CHM_INVALID_PORT == port){
		ERR_CHMPRN("Parameters are wrong.");
		return CHM_INVALID_SOCK;
	}

	// Get addrinfo
	struct addrinfo*	paddrinfo = NULL;
	if(!ChmNetDb::Get()->GetAddrInfo(hostname, port, &paddrinfo, true)){			// if "localhost", convert fqdn.
		ERR_CHMPRN("Failed to get addrinfo for %s:%d.", hostname, port);
		return CHM_INVALID_SOCK;
	}

	// make socket, bind, listen
	int	sockfd = CHM_INVALID_SOCK;
	for(struct addrinfo* ptmpaddrinfo = paddrinfo; ptmpaddrinfo && CHM_INVALID_SOCK == sockfd; ptmpaddrinfo = ptmpaddrinfo->ai_next){
		if(IPPROTO_TCP != ptmpaddrinfo->ai_protocol){
			MSG_CHMPRN("protocol in addrinfo which is made from %s:%d does not TCP, so check next addrinfo...", hostname, port);
			continue;
		}
		// socket
		if(-1 == (sockfd = socket(ptmpaddrinfo->ai_family, ptmpaddrinfo->ai_socktype, ptmpaddrinfo->ai_protocol))){
			ERR_CHMPRN("Failed to make socket for %s:%d by errno=%d, but continue to make next addrinfo...", hostname, port, errno);
			// sockfd = CHM_INVALID_SOCK;
			continue;
		}

		// options
		setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const void*>(&opt_yes), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const void*>(&opt_yes), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, reinterpret_cast<const void*>(&ChmEventSock::DEFAULT_KEEPIDLE), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, reinterpret_cast<const void*>(&ChmEventSock::DEFAULT_KEEPINTERVAL), sizeof(int));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, reinterpret_cast<const void*>(&ChmEventSock::DEFAULT_KEEPCOUNT), sizeof(int));
		if(!is_blocking){
			ChmEventSock::SetNonblocking(sockfd);	// NONBLOCKING
		}

		// connect
		if(-1 == connect(sockfd, ptmpaddrinfo->ai_addr, ptmpaddrinfo->ai_addrlen)){
			if(is_blocking || EINPROGRESS != errno){
				ERR_CHMPRN("Failed to connect for %s:%d by errno=%d, but continue to make next addrinfo...", hostname, port, errno);
				CHM_CLOSESOCK(sockfd);
				continue;
			}else{
				// wait connected...(non blocking & EINPROGRESS)
				int	werr = ChmEventSock::WaitForReady(sockfd, WAIT_WRITE_FD, con_retrycnt, true, con_waittime);		// check SO_ERROR
				if(0 != werr){
					MSG_CHMPRN("Failed to connect for %s:%d by errno=%d.", hostname, port, werr);
					CHM_CLOSESOCK(sockfd);
					continue;
				}
			}
		}
		if(CHM_INVALID_SOCK != sockfd){
			break;
		}
	}
	freeaddrinfo(paddrinfo);

	if(CHM_INVALID_SOCK == sockfd){
		MSG_CHMPRN("Could not make socket and connect %s:%d.", hostname, port);
	}
	return sockfd;
}

// [TODO]
// In most case, pComPkt is allocated memory. But in case of large data it should
// be able to allocate memory by shared memory.
//
bool ChmEventSock::RawSend(int sock, SSL* ssl, PCOMPKT pComPkt, bool& is_closed, bool is_blocking, int retrycnt, suseconds_t waittime)
{
	if(CHM_INVALID_SOCK == sock || !pComPkt){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	DUMPCOM_COMPKT_TYPE("Sock::RawSend", pComPkt);
	DUMPCOM_COMPKT("Sock::RawSend", pComPkt);

	// convert hton
	size_t	length = 0L;
	if(!ChmEventSock::hton(pComPkt, length)){
		ERR_CHMPRN("Failed to convert packet by hton.");
		return false;
	}
	//DUMPCOM_COMPKT("Sock::RawSend", pComPkt);

	if(length <= 0L){
		ERR_CHMPRN("Length(%zu) in PCOMPKT is wrong.", length);
		return false;
	}
	unsigned char*	pbydata = reinterpret_cast<unsigned char*>(pComPkt);

	// send
	return ChmEventSock::RawSend(sock, ssl, pbydata, length, is_closed, is_blocking, retrycnt, waittime);
}

bool ChmEventSock::RawSend(int sock, SSL* ssl, const unsigned char* pbydata, size_t length, bool& is_closed, bool is_blocking, int retrycnt, suseconds_t waittime)
{
	if(CHM_INVALID_SOCK == sock || !pbydata || length <= 0L){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}

	// send
	ssize_t	onesent = 0;
	size_t	totalsent;
	bool	is_retry = true;
	for(totalsent = 0; totalsent < length; totalsent += static_cast<size_t>(onesent)){
		if(ssl){
			// SSL
			onesent				= 0;
			is_retry 			= true;
			long	write_result= SSL_write(ssl, &pbydata[totalsent], length - totalsent);
			if(ChmEventSock::CheckResultSSL(sock, ssl, write_result, CHKRESULTSSL_TYPE_RW, is_retry, is_closed, retrycnt, waittime)){
				// success
				if(0 < write_result){
					onesent = static_cast<ssize_t>(write_result);
				}
			}else{
				if(!is_retry){
					// If the socket is closed, it occures notification. so nothing to do here.
					ERR_CHMPRN("Failed to write from SSL on sock(%d), and the socket is %s.", sock, (is_closed ? "closed" : "not closed"));
					return false;
				}
			}
		}else{
			// Not SSL
			if(!is_blocking){
				int	werr = ChmEventSock::WaitForReady(sock, WAIT_WRITE_FD, retrycnt, false, waittime);		// not check SO_ERROR
				if(0 != werr){
					ERR_CHMPRN("Failed to send PCOMPKT(length:%zu), because failed to wait ready for sending on sock(%d) by errno=%d.", length, sock, werr);
					if(ETIMEDOUT != werr){
						is_closed = true;
					}
					return false;
				}
			}

			if(-1 == (onesent = send(sock, &pbydata[totalsent], length - totalsent, is_blocking ? 0 : MSG_DONTWAIT))){
				if(EINTR == errno){
					// retry assap
					MSG_CHMPRN("Interapted signal during sending to sock(%d), errno=%d(EINTR).", sock, errno);

				}else if(EAGAIN == errno || EWOULDBLOCK == errno){
					// wait(non blocking)
					MSG_CHMPRN("sock(%d) does not ready for sending, errno=%d(EAGAIN or EWOULDBLOCK).", sock, errno);

				}else if(EACCES == errno || EBADF == errno || ECONNRESET == errno || ENOTCONN == errno || EDESTADDRREQ == errno || EISCONN == errno || ENOTSOCK == errno){
					// something error to closing
					ERR_CHMPRN("sock(%d) does not ready for sending, errno=%d(EACCES or EBADF or ECONNRESET or ENOTCONN or EDESTADDRREQ or EISCONN or ENOTSOCK).", sock, errno);
					is_closed = true;
					return false;

				}else{
					// failed
					ERR_CHMPRN("Failed to send PCOMPKT(length:%zu), errno=%d.", length, errno);
					return false;
				}
				// continue...
				onesent = 0;
			}
		}
	}
	return true;
}

// [TODO]
// For large data case, pbuff is shared memory instead of allocated memory.
//
bool ChmEventSock::RawReceiveByte(int sock, SSL* ssl, bool& is_closed, unsigned char* pbuff, size_t length, bool is_blocking, int retrycnt, suseconds_t waittime)
{
	if(CHM_INVALID_SOCK == sock || !pbuff || 0 == length){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	is_closed = false;

	// receive
	ssize_t	onerecv = 0;
	size_t	totalrecv;
	bool	is_retry = true;
	for(totalrecv = 0; totalrecv < length; totalrecv += static_cast<size_t>(onerecv)){
		if(ssl){
			// SSL
			onerecv				= 0;
			is_retry 			= true;
			long	read_result	= SSL_read(ssl, &pbuff[totalrecv], length - totalrecv);
			if(ChmEventSock::CheckResultSSL(sock, ssl, read_result, CHKRESULTSSL_TYPE_RW, is_retry, is_closed, retrycnt, waittime)){
				// success
				if(0 < read_result){
					onerecv = static_cast<ssize_t>(read_result);
				}
			}else{
				if(!is_retry){
					// If the socket is closed, it occures notification. so nothing to do here.
					ERR_CHMPRN("Failed to receive from SSL on sock(%d), and the socket is %s.", sock, (is_closed ? "closed" : "not closed"));
					return false;
				}
			}
		}else{
			// Not SSL
			if(!is_blocking){
				int	werr = ChmEventSock::WaitForReady(sock, WAIT_READ_FD, retrycnt, false, waittime);				// not check SO_ERROR
				if(0 != werr){
					ERR_CHMPRN("Failed to receive from sock(%d), because failed to wait ready for receiving, errno=%d.", sock, werr);
					if(ETIMEDOUT != werr){
						is_closed = true;
					}
					return false;
				}
			}

			if(-1 == (onerecv = recv(sock, &pbuff[totalrecv], length - totalrecv, is_blocking ? 0 : MSG_DONTWAIT))){
				if(EINTR == errno){
					// retry assap
					MSG_CHMPRN("Interapted signal during receiving from sock(%d), errno=%d(EINTR).", sock, errno);

				}else if(EAGAIN == errno || EWOULDBLOCK == errno){
					// wait(non blocking)
					MSG_CHMPRN("There are no received data on sock(%d), errno=%d(EAGAIN or EWOULDBLOCK).", sock, errno);

				}else if(EBADF == errno || ECONNREFUSED == errno || ENOTCONN == errno || ENOTSOCK == errno){
					// error for closing
					ERR_CHMPRN("There are no received data on sock(%d), errno=%d(EBADF or ECONNREFUSED or ENOTCONN or ENOTSOCK).", sock, errno);
					is_closed = true;
					return false;

				}else{
					// failed
					ERR_CHMPRN("Failed to receive from sock(%d), errno=%d.", sock, errno);
					return false;
				}
				// continue...
				onerecv = 0;

			}else if(0 == onerecv){
				// close sock
				//
				// [NOTICE]
				// We do not specify EPOLLRDHUP for epoll, then we know the socket closed by receive length = 0.
				// Because case of specified EPOLLRDHUP, there is possibility of a problem that the packet just
				// before FIN is replaced with FIN.
				//
				MSG_CHMPRN("Receive 0 byte from sock(%d), it means socket is closed.", sock);
				is_closed = true;
				return false;
			}
		}
	}
	return true;
}

bool ChmEventSock::RawReceiveAny(int sock, bool& is_closed, unsigned char* pbuff, size_t* plength, bool is_blocking, int retrycnt, suseconds_t waittime)
{
	if(CHM_INVALID_SOCK == sock || !pbuff || !plength){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	is_closed = false;

	// receive
	ssize_t	onerecv;
	while(true){
		if(!is_blocking){
			int	werr = ChmEventSock::WaitForReady(sock, WAIT_READ_FD, retrycnt, false, waittime);		// not check SO_ERROR
			if(0 != werr){
				ERR_CHMPRN("Failed to receive from sock(%d), because failed to wait ready for receiving, errno=%d.", sock, werr);
				if(ETIMEDOUT != werr){
					is_closed = true;
				}
				return false;
			}
		}

		if(-1 == (onerecv = recv(sock, pbuff, *plength, is_blocking ? 0 : MSG_DONTWAIT))){
			if(EINTR == errno){
				// retry
				MSG_CHMPRN("Interapted signal during receiving from sock(%d), errno=%d(EINTR).", sock, errno);

			}else if(EAGAIN == errno || EWOULDBLOCK == errno){
				// no data(return assap)
				MSG_CHMPRN("There are no received data on sock(%d), so not wait. errno=%d(EAGAIN or EWOULDBLOCK).", sock, errno);
				*plength = 0L;
				break;

			}else if(EBADF == errno || ECONNREFUSED == errno || ENOTCONN == errno || ENOTSOCK == errno){
				// error for closing
				ERR_CHMPRN("There are no received data on sock(%d), errno=%d(EBADF or ECONNREFUSED or ENOTCONN or ENOTSOCK).", sock, errno);
				is_closed = true;
				return false;

			}else{
				// failed
				ERR_CHMPRN("Failed to receive from sock(%d), errno=%d.", sock, errno);
				return false;
			}

		}else if(0 == onerecv){
			// close sock
			//
			// [NOTICE]
			// We do not specify EPOLLRDHUP for epoll, then we know the socket closed by receive *plength = 0.
			// Because case of specified EPOLLRDHUP, there is possibility of a problem that the packet just
			// before FIN is replaced with FIN.
			//
			ERR_CHMPRN("Receive 0 byte from sock(%d), it means socket is closed.", sock);
			is_closed = true;
			return false;

		}else{
			// read some bytes.
			*plength = static_cast<size_t>(onerecv);
			break;
		}
	}
	return true;
}

// [NOTICE]
// If *ppComPkt is NULL, this function returns *ppComPkt to allocated memory.
// The other(not NULL), this function set received data into *ppComPkt. Then pktlength
// means *ppComPkt buffer length.
//
bool ChmEventSock::RawReceive(int sock, SSL* ssl, bool& is_closed, PCOMPKT* ppComPkt, size_t pktlength, bool is_blocking, int retrycnt, suseconds_t waittime)
{
	if(CHM_INVALID_SOCK == sock || !ppComPkt){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	is_closed = false;

	// At first, read only COMPKT
	COMPKT			ComPkt;
	unsigned char*	pbyComPkt = reinterpret_cast<unsigned char*>(&ComPkt);
	if(!ChmEventSock::RawReceiveByte(sock, ssl, is_closed, pbyComPkt, sizeof(COMPKT), is_blocking, retrycnt, waittime)){
		if(is_closed){
			MSG_CHMPRN("Failed to receive only COMPKT from sock(%d), socket is closed.", sock);
		}else{
			ERR_CHMPRN("Failed to receive only COMPKT from sock(%d), socket is not closed.", sock);
		}
		return false;
	}
	//DUMPCOM_COMPKT("Sock::RawReceive", &ComPkt);

	// get remaining length
	size_t	totallength = 0L;
	{
		// ntoh
		NTOH_PCOMPKT(&ComPkt);
		if(ComPkt.length < sizeof(COMPKT)){
			ERR_CHMPRN("The packet length(%zu) in resieved COMPKT from sock(%d) is too short, should be %zu byte.", ComPkt.length, sock, sizeof(COMPKT));
			return false;
		}
		totallength = ComPkt.length;
	}
	DUMPCOM_COMPKT("Sock::RawReceive", &ComPkt);

	// build buffer & copy COMPKT
	unsigned char*	pbyall;
	bool			is_alloc = false;
	if(NULL == *ppComPkt){
		if(NULL == (pbyall = reinterpret_cast<unsigned char*>(malloc(totallength)))){
			ERR_CHMPRN("Coult not allocate memory(size=%zu)", totallength);
			return false;
		}
		*ppComPkt	= reinterpret_cast<PCOMPKT>(pbyall);
		is_alloc	= true;
	}else{
		if(pktlength < totallength){
			ERR_CHMPRN("Buffer length(%zu) is too small, receiving length is %zu, so COULD NOT READ REMAINING DATA.", pktlength, totallength);
			return false;
		}
		pbyall = reinterpret_cast<unsigned char*>(*ppComPkt);
	}
	memcpy(pbyall, pbyComPkt, sizeof(COMPKT));

	// Read remaining data
	if(!ChmEventSock::RawReceiveByte(sock, ssl, is_closed, &pbyall[sizeof(COMPKT)], totallength - sizeof(COMPKT), is_blocking, retrycnt, waittime)){
		if(is_closed){
			MSG_CHMPRN("Failed to receive after COMPKT from sock(%d), socket is closed.", sock);
		}else{
			ERR_CHMPRN("Failed to receive after COMPKT from sock(%d), socket is not closed.", sock);
		}
		if(is_alloc){
			CHM_Free(pbyall);
			*ppComPkt = NULL;
		}
		return false;
	}

	// ntoh
	if(!ChmEventSock::ntoh(*ppComPkt, true)){					// Already convert COMPKT
		ERR_CHMPRN("Failed to convert packet by ntoh.");
		if(is_alloc){
			CHM_Free(pbyall);
			*ppComPkt = NULL;
		}
		return false;
	}
	DUMPCOM_COMPKT_TYPE("Sock::RawReceive", *ppComPkt);

	return true;
}

//
// [NOTE]
// This method opens NEW control socket, so we do not need to lock socket for sending/receiving.
// BUT we must lock sockfd_lockval because this opens new socket.
//
bool ChmEventSock::RawSendCtlPort(const char* hostname, short ctlport, const unsigned char* pbydata, size_t length, string& strResult, volatile int& sockfd_lockval, int retrycnt, suseconds_t waittime, int con_retrycnt, suseconds_t con_waittime)
{
	strResult = "";

	if(CHMEMPTYSTR(hostname) || CHM_INVALID_PORT == ctlport || !pbydata || 0L == length){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	// try to connect to ctlport
	int	ctlsock;
	while(!fullock::flck_trylock_noshared_mutex(&sockfd_lockval));	// LOCK
	if(CHM_INVALID_SOCK == (ctlsock = ChmEventSock::Connect(hostname, ctlport, false, con_retrycnt, con_waittime))){
		// could not connect other server ctlport
		ERR_CHMPRN("Could not connect to %s:%d.", hostname, ctlport);
		fullock::flck_unlock_noshared_mutex(&sockfd_lockval);		// UNLOCK
		return false;
	}
	MSG_CHMPRN("Connected to %s:%d, then transfer command.", hostname, ctlport);
	fullock::flck_unlock_noshared_mutex(&sockfd_lockval);			// UNLOCK

	// send(does not lock for control socket)
	bool	is_closed = false;
	if(!ChmEventSock::RawSend(ctlsock, NULL, pbydata, length, is_closed, false, retrycnt, waittime)){
		ERR_CHMPRN("Could not send to %s:%d(sock:%d).", hostname, ctlport, ctlsock);
		if(!is_closed){
			CHM_CLOSESOCK(ctlsock);
		}
		return false;
	}

	// receive
	char	szReceive[CTL_RECEIVE_MAX_LENGTH];
	size_t	RecLength = CTL_RECEIVE_MAX_LENGTH - 1;
	memset(szReceive, 0, CTL_RECEIVE_MAX_LENGTH);

	if(!ChmEventSock::RawReceiveAny(ctlsock, is_closed, reinterpret_cast<unsigned char*>(szReceive), &RecLength, false, retrycnt * 10, waittime)){	// wait 10 times by normal
		ERR_CHMPRN("Failed to receive data from ctlport ctlsock(%d), ctlsock is %s.", ctlsock, is_closed ? "closed" : "not closed");
		if(!is_closed){
			CHM_CLOSESOCK(ctlsock);
		}
		return false;
	}
	CHM_CLOSESOCK(ctlsock);

	strResult = szReceive;
	return true;
}

bool ChmEventSock::hton(PCOMPKT pComPkt, size_t& length)
{
	if(!pComPkt){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	// backup
	comtype_t	type	= pComPkt->head.type;
	off_t		offset	= pComPkt->offset;
	length				= pComPkt->length;

	// hton
	HTON_PCOMPKT(pComPkt);

	if(COM_PX2PX == type){
		// following datas
		PPXCOM_ALL	pComPacket	= CHM_OFFSET(pComPkt, offset, PPXCOM_ALL);

		// backup
		pxcomtype_t	comtype		= pComPacket->val_head.type;
		//size_t	comlength	= pComPacket->val_head.length;		// unused now

		// hton by type
		if(CHMPX_COM_STATUS_REQ == comtype){
			PPXCOM_STATUS_REQ	pComContents = CVT_COMPTR_STATUS_REQ(pComPacket);
			HTON_PPXCOM_STATUS_REQ(pComContents);

		}else if(CHMPX_COM_STATUS_RES == comtype){
			PPXCOM_STATUS_RES	pComContents = CVT_COMPTR_STATUS_RES(pComPacket);
			// hton each chmpxsvr
			PCHMPXSVR	pChmpxsvr = CHM_OFFSET(pComContents, pComContents->pchmpxsvr_offset, PCHMPXSVR);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				HTON_PCHMPXSVR(&pChmpxsvr[cnt]);
			}
			HTON_PPXCOM_STATUS_RES(pComContents);

		}else if(CHMPX_COM_CONINIT_REQ == comtype){
			PPXCOM_CONINIT_REQ	pComContents = CVT_COMPTR_CONINIT_REQ(pComPacket);
			HTON_PPXCOM_CONINIT_REQ(pComContents);

		}else if(CHMPX_COM_CONINIT_RES == comtype){
			PPXCOM_CONINIT_RES	pComContents = CVT_COMPTR_CONINIT_RES(pComPacket);
			HTON_PPXCOM_CONINIT_RES(pComContents);

		}else if(CHMPX_COM_JOIN_RING == comtype){
			PPXCOM_JOIN_RING	pComContents = CVT_COMPTR_JOIN_RING(pComPacket);
			HTON_PPXCOM_JOIN_RING(pComContents);

		}else if(CHMPX_COM_STATUS_UPDATE == comtype){
			PPXCOM_STATUS_UPDATE	pComContents = CVT_COMPTR_STATUS_UPDATE(pComPacket);
			// hton each chmpxsvr
			PCHMPXSVR	pChmpxsvr = CHM_OFFSET(pComContents, pComContents->pchmpxsvr_offset, PCHMPXSVR);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				HTON_PCHMPXSVR(&pChmpxsvr[cnt]);
			}
			HTON_PPXCOM_STATUS_UPDATE(pComContents);

		}else if(CHMPX_COM_STATUS_CONFIRM == comtype){
			PPXCOM_STATUS_CONFIRM	pComContents = CVT_COMPTR_STATUS_CONFIRM(pComPacket);
			// hton each chmpxsvr
			PCHMPXSVR	pChmpxsvr = CHM_OFFSET(pComContents, pComContents->pchmpxsvr_offset, PCHMPXSVR);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				HTON_PCHMPXSVR(&pChmpxsvr[cnt]);
			}
			HTON_PPXCOM_STATUS_CONFIRM(pComContents);

		}else if(CHMPX_COM_STATUS_CHANGE == comtype){
			PPXCOM_STATUS_CHANGE	pComContents = CVT_COMPTR_STATUS_CHANGE(pComPacket);
			HTON_PPXCOM_STATUS_CHANGE(pComContents);

		}else if(CHMPX_COM_MERGE_START == comtype){
			PPXCOM_MERGE_START	pComContents = CVT_COMPTR_MERGE_START(pComPacket);
			HTON_PPXCOM_MERGE_START(pComContents);

		}else if(CHMPX_COM_MERGE_ABORT == comtype){
			PPXCOM_MERGE_ABORT	pComContents = CVT_COMPTR_MERGE_ABORT(pComPacket);
			HTON_PPXCOM_MERGE_ABORT(pComContents);

		}else if(CHMPX_COM_MERGE_COMPLETE == comtype){
			PPXCOM_MERGE_COMPLETE	pComContents = CVT_COMPTR_MERGE_COMPLETE(pComPacket);
			HTON_PPXCOM_MERGE_COMPLETE(pComContents);

		}else if(CHMPX_COM_SERVER_DOWN == comtype){
			PPXCOM_SERVER_DOWN	pComContents = CVT_COMPTR_SERVER_DOWN(pComPacket);
			HTON_PPXCOM_SERVER_DOWN(pComContents);

		}else if(CHMPX_COM_REQ_UPDATEDATA == comtype){
			PPXCOM_REQ_UPDATEDATA	pComContents = CVT_COMPTR_REQ_UPDATEDATA(pComPacket);
			// hton each map
			PPXCOM_REQ_IDMAP	pIdMap = CHM_OFFSET(pComContents, pComContents->plist_offset, PPXCOM_REQ_IDMAP);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				HTON_PPXCOM_REQ_IDMAP(&pIdMap[cnt]);
			}
			HTON_PPXCOM_REQ_UPDATEDATA(pComContents);

		}else if(CHMPX_COM_RES_UPDATEDATA == comtype){
			PPXCOM_RES_UPDATEDATA	pComContents = CVT_COMPTR_RES_UPDATEDATA(pComPacket);
			HTON_PPXCOM_RES_UPDATEDATA(pComContents);

		}else if(CHMPX_COM_RESULT_UPDATEDATA == comtype){
			PPXCOM_RESULT_UPDATEDATA	pComContents = CVT_COMPTR_RESULT_UPDATEDATA(pComPacket);
			HTON_PPXCOM_RESULT_UPDATEDATA(pComContents);

		}else{
			WAN_CHMPRN("ComPacket type is %s(%" PRIu64 ") which is unknown. so does not convert contents.", STRPXCOMTYPE(comtype), comtype);
		}
	}else if(COM_C2C == type){
		//
		// Not implement
		//

	}else{
		WAN_CHMPRN("Packet type is %s(%" PRIu64 "), why does send to other chmpx? so does not convert contents.", STRCOMTYPE(type), type);
	}
	return true;
}

bool ChmEventSock::ntoh(PCOMPKT pComPkt, bool is_except_compkt)
{
	if(!pComPkt){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}

	// ntoh
	if(!is_except_compkt){
		NTOH_PCOMPKT(pComPkt);
	}

	if(COM_PX2PX == pComPkt->head.type){
		// following datas
		PPXCOM_ALL	pComPacket	= CHM_OFFSET(pComPkt, pComPkt->offset, PPXCOM_ALL);
		pxcomtype_t	type		= be64toh(pComPacket->val_head.type);

		// ntoh by type
		if(CHMPX_COM_STATUS_REQ == type){
			PPXCOM_STATUS_REQ	pComContents = CVT_COMPTR_STATUS_REQ(pComPacket);
			NTOH_PPXCOM_STATUS_REQ(pComContents);

		}else if(CHMPX_COM_STATUS_RES == type){
			PPXCOM_STATUS_RES	pComContents = CVT_COMPTR_STATUS_RES(pComPacket);
			NTOH_PPXCOM_STATUS_RES(pComContents);
			// ntoh each chmpx
			PCHMPXSVR	pChmpxsvr = CHM_OFFSET(pComContents, pComContents->pchmpxsvr_offset, PCHMPXSVR);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				NTOH_PCHMPXSVR(&pChmpxsvr[cnt]);
			}

		}else if(CHMPX_COM_CONINIT_REQ == type){
			PPXCOM_CONINIT_REQ	pComContents = CVT_COMPTR_CONINIT_REQ(pComPacket);
			NTOH_PPXCOM_CONINIT_REQ(pComContents);

		}else if(CHMPX_COM_CONINIT_RES == type){
			PPXCOM_CONINIT_RES	pComContents = CVT_COMPTR_CONINIT_RES(pComPacket);
			NTOH_PPXCOM_CONINIT_RES(pComContents);

		}else if(CHMPX_COM_JOIN_RING == type){
			PPXCOM_JOIN_RING	pComContents = CVT_COMPTR_JOIN_RING(pComPacket);
			NTOH_PPXCOM_JOIN_RING(pComContents);

		}else if(CHMPX_COM_STATUS_UPDATE == type){
			PPXCOM_STATUS_UPDATE	pComContents = CVT_COMPTR_STATUS_UPDATE(pComPacket);
			NTOH_PPXCOM_STATUS_UPDATE(pComContents);
			// ntoh each chmpx
			PCHMPXSVR	pChmpxsvr = CHM_OFFSET(pComContents, pComContents->pchmpxsvr_offset, PCHMPXSVR);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				NTOH_PCHMPXSVR(&pChmpxsvr[cnt]);
			}

		}else if(CHMPX_COM_STATUS_CONFIRM == type){
			PPXCOM_STATUS_CONFIRM	pComContents = CVT_COMPTR_STATUS_CONFIRM(pComPacket);
			NTOH_PPXCOM_STATUS_CONFIRM(pComContents);
			// ntoh each chmpx
			PCHMPXSVR	pChmpxsvr = CHM_OFFSET(pComContents, pComContents->pchmpxsvr_offset, PCHMPXSVR);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				NTOH_PCHMPXSVR(&pChmpxsvr[cnt]);
			}

		}else if(CHMPX_COM_STATUS_CHANGE == type){
			PPXCOM_STATUS_CHANGE	pComContents = CVT_COMPTR_STATUS_CHANGE(pComPacket);
			NTOH_PPXCOM_STATUS_CHANGE(pComContents);

		}else if(CHMPX_COM_MERGE_START == type){
			PPXCOM_MERGE_START	pComContents = CVT_COMPTR_MERGE_START(pComPacket);
			NTOH_PPXCOM_MERGE_START(pComContents);

		}else if(CHMPX_COM_MERGE_ABORT == type){
			PPXCOM_MERGE_ABORT	pComContents = CVT_COMPTR_MERGE_ABORT(pComPacket);
			NTOH_PPXCOM_MERGE_ABORT(pComContents);

		}else if(CHMPX_COM_MERGE_COMPLETE == type){
			PPXCOM_MERGE_COMPLETE	pComContents = CVT_COMPTR_MERGE_COMPLETE(pComPacket);
			NTOH_PPXCOM_MERGE_COMPLETE(pComContents);

		}else if(CHMPX_COM_SERVER_DOWN == type){
			PPXCOM_SERVER_DOWN	pComContents = CVT_COMPTR_SERVER_DOWN(pComPacket);
			NTOH_PPXCOM_SERVER_DOWN(pComContents);

		}else if(CHMPX_COM_REQ_UPDATEDATA == type){
			PPXCOM_REQ_UPDATEDATA	pComContents = CVT_COMPTR_REQ_UPDATEDATA(pComPacket);
			NTOH_PPXCOM_REQ_UPDATEDATA(pComContents);
			// ntoh each chmpx
			PPXCOM_REQ_IDMAP	pIdMap = CHM_OFFSET(pComContents, pComContents->plist_offset, PPXCOM_REQ_IDMAP);
			for(long cnt = 0; cnt < pComContents->count; cnt++){
				NTOH_PPXCOM_REQ_IDMAP(&pIdMap[cnt]);
			}

		}else if(CHMPX_COM_RES_UPDATEDATA == type){
			PPXCOM_RES_UPDATEDATA	pComContents = CVT_COMPTR_RES_UPDATEDATA(pComPacket);
			NTOH_PPXCOM_RES_UPDATEDATA(pComContents);

		}else if(CHMPX_COM_RESULT_UPDATEDATA == type){
			PPXCOM_RESULT_UPDATEDATA	pComContents = CVT_COMPTR_RESULT_UPDATEDATA(pComPacket);
			NTOH_PPXCOM_RESULT_UPDATEDATA(pComContents);

		}else{
			WAN_CHMPRN("ComPacket type is %s(%" PRIu64 ") which is unknown. so does not convert contents.", STRPXCOMTYPE(type), type);
		}
	}else if(COM_C2C == pComPkt->head.type){
		//
		// Not implement
		//

	}else{
		WAN_CHMPRN("Packet type is %s(%" PRIu64 "), why does receive from other chmpx? so does not convert contents.", STRCOMTYPE(pComPkt->head.type), pComPkt->head.type);
	}
	return true;
}

//
// If ext_length is not PXCOMPKT_AUTO_LENGTH, allocate memory length as
// COMPKT + type + ext_length size.
//
PCOMPKT ChmEventSock::AllocatePxComPacket(pxcomtype_t type, ssize_t ext_length)
{
	ssize_t	length = sizeof(COMPKT) + (PXCOMPKT_AUTO_LENGTH == ext_length ? 0L : ext_length);

	if(CHMPX_COM_STATUS_REQ == type){
		length += sizeof(PXCOM_STATUS_REQ);

	}else if(CHMPX_COM_STATUS_RES == type){
		length += sizeof(PXCOM_STATUS_RES);

	}else if(CHMPX_COM_CONINIT_REQ == type){
		length += sizeof(PXCOM_CONINIT_REQ);

	}else if(CHMPX_COM_CONINIT_RES == type){
		length += sizeof(PXCOM_CONINIT_RES);

	}else if(CHMPX_COM_JOIN_RING == type){
		length += sizeof(PXCOM_JOIN_RING);

	}else if(CHMPX_COM_STATUS_UPDATE == type){
		length += sizeof(PXCOM_STATUS_UPDATE);

	}else if(CHMPX_COM_STATUS_CONFIRM == type){
		length += sizeof(PXCOM_STATUS_CONFIRM);

	}else if(CHMPX_COM_STATUS_CHANGE == type){
		length += sizeof(PXCOM_STATUS_CHANGE);

	}else if(CHMPX_COM_MERGE_START == type){
		length += sizeof(PXCOM_MERGE_START);

	}else if(CHMPX_COM_MERGE_ABORT == type){
		length += sizeof(PXCOM_MERGE_ABORT);

	}else if(CHMPX_COM_MERGE_COMPLETE == type){
		length += sizeof(PXCOM_MERGE_COMPLETE);

	}else if(CHMPX_COM_SERVER_DOWN == type){
		length += sizeof(PXCOM_SERVER_DOWN);

	}else if(CHMPX_COM_REQ_UPDATEDATA == type){
		length += sizeof(PXCOM_REQ_UPDATEDATA);

	}else if(CHMPX_COM_RES_UPDATEDATA == type){
		length += sizeof(PXCOM_RES_UPDATEDATA);

	}else if(CHMPX_COM_RESULT_UPDATEDATA == type){
		length += sizeof(PXCOM_RESULT_UPDATEDATA);

	}else{
		WAN_CHMPRN("ComPacket type is %s(%" PRIu64 ") which is unknown. so does not convert contents.", STRPXCOMTYPE(type), type);
	}

	PCOMPKT	rtnptr;
	if(NULL == (rtnptr = reinterpret_cast<PCOMPKT>(malloc(length)))){
		ERR_CHMPRN("Could not allocate memory as %zd length for %s(%" PRIu64 ").", length, STRPXCOMTYPE(type), type);
	}
	return rtnptr;
}

PCOMPKT ChmEventSock::DuplicateComPkt(PCOMPKT pComPkt)
{
	if(!pComPkt){
		ERR_CHMPRN("Parameter is wrong.");
		return NULL;
	}

	// allocation
	PCOMPKT	pDupPkt;
	if(NULL == (pDupPkt = reinterpret_cast<PCOMPKT>(malloc(pComPkt->length)))){
		ERR_CHMPRN("Could not allocate memory as %zu length.", pComPkt->length);
		return NULL;
	}

	// copy compkt
	COPY_COMPKT(pDupPkt, pComPkt);

	// copy after compkt
	if(sizeof(COMPKT) < pComPkt->length){
		unsigned char*	psrc	= reinterpret_cast<unsigned char*>(pComPkt) + sizeof(COMPKT);
		unsigned char*	pdest	= reinterpret_cast<unsigned char*>(pDupPkt) + sizeof(COMPKT);
		memcpy(pdest, psrc, (pComPkt->length - sizeof(COMPKT)));
	}
	return pDupPkt;
}

//---------------------------------------------------------
// Class Methods - Processing
//---------------------------------------------------------
bool ChmEventSock::ReceiveWorkerProc(void* common_param, chmthparam_t wp_param)
{
	ChmEventSock*		pSockObj	= reinterpret_cast<ChmEventSock*>(common_param);
	int					sock		= static_cast<int>(wp_param);
	if(!pSockObj || CHM_INVALID_SOCK == sock){
		ERR_CHMPRN("Paraemtera are wrong.");
		return true;		// sleep thread
	}
	// [NOTE]
	// We use edge triggerd epoll, in this type the event is accumulated and not sent when the data to
	// the socket is left. Thus we must read more until EAGAIN. Otherwise we lost data.
	// So we checked socket for rest data here.
	//
	bool		is_closed;
	suseconds_t	waittime = pSockObj->sock_wait_time;
	int			werr;
	while(0 == (werr = ChmEventSock::WaitForReady(sock, WAIT_READ_FD, 0, false, waittime))){		// check rest data & return assap
		// Processing
		is_closed = false;
		if(false == pSockObj->RawReceive(sock, is_closed)){
			if(!is_closed){
				ERR_CHMPRN("Failed to receive and to process for sock(%d) by event socket object.", sock);
			}else{
				MSG_CHMPRN("sock(%d) is closed while processing thread.", sock);
			}
			break;
		}
	}
	if((0 != werr && ETIMEDOUT != werr) || is_closed){
		if(!pSockObj->RawNotifyHup(sock)){
			ERR_CHMPRN("Failed to closing socket(%d), but continue...", sock);
		}
	}
	return true;		// always return true for continue to work.
}

//---------------------------------------------------------
// Class Methods - Merge
//---------------------------------------------------------
// Merge logic
//
// This merge thread(chmpx server node)
//		--- [MQ] -----> get lastest update time(to client on this server node through MQ)
//		<--------------
//
//		--- [SOCK] ---> push all update datas to this chmpx(to all other chmpx)
//			Other chmpx server node(status is up/servicein/nosuspend)
//				--- [MQ] ---> send request of pushing update datas(to client on server node)
//					client chmpx library
//						--- [CB] ---> loop: get update datas/send it to original chmpx
//				<-- [MQ] ---- finish
//			Other chmpx server node(status is NOT up/servicein/nosuspend)
//				nothing to do
//		<-- [SOCK] ---- finish
//
//	Wait for all server node finished.
//
// [NOTICE]
// This method is run on another worker thread.
// If need to stop this method, change "is_run_merge" flag to false.
// So that this method exits and return worker thread proc.
// After exiting this method, the worker proc is SLEEP.
//
// "is_run_merge" flag is changed only in main thread, so do not lock it.
//
bool ChmEventSock::MergeWorkerFunc(void* common_param, chmthparam_t wp_param)
{
	if(!common_param){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	ChmEventSock*	pThis = reinterpret_cast<ChmEventSock*>(common_param);

	if(!pThis->is_do_merge){
		WAN_CHMPRN("Why does this method call when is_do_merge flag is false.");
		pThis->is_run_merge = false;
		return false;
	}

	//---------------------------------
	// Make communication datas
	//---------------------------------
	// check target chmpxid map
	if(0 == pThis->mergeidmap.size()){
		ERR_CHMPRN("There is no merge chmpxids.");
		pThis->is_run_merge = false;
		return true;			// keep running thread for next request
	}
	// next chmpxid
	chmpxid_t	to_chmpxid	= pThis->GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == to_chmpxid){
		ERR_CHMPRN("There is no next chmpx server.");
		pThis->is_run_merge = false;
		return true;			// keep running thread for next request
	}
	// make merge param
	PXCOMMON_MERGE_PARAM	merge_param;
	struct timespec			lastts;
	// get lastest update time.
	if(!pThis->pChmCntrl->MergeGetLastTime(lastts)){
		ERR_CHMPRN("Something error occurred during getting lastest update time.");
		pThis->is_run_merge = false;
		return true;			// keep running thread for next request
	}
	merge_param.startts.tv_sec	= lastts.tv_sec;
	merge_param.startts.tv_nsec	= lastts.tv_nsec;

	// hash values
	ChmIMData*	pImData			= pThis->pChmCntrl->GetImDataObj();
	chmhash_t	tmp_hashval		= static_cast<chmhash_t>(-1);
	chmhash_t	tmp_max_hashval	= static_cast<chmhash_t>(-1);
	merge_param.chmpxid			= pImData->GetSelfChmpxId();
	merge_param.replica_count	= pImData->GetReplicaCount() + 1;	// ImData::Replica count means copy count
	merge_param.is_expire_check	= true;								// always check expire time

	if(!pImData->GetSelfPendingHash(tmp_hashval) || !pImData->GetMaxPendingHashCount(tmp_max_hashval)){
		ERR_CHMPRN("Could not get own hash and pending hash values.");
		pThis->is_run_merge = false;
		return true;			// keep running thread for next request
	}else{
		//
		// *** modify start hash value and replica count ***
		//
		// [NOTE]
		// Main hash value's data is had the servers which are assigned after main hash value with replica count.
		// Conversely, Main hash value's server has another servers data which is pointed by forwarding replica
		// count to hash value. Thus we modify start hash value(but do not change replica count(range)) at here.
		// For example)
		//		replica = 2, new hash value = 5, max hash value = 10  ---> start hash = 3, range = 2
		//		replica = 2, new hash value = 1, max hash value = 10  ---> start hash = 9, range = 2
		//
		if(static_cast<chmhash_t>(merge_param.replica_count) <= (tmp_hashval + 1)){
			tmp_hashval = (tmp_hashval + 1) - static_cast<chmhash_t>(merge_param.replica_count);
		}else{
			tmp_hashval = (tmp_hashval + tmp_max_hashval + 1) - static_cast<chmhash_t>(merge_param.replica_count);
		}
		if((tmp_max_hashval + 1) < static_cast<chmhash_t>(merge_param.replica_count)){
			// why, but set maximum
			merge_param.replica_count = static_cast<long>(tmp_max_hashval + 1);
		}
	}
	merge_param.pending_hash	= tmp_hashval;
	merge_param.max_pending_hash= tmp_max_hashval;

	//
	// [NOTE]
	// If this chmpx join at first time to ring, probabry base hash value is ignored.
	// (but max base hash is not ignored.)
	//
	tmp_hashval		= static_cast<chmhash_t>(-1);
	tmp_max_hashval	= static_cast<chmhash_t>(-1);
	if(!pImData->GetSelfBaseHash(tmp_hashval) || !pImData->GetMaxBaseHashCount(tmp_max_hashval) || tmp_hashval == static_cast<chmhash_t>(-1) || tmp_max_hashval == static_cast<chmhash_t>(-1)){
		// base hash value is ignored, maybe this process run and first join to ring.
		MSG_CHMPRN("Could not own base hash(or max base hash count), on this case we need to check all hash.");
		merge_param.base_hash		= static_cast<chmhash_t>(-1);
		merge_param.max_base_hash	= static_cast<chmhash_t>(-1);
	}else{
		if(static_cast<chmhash_t>(merge_param.replica_count) <= (tmp_hashval + 1)){
			tmp_hashval = (tmp_hashval + 1) - static_cast<chmhash_t>(merge_param.replica_count);
		}else{
			tmp_hashval = (tmp_hashval + tmp_max_hashval + 1) - static_cast<chmhash_t>(merge_param.replica_count);
		}
		if((tmp_max_hashval + 1) < static_cast<chmhash_t>(merge_param.replica_count)){
			// why, but set maximum
			merge_param.replica_count = static_cast<long>(tmp_max_hashval + 1);
		}
		merge_param.base_hash		= tmp_hashval;
		merge_param.max_base_hash	= tmp_max_hashval;
	}

	//---------------------------------
	// send request to other server node
	//---------------------------------
	if(!pThis->PxComSendReqUpdateData(to_chmpxid, &merge_param)){
		ERR_CHMPRN("Failed to send request update datas command.");
		pThis->is_run_merge = false;
		return true;			// keep running thread for next request
	}

	//---------------------------------
	// Loop: wait for all chmpx
	//---------------------------------
	struct timespec	sleeptime		= {0, 100 * 1000 * 1000};		// 100ms
	bool			is_all_finish	= false;
	while(pThis->is_run_merge && !is_all_finish){
		// check exited
		is_all_finish = true;

		while(!fullock::flck_trylock_noshared_mutex(&(pThis->mergeidmap_lockval)));	// LOCK
		for(mergeidmap_t::const_iterator iter = pThis->mergeidmap.begin(); iter != pThis->mergeidmap.end(); ++iter){
			if(!IS_PXCOM_REQ_UPDATE_RESULT(iter->second)){
				is_all_finish = false;
				break;
			}
		}
		fullock::flck_unlock_noshared_mutex(&(pThis->mergeidmap_lockval));			// UNLOCK

		if(pThis->is_run_merge && !is_all_finish){
			// sleep for wait
			nanosleep(&sleeptime, NULL);
		}
	}
	if(!pThis->is_run_merge){
		// If stop merging, exit this method immediately.
		// Do not change status, changing status is done by main thread which calls abort merging.
		return true;
	}

	// Done
	// change status.
	//
	if(pThis->is_run_merge){
		if(!pThis->MergeDone()){
			ERR_CHMPRN("Failed to change status \"DONE\".");
			pThis->is_run_merge = false;
			return true;			// keep running thread for next request
		}
	}
	pThis->is_run_merge = false;

	return true;
}

//---------------------------------------------------------
// Class Methods - Lock map
//---------------------------------------------------------
bool ChmEventSock::ServerSockMapCallback(sock_ids_map_t::iterator& iter, void* psockobj)
{
	ChmEventSock*	pSockObj = reinterpret_cast<ChmEventSock*>(psockobj);
	if(!pSockObj){
		ERR_CHMPRN("Parameter is wrong.");
		return true;										// do not stop loop.
	}
	int	sock = iter->first;
	if(CHM_INVALID_SOCK != sock){
		// close
		pSockObj->UnlockSendSock(sock);						// UNLOCK SOCK(For safety)
		pSockObj->CloseSocketWithEpoll(sock);				// not check result
	}

	ChmIMData*	pImData = pSockObj->pChmCntrl->GetImDataObj();
	if(pImData){
		chmpxid_t	chmpxid = pImData->GetChmpxIdBySock(sock, CLOSETG_SERVERS);
		if(CHM_INVALID_CHMPXID != chmpxid){
			if(chmpxid != iter->second){
				ERR_CHMPRN("Not same slave chmpxid(0x%016" PRIx64 " : 0x%016" PRIx64 "), by socket in imdata and by parameter", chmpxid, iter->second);
			}
			// set invalid sock in server list
			if(CHM_INVALID_SOCK != sock && !pImData->RemoveServerSock(chmpxid, sock)){
				ERR_CHMPRN("Could not set sock(INVALID) to chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
			}
		}
	}
	return true;
}

bool ChmEventSock::SlaveSockMapCallback(sock_ids_map_t::iterator& iter, void* psockobj)
{
	ChmEventSock*	pSockObj = reinterpret_cast<ChmEventSock*>(psockobj);
	if(!pSockObj){
		ERR_CHMPRN("Parameter is wrong.");
		return true;										// do not stop loop.
	}

	int	sock = iter->first;
	if(CHM_INVALID_SOCK != sock){
		// close
		pSockObj->UnlockSendSock(sock);						// UNLOCK SOCK(For safety)
		pSockObj->CloseSocketWithEpoll(sock);				// not check result
	}

	ChmIMData*	pImData	= pSockObj->pChmCntrl->GetImDataObj();
	if(pImData){
		chmpxid_t	chmpxid = pImData->GetChmpxIdBySock(sock, CLOSETG_SLAVES);
		if(CHM_INVALID_CHMPXID != chmpxid && chmpxid == iter->second){
			if(CHM_INVALID_SOCK != sock && !pImData->RemoveSlaveSock(chmpxid, sock)){
				ERR_CHMPRN("Failed to remove slave(0x%016" PRIx64 ") information, but continue...", iter->second);
			}
		}else{
			ERR_CHMPRN("Not same slave chmpxid(0x%016" PRIx64 " : 0x%016" PRIx64 "), by socket in imdata and by parameter", chmpxid, iter->second);
		}
	}
	return true;
}

bool ChmEventSock::AcceptMapCallback(sock_pending_map_t::iterator& iter, void* psockobj)
{
	ChmEventSock*	pSockObj = reinterpret_cast<ChmEventSock*>(psockobj);
	if(!pSockObj){
		ERR_CHMPRN("Parameter is wrong.");
		return true;										// do not stop loop.
	}
	// close
	pSockObj->UnlockSendSock(iter->first);					// UNLOCK SOCK(For safety)
	pSockObj->CloseSocketWithEpoll(iter->first);			// not check result

	return true;
}

bool ChmEventSock::ControlSockMapCallback(sock_ids_map_t::iterator& iter, void* psockobj)
{
	ChmEventSock*	pSockObj = reinterpret_cast<ChmEventSock*>(psockobj);
	if(!pSockObj){
		ERR_CHMPRN("Parameter is wrong.");
		return true;										// do not stop loop.
	}
	// close
	pSockObj->UnlockSendSock(iter->first);					// UNLOCK SOCK(For safety)
	pSockObj->CloseSocketWithEpoll(iter->first);			// not check result

	return true;
}

bool ChmEventSock::SslSockMapCallback(sock_ssl_map_t::iterator& iter, void* psockobj)
{
	ChmEventSock*	pSockObj = reinterpret_cast<ChmEventSock*>(psockobj);
	if(!pSockObj){
		ERR_CHMPRN("Parameter is wrong.");
		return true;										// do not stop loop.
	}
	int		sock = iter->first;
	SSL*	pssl = iter->second;

	pSockObj->UnlockSendSock(sock);							// UNLOCK SOCK

	if(pssl){
		ChmEventSock::ShutdownSSL(sock, pssl, pSockObj->con_retry_count, pSockObj->con_wait_time);
		SSL_free(pssl);
	}
	return true;
}

bool ChmEventSock::SendLockMapCallback(sendlockmap_t::iterator& iter, void* psockobj)
{
	ChmEventSock*	pSockObj = reinterpret_cast<ChmEventSock*>(psockobj);
	if(!pSockObj){
		ERR_CHMPRN("Parameter is wrong.");
		return true;										// do not stop loop.
	}
	//int		sock	= iter->first;
	PCHMSSSTAT	pssstat	= iter->second;
	CHM_Delete(pssstat);
	return true;
}

//---------------------------------------------------------
// Methods
//---------------------------------------------------------
ChmEventSock::ChmEventSock(int eventqfd, ChmCntrl* pcntrl) : 
	ChmEventBase(eventqfd, pcntrl),
	seversockmap(CHM_INVALID_CHMPXID, ChmEventSock::ServerSockMapCallback, this),
	slavesockmap(CHM_INVALID_CHMPXID, ChmEventSock::SlaveSockMapCallback, this),
	acceptingmap(string(""), ChmEventSock::AcceptMapCallback, this),
	ctlsockmap(CHM_INVALID_CHMPXID, ChmEventSock::ControlSockMapCallback, this),
	sslmap(NULL, ChmEventSock::SslSockMapCallback, this),
	sendlockmap(NULL, ChmEventSock::SendLockMapCallback, this),
	svr_sslctx(NULL), slv_sslctx(NULL), is_do_merge(false), is_auto_merge(false), is_run_merge(false),
	procthreads(CHMEVSOCK_SOCK_THREAD_NAME), mergethread(CHMEVSOCK_MERGE_THREAD_NAME),
	sockfd_lockval(FLCK_NOSHARED_MUTEX_VAL_UNLOCKED), last_check_time(time(NULL)), dyna_sockfd_lockval(FLCK_NOSHARED_MUTEX_VAL_UNLOCKED),
	mergeidmap_lockval(FLCK_NOSHARED_MUTEX_VAL_UNLOCKED), is_server_mode(false), max_sock_pool(ChmEventSock::DEFAULT_MAX_SOCK_POOL),
	sock_pool_timeout(ChmEventSock::DEFAULT_SOCK_POOL_TIMEOUT), sock_retry_count(ChmEventSock::DEFAULT_RETRYCNT),
	con_retry_count(ChmEventSock::DEFAULT_RETRYCNT_CONNECT), sock_wait_time(ChmEventSock::DEFAULT_WAIT_SOCKET),
	con_wait_time(ChmEventSock::DEFAULT_WAIT_CONNECT)
{
	assert(pChmCntrl);

	// first initialize cache value and threads
	//
	// [NOTE]
	// ChmEventSock must be initialized after initialize ChmIMData
	//
	if(!UpdateInternalData()){
		ERR_CHMPRN("Could not initialize cache data for ImData, but continue...");
	}
}

ChmEventSock::~ChmEventSock()
{
	// clear all
	Clean();
}

bool ChmEventSock::Clean(void)
{
	if(IsEmpty()){
		return true;
	}
	// exit merge thread
	is_run_merge = false;
	if(!mergethread.ExitAllThreads()){
		ERR_CHMPRN("Failed to exit thread for merging.");
	}
	// exit processing thread
	if(!procthreads.ExitAllThreads()){
		ERR_CHMPRN("Failed to exit thread for processing.");
	}
	// close sockets
	if(!CloseSelfSocks()){
		ERR_CHMPRN("Failed to close self sock and ctlsock, but continue...");
	}
	if(!CloseFromSlavesSocks()){
		ERR_CHMPRN("Failed to close connection from slaves, but continue...");
	}
	if(!CloseToServersSocks()){
		ERR_CHMPRN("Failed to close connection to other servers, but continue...");
	}
	if(!CloseCtlportClientSocks()){
		ERR_CHMPRN("Failed to close control port connection from clients, but continue...");
	}
	if(!CloseFromSlavesAcceptingSocks()){
		ERR_CHMPRN("Failed to close accepting connection from slaves, but continue...");
	}
	// Here, there are no SSL connection, but check it.
	CloseAllSSL();

	// SSL contexts
	if(svr_sslctx){
		SSL_CTX_free(svr_sslctx);
		svr_sslctx = NULL;
	}
	if(slv_sslctx){
		SSL_CTX_free(slv_sslctx);
		slv_sslctx = NULL;
	}

	// all socket lock are freed
	sendlockmap.clear();		// sendlockmap does not have default cb function & not call cb here --> do nothing

	return ChmEventBase::Clean();
}

//
// initialize/reinitialize cache value and threads
//
bool ChmEventSock::UpdateInternalData(void)
{
	ChmIMData*	pImData;
	if(!pChmCntrl || NULL == (pImData = pChmCntrl->GetImDataObj())){
		ERR_CHMPRN("Object is not pre-initialized.");
		return false;
	}

	//
	// If random mode, set not do merging and automatically merge start.
	// If hash(not random) mode, set do merge and manually merge start.
	//
	// [NOTE] BE CAREFUL
	// In configuration the auto merge flag at random mode MUST be false,
	// but this class needs the flag is true at random mode.
	// So convert flag here!
	//
	bool	tmp_is_auto_merge	= pImData->IsRandomDeliver() ? true : pImData->IsAutoMergeConf();
	bool	tmp_is_do_merge		= pImData->IsRandomDeliver() ? false : pImData->IsDoMergeConf();

	// merge thread
	if(!tmp_is_do_merge && is_run_merge){
		// now work to merge, but we do do not stop it.
		WAN_CHMPRN("Re-initialize by configration. new do_merge mode is false, but mergeing now. be careful about merging.");
	}
	if(tmp_is_do_merge && !mergethread.HasThread()){
		// do_merge false to true, we need to run merge thread.
		//
		//	- parameter is this object
		//	- sleep at starting
		//	- not at onece(not one shot)
		//	- sleep after every working
		//	- keep event count
		//
		if(!mergethread.CreateThreads(1, ChmEventSock::MergeWorkerFunc, NULL, this, 0, true, false, false, true)){
			ERR_CHMPRN("Failed to create thread for merging on socket, but continue...");
		}else{
			MSG_CHMPRN("start to run merge thread on socket.");
		}
	}else{
		// [NOTE]
		// if new do_merge flag is false and already run thread, but we do not stop it.
	}
	is_auto_merge	 	= tmp_is_auto_merge;
	is_do_merge		 	= tmp_is_do_merge;

	// processing thread
	int	conf_thread_cnt	= pImData->GetSocketThreadCount();		// we do not need to cache this value.
	int	now_thread_cnt	= procthreads.GetThreadCount();
	if(conf_thread_cnt < now_thread_cnt){
		if(DEFAULT_SOCK_THREAD_CNT == conf_thread_cnt){
			// stop all
			if(!procthreads.ExitAllThreads()){
				ERR_CHMPRN("Failed to exit thread for sock processing.");
			}else{
				MSG_CHMPRN("stop all sock processing thread(%d).", now_thread_cnt);
			}
		}else{
			// need to stop some thread
			if(!procthreads.ExitThreads(now_thread_cnt - conf_thread_cnt)){
				ERR_CHMPRN("Failed to exit thread for sock processing.");
			}else{
				MSG_CHMPRN("stop sock processing thread(%d - %d = %d).", now_thread_cnt, conf_thread_cnt, now_thread_cnt - conf_thread_cnt);
			}
		}
	}else if(now_thread_cnt < conf_thread_cnt){
		// need to run new threads
		//
		//	- parameter is NULL(because thread is sleep at start)
		//	- sleep at starting
		//	- not at onece(not one shot)
		//	- sleep after every working
		//	- not keep event count
		//
		if(!procthreads.CreateThreads(conf_thread_cnt - now_thread_cnt, ChmEventSock::ReceiveWorkerProc, NULL, this, 0, true, false, false, false)){
			ERR_CHMPRN("Failed to create thread for sock processing, but continue...");
		}else{
			MSG_CHMPRN("start to run sock processing thread(%d + %d = %d).", now_thread_cnt, conf_thread_cnt - now_thread_cnt, conf_thread_cnt);
		}
	}else{
		// nothing to do because of same thread count
	}

	// others
	int			tmp_sock_retry_count= pImData->GetSockRetryCnt();
	suseconds_t	tmp_sock_wait_time	= pImData->GetSockTimeout();
	suseconds_t tmp_con_wait_time	= pImData->GetConnectTimeout();

	sock_retry_count	= (CHMEVENTSOCK_RETRY_DEFAULT == tmp_sock_retry_count ? ChmEventSock::DEFAULT_RETRYCNT : tmp_sock_retry_count);
	sock_wait_time		= (CHMEVENTSOCK_TIMEOUT_DEFAULT == tmp_sock_wait_time ? ChmEventSock::DEFAULT_WAIT_SOCKET : tmp_sock_wait_time);
	con_wait_time		= (CHMEVENTSOCK_TIMEOUT_DEFAULT == tmp_con_wait_time ? ChmEventSock::DEFAULT_WAIT_CONNECT : tmp_con_wait_time);
	con_retry_count		= sock_retry_count;
	is_server_mode		= pImData->IsServerMode();
	max_sock_pool		= pImData->GetMaxSockPool();
	sock_pool_timeout	= pImData->GetSockPoolTimeout();

	return true;
}

bool ChmEventSock::GetEventQueueFds(event_fds_t& fds)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	fds.clear();

	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();
	int			sock	= CHM_INVALID_SOCK;
	int			ctlsock	= CHM_INVALID_SOCK;
	if(!pImData->GetSelfSocks(sock, ctlsock)){
		ERR_CHMPRN("Could not get self sock and ctlsock.");
		return false;
	}
	if(CHM_INVALID_SOCK != sock){
		fds.push_back(sock);
	}
	if(CHM_INVALID_SOCK != ctlsock){
		fds.push_back(ctlsock);
	}
	seversockmap.get_keys(fds);
	slavesockmap.get_keys(fds);
	ctlsockmap.get_keys(fds);
	acceptingmap.get_keys(fds);
	return true;
}

bool ChmEventSock::SetEventQueue(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// first check for SSL
	if(!IsSafeParamsForSSL()){
		ERR_CHMPRN("Something wrong about SSL parameters, thus could not make SSL context.");
		return false;
	}

	// all status update.
	if(!InitialAllServerStatus()){
		ERR_CHMPRN("Could not update all server status.");
		return false;
	}

	// Get self port
	short	port	= CHM_INVALID_PORT;
	short	ctlport	= CHM_INVALID_PORT;
	if(!pImData->GetSelfPorts(port, ctlport) || CHM_INVALID_PORT == ctlport || (is_server_mode && CHM_INVALID_PORT == port)){
		ERR_CHMPRN("Could not get self port and ctlport.");
		return false;
	}

	struct epoll_event	eqevent;
	int		sock	= CHM_INVALID_SOCK;
	int		ctlsock	= CHM_INVALID_SOCK;

	// on only server mode, listen port
	if(is_server_mode){
		// listen port
		if(CHM_INVALID_SOCK == (sock = ChmEventSock::Listen(NULL, port))){
			ERR_CHMPRN("Could not listen port(%d).", port);
			return false;
		}
		// add event fd
		memset(&eqevent, 0, sizeof(struct epoll_event));
		eqevent.data.fd	= sock;
		eqevent.events	= EPOLLIN | EPOLLET | EPOLLRDHUP;			// EPOLLRDHUP is set
		if(-1 == epoll_ctl(eqfd, EPOLL_CTL_ADD, sock, &eqevent)){
			ERR_CHMPRN("Failed to add sock(port %d: sock %d) into epoll event(%d), errno=%d", port, sock, eqfd, errno);
			CHM_CLOSESOCK(sock);
			return false;
		}
	}

	// listen ctlport
	if(CHM_INVALID_SOCK == (ctlsock = ChmEventSock::Listen(NULL, ctlport))){
		ERR_CHMPRN("Could not listen ctlport(%d).", ctlport);
		if(CHM_INVALID_SOCK != sock){
			epoll_ctl(eqfd, EPOLL_CTL_DEL, sock, NULL);
		}
		CHM_CLOSESOCK(sock);
		return false;
	}

	// add event fd
	memset(&eqevent, 0, sizeof(struct epoll_event));
	eqevent.data.fd	= ctlsock;
	eqevent.events	= EPOLLIN | EPOLLET | EPOLLRDHUP;				// EPOLLRDHUP is set.
	if(-1 == epoll_ctl(eqfd, EPOLL_CTL_ADD, ctlsock, &eqevent)){
		ERR_CHMPRN("Failed to add sock(port %d: sock %d) into epoll event(%d), errno=%d", ctlport, ctlsock, eqfd, errno);
		if(CHM_INVALID_SOCK != sock){
			epoll_ctl(eqfd, EPOLL_CTL_DEL, sock, NULL);
		}
		CHM_CLOSESOCK(sock);
		CHM_CLOSESOCK(ctlsock);
		return false;
	}

	// set chmshm
	if(!pImData->SetSelfSocks(sock, ctlsock)){
		ERR_CHMPRN("Could not set self sock(%d) and ctlsock(%d) into chmshm.", sock, ctlsock);
		if(CHM_INVALID_SOCK != sock){
			epoll_ctl(eqfd, EPOLL_CTL_DEL, sock, NULL);
		}
		if(CHM_INVALID_SOCK != ctlsock){
			epoll_ctl(eqfd, EPOLL_CTL_DEL, ctlsock, NULL);
		}
		CHM_CLOSESOCK(sock);
		CHM_CLOSESOCK(ctlsock);
		return false;
	}

	// check self status
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(is_server_mode){
		if(CHMPXSTS_SRVOUT_DOWN_NORMAL == status){
			// normally default come here.
			MSG_CHMPRN("initial chmpx status(0x%016" PRIx64 ":%s) is wrong, so force to set CHMPXSTS_SRVOUT_UP_NORMAL.", status, STR_CHMPXSTS_FULL(status).c_str());
			status = CHMPXSTS_SRVOUT_UP_NORMAL;

		}else if(CHMPXSTS_SRVIN_DOWN_NORMAL == status){
			WAN_CHMPRN("initial chmpx status(0x%016" PRIx64 ":%s) is wrong, so force to set CHMPXSTS_SRVIN_UP_MERGING.", status, STR_CHMPXSTS_FULL(status).c_str());
			status = CHMPXSTS_SRVIN_UP_MERGING;

		}else if(CHMPXSTS_SRVIN_DOWN_DELPENDING == status){
			WAN_CHMPRN("initial chmpx status(0x%016" PRIx64 ":%s) is wrong, so force to set CHMPXSTS_SRVIN_UP_MERGING.", status, STR_CHMPXSTS_FULL(status).c_str());
			status = CHMPXSTS_SRVIN_UP_MERGING;		// should be CHMPXSTS_SRVIN_UP_NORMAL ?

		}else if(CHMPXSTS_SRVIN_DOWN_DELETED == status){
			WAN_CHMPRN("initial chmpx status(0x%016" PRIx64 ":%s) is wrong, so force to set CHMPXSTS_SRVOUT_UP_NORMAL.", status, STR_CHMPXSTS_FULL(status).c_str());
			status = CHMPXSTS_SRVIN_UP_DELETED;

		}else{
			// nothing to change status
		}

	}else{
		WAN_CHMPRN("initial chmpx status(0x%016" PRIx64 ":%s) is slave, so force to set CHMPXSTS_SRVOUT_UP_NORMAL.", status, STR_CHMPXSTS_FULL(status).c_str());
		status = CHMPXSTS_SLAVE_UP_NORMAL;
	}

	// re-set self status(set suspend in this method)
	if(!pImData->SetSelfStatus(status)){
		CloseSelfSocks();
		return false;
	}

	// Connect(join ring/connect servers)
	if(is_server_mode){
		// server mode
		if(!ConnectRing()){
			ERR_CHMPRN("Failed to join RING(mode is server as SERVICE OUT/IN).");
			return false;
		}
	}else{
		// slave mode
		if(!ConnectServers()){
			ERR_CHMPRN("Failed to connect servers on RING(mode is SLAVE).");
			return false;
		}
	}
	return true;
}

bool ChmEventSock::UnsetEventQueue(void)
{
	return Clean();
}

bool ChmEventSock::IsEventQueueFd(int fd)
{
	if(CHM_INVALID_HANDLE == fd){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}

	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();
	int			sock	= CHM_INVALID_SOCK;
	int			ctlsock	= CHM_INVALID_SOCK;
	if(!pImData->GetSelfSocks(sock, ctlsock)){
		ERR_CHMPRN("Could not get self sock and ctlsock, but continue...");
	}else{
		if(fd == sock || fd == ctlsock){
			return true;
		}
	}
	if(seversockmap.find(fd) || slavesockmap.find(fd) || ctlsockmap.find(fd) || acceptingmap.find(fd)){
		return true;
	}
	return false;
}

//------------------------------------------------------
// SSL context
//------------------------------------------------------
SSL_CTX* ChmEventSock::GetSSLContext(const char* CApath, const char* CAfile, const char* server_cert, const char* server_prikey, const char* slave_cert, const char* slave_prikey, bool is_verify_peer)
{
	if(!CHMEMPTYSTR(CApath) && !CHMEMPTYSTR(CAfile)){
		ERR_CHMPRN("Parameter is wrong.");
		return NULL;
	}

	// Which ctx is needed?
	SSL_CTX**	ppctx = NULL;
	if(!CHMEMPTYSTR(server_cert) && !CHMEMPTYSTR(server_prikey)){
		ppctx = &svr_sslctx;
	}else{
		ppctx = &slv_sslctx;
	}

	if(!*ppctx){
		// Make context
		if(NULL == (*ppctx = ChmEventSock::MakeSSLContext(CVT_ESTR_NULL(CApath), CVT_ESTR_NULL(CAfile), CVT_ESTR_NULL(server_cert), CVT_ESTR_NULL(server_prikey), CVT_ESTR_NULL(slave_cert), CVT_ESTR_NULL(slave_prikey), is_verify_peer))){
			ERR_CHMPRN("Failed to make SSL context.");
		}
	}else{
		// already has ctx.
	}
	return *ppctx;
}

SSL* ChmEventSock::GetSSL(int sock)
{
	if(IsEmpty()){
		return NULL;
	}
	if(!sslmap.find(sock)){
		return NULL;
	}
	return sslmap[sock];
}

// [NOTE]
// This method is checking whichever the parameters for SSL(CA, cert, keys, etc) is no problem.
// So that, this method makes SSL context when SSL mode.
// If could not make SSL context, it means that those parameter are wrong.
//
bool ChmEventSock::IsSafeParamsForSSL(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();

	CHMPXSSL	ssldata;
	if(!pImData->GetSelfSsl(ssldata)){
		ERR_CHMPRN("Failed to get SSL structure from self chmpx.");
		return false;
	}
	if(!CHMEMPTYSTR(ssldata.server_cert)){
		if(NULL == ChmEventSock::GetSSLContext((!ssldata.is_ca_file ? ssldata.capath : NULL), (ssldata.is_ca_file ? ssldata.capath : NULL), ssldata.server_cert, ssldata.server_prikey, ssldata.slave_cert, ssldata.slave_prikey, ssldata.verify_peer)){
			ERR_CHMPRN("Failed to make self SSL context for server socket.");
			return false;
		}
	}
	if(!CHMEMPTYSTR(ssldata.slave_cert)){
		if(NULL == ChmEventSock::GetSSLContext((!ssldata.is_ca_file ? ssldata.capath : NULL), (ssldata.is_ca_file ? ssldata.capath : NULL), NULL, NULL, ssldata.slave_cert, ssldata.slave_prikey, ssldata.verify_peer)){
			ERR_CHMPRN("Failed to make self SSL context for slave socket.");
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------
// Lock for socket(for server/slave node)
//---------------------------------------------------------
// [NOTE]
// This method uses and manages socket lock mapping.
// Because this library supports multi-thread for sending/recieving, this class needs to
// lock socket before sending. (but the control socket does not be needed to lock.)
//
// [NOTICE]
// For locking, we use sendlockmap_t class(template), and use directly lock variable in
// sendlockmap_t here.
//
bool ChmEventSock::RawLockSendSock(int sock, bool is_lock, bool is_onece)
{
	if(CHM_INVALID_SOCK == sock){
		return false;
	}
	pid_t	tid		= gettid();
	bool	result	= false;
	for(bool is_loop = true; !result && is_loop; is_loop = !is_onece){
		// get trylock
		if(!fullock::flck_trylock_noshared_mutex(&sendlockmap.lockval)){
			// [NOTE]
			// no wait and not sched_yield
			continue;
		}

		sendlockmap_t::iterator	iter = sendlockmap.basemap.find(sock);
		if(sendlockmap.basemap.end() == iter || NULL == iter->second){
			// there is no sock in map(or invalid pointer), so set new status data to map
			PCHMSSSTAT	pssstat			= new CHMSSSTAT;
			pssstat->tid				= is_lock ? tid : CHM_INVALID_TID;
			pssstat->last_time			= time(NULL);
			sendlockmap.basemap[sock]	= pssstat;
			result = true;
		}else{
			// found sock status in map
			PCHMSSSTAT	pssstat = iter->second;
			if(is_lock){
				if(CHM_INVALID_TID == pssstat->tid){
					// sock is unlocked.
					pssstat->tid = tid;
					result = true;
				}else if(tid == pssstat->tid){
					// same thread locked.
					result = true;
				}else{
					// other thread is locked, so looping
				}
			}else{
				if(CHM_INVALID_TID == pssstat->tid){
					// already unlocked
				}else{
					if(tid != pssstat->tid){
						MSG_CHMPRN("thread(%d) unlocked sock(%d) locked by thread(%d).", tid, sock, pssstat->tid);
					}
					pssstat->tid		= CHM_INVALID_TID;
					pssstat->last_time	= time(NULL);			// last_time is updated only at closing!
				}
				result = true;
			}
		}
 	 	fullock::flck_unlock_noshared_mutex(&sendlockmap.lockval);		// UNLOCK
	}
	return result;
}

//---------------------------------------------------------
// Methods for Communication base
//---------------------------------------------------------
//
// Lapped RawSend method with LockSendSock
//
bool ChmEventSock::LockedSend(int sock, SSL* ssl, PCOMPKT pComPkt, bool is_blocking)
{
	LockSendSock(sock);			// LOCK SOCKET

	bool	is_closed = false;
	if(!ChmEventSock::RawSend(sock, ssl, pComPkt, is_closed, is_blocking, sock_retry_count, sock_wait_time)){
		ERR_CHMPRN("Failed to send COMPKT to sock(%d).", sock);
		UnlockSendSock(sock);	// UNLOCK SOCKET
		if(!is_closed){
			CloseSSL(sock);		// close socket
		}
		return false;
	}
	UnlockSendSock(sock);		// UNLOCK SOCKET

	return true;
}

//
// Lapped RawSend method with LockSendSock
//
bool ChmEventSock::LockedSend(int sock, SSL* ssl, const unsigned char* pbydata, size_t length, bool is_blocking)
{
	LockSendSock(sock);			// LOCK SOCKET

	bool	is_closed = false;
	if(!ChmEventSock::RawSend(sock, ssl, pbydata, length, is_closed, is_blocking, sock_retry_count, sock_wait_time)){
		ERR_CHMPRN("Failed to send binary data to sock(%d).", sock);
		UnlockSendSock(sock);	// UNLOCK SOCKET
		if(!is_closed){
			CloseSSL(sock);		// close socket
		}
		return false;
	}
	UnlockSendSock(sock);		// UNLOCK SOCKET

	return true;
}

//
// [NOTE]
// We check socket count in each server here, and if it is over socket pool count,
// this method tries to close it.
// This logic is valid only when server socket.
//
// When this method opens/closes a socket, this locks dyna_sockfd_lockval every time.
// If dyna_sockfd_lockval is already locked, this does not check over max pool count
// and not open new socket, and continue to loop.
//
bool ChmEventSock::GetLockedSendSock(chmpxid_t chmpxid, int& sock, bool is_check_slave)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData		= pChmCntrl->GetImDataObj();
	bool		is_server	= pImData->IsServerChmpxId(chmpxid);

	if(!is_server && !is_check_slave){
		WAN_CHMPRN("Could not find chmpxid(0x%016" PRIx64 ") sock in slave list.", chmpxid);
		return false;
	}
	socklist_t	socklist;

	// check max socket pool (if over max sock pool count, close idle sockets which is over time limit)
	if(is_server && (last_check_time + sock_pool_timeout) < time(NULL)){
		// get socket list to server
		if(pImData->GetServerSock(chmpxid, socklist)){
			// check max socket pool (if over max sock pool count, close idle sockets which is over time limit)
			if(max_sock_pool < static_cast<int>(socklist.size())){
				if(fullock::flck_trylock_noshared_mutex(&dyna_sockfd_lockval)){			// LOCK(dynamic)
					for(socklist_t::reverse_iterator riter = socklist.rbegin(); riter != socklist.rend(); ){
						if(TryLockSendSock(*riter)){									// try to lock socket
							// Succeed to lock socket, so close this.
							seversockmap.erase(*riter);									// close socket
							sendlockmap.erase(*riter);									// remove socket from send lock map(with unlock it)

							// remove socket from socklist
							if((++riter) != socklist.rend()){
								socklist.erase((riter).base());
							}else{
								socklist.erase(socklist.begin());
								break;	// over the start position.
							}

							// check over max pool count
							if(static_cast<int>(socklist.size()) <= max_sock_pool){
								break;
							}
						}else{
							++riter;
						}
					}
					fullock::flck_unlock_noshared_mutex(&dyna_sockfd_lockval);			// UNLOCK(dynamic)

					last_check_time = time(NULL);
				}
			}
		}
	}

	// loop
	size_t	slv_sockcnt	= 0;
	for(sock = CHM_INVALID_SOCK; CHM_INVALID_SOCK == sock; ){
		slv_sockcnt	= 0;

		// at first, server socket
		if(is_server){
			// get socket list to server
			socklist.clear();
			if(!pImData->GetServerSock(chmpxid, socklist)){
				socklist.clear();
			}

			// try to lock existed socket
			for(socklist_t::const_iterator iter = socklist.begin(); iter != socklist.end(); ++iter){
				if(TryLockSendSock(*iter)){												// try to lock socket
					// Succeed to lock socket!
					sock = *iter;
					break;
				}
			}

			// check for closing another idle socket which is over time limit.
			//
			// [NOTE]
			// interval for checking is 5 times by sock_pool_timeout.
			// already locked one socket(sock), so minimum socket count is guaranteed)
			//
			if(CHM_INVALID_SOCK != sock && ChmEventSock::NO_SOCK_POOL_TIMEOUT < sock_pool_timeout && (last_check_time + (sock_pool_timeout * 5) < time(NULL))){
				if(fullock::flck_trylock_noshared_mutex(&dyna_sockfd_lockval)){			// LOCK(dynamic)
					time_t	nowtime = time(NULL);

					for(socklist_t::const_iterator iter = socklist.begin(); iter != socklist.end(); ++iter){
						if(sock == *iter){
							continue;	// skip
						}
						if(TryLockSendSock(*iter)){										// try to lock socket
							// get locked sock status.
							const PCHMSSSTAT	pssstat = sendlockmap.get(*iter);
							if(pssstat && (pssstat->last_time + sock_pool_timeout) < nowtime){
								// timeouted socket.
								seversockmap.erase(*iter);								// close socket
								sendlockmap.erase(*iter);								// remove socket from send lock map(with unlock it)

								// [NOTE]
								// we do not remove socket(iter) from socklist, because this loop will be broken soon.
								//
							}else{
								UnlockSendSock(*iter);									// unlock socket
							}
						}
					}
					fullock::flck_unlock_noshared_mutex(&dyna_sockfd_lockval);			// UNLOCK(dynamic)
				}
			}
		}

		// search socket in list of slave ( for a case of other server connect to server as slave )
		if(CHM_INVALID_SOCK == sock && is_check_slave){
			socklist.clear();
			if(!pImData->GetSlaveSock(chmpxid, socklist) || socklist.empty()){
				slv_sockcnt	= 0;
				if(!is_server){
					// there is no server, so no more checking.
					WAN_CHMPRN("Could not find chmpxid(0x%016" PRIx64 ") sock in slave list.", chmpxid);
					return false;
				}
			}else{
				slv_sockcnt	= socklist.size();

				// slave has socket list. next, search unlocked socket.
				bool	need_wait_unlock = false;
				for(socklist_t::const_iterator iter = socklist.begin(); iter != socklist.end(); ++iter){
					int	tmpsock = *iter;
					if(CHM_INVALID_SOCK != tmpsock){
						need_wait_unlock = true;
						if(TryLockSendSock(tmpsock)){									// try to lock socket
							// Succeed to lock socket!
							sock = tmpsock;
							break;
						}
					}
				}
				if(!is_server && !need_wait_unlock){
					// there is no server, so no more checking.
					WAN_CHMPRN("Could not find chmpxid(0x%016" PRIx64 ") sock in slave list.", chmpxid);
					return false;
				}
			}
		}

		// try to connect new socket ( there is no (idle) socket )
		if(CHM_INVALID_SOCK == sock && is_server){
			// reget socket list, and check socket pool count
			socklist.clear();
			if(pImData->GetServerSock(chmpxid, socklist) && static_cast<int>(socklist.size()) < max_sock_pool){
				if(fullock::flck_trylock_noshared_mutex(&dyna_sockfd_lockval)){			// LOCK(dynamic)
					// can connect new sock
					if(!RawConnectServer(chmpxid, sock, false, true) && CHM_INVALID_SOCK == sock){

						if(0 == socklist.size() && 0 == slv_sockcnt){
							// no server socket and could not connect, and no slave socket, so no more try.
							WAN_CHMPRN("Could not connect to server chmpxid(0x%016" PRIx64 ").", chmpxid);
							fullock::flck_unlock_noshared_mutex(&dyna_sockfd_lockval);	// UNLOCK(dynamic)
							return false;
						}
						MSG_CHMPRN("Could not connect to server chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
					}else{
						// Succeed to connect new sock and lock it.
					}
					fullock::flck_unlock_noshared_mutex(&dyna_sockfd_lockval);			// UNLOCK(dynamic)
				}
			}
		}
	}
	return true;
}

//
// If you make all COMPKT before calling this method, you can set NULL
// to pbody. If pbody is not NULL, this method builds COMPKT with body data.
//
bool ChmEventSock::Send(PCOMPKT pComPkt, const unsigned char* pbody, size_t blength)
{
	if(!pComPkt){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();

	// for stat
	bool			is_measure 	= (COM_C2C == pComPkt->head.type);
	chmpxid_t		term_chmpxid= pComPkt->head.term_ids.chmpxid;
	struct timespec	start_time;
	if(is_measure){
		STR_MATE_TIMESPEC(&start_time);
	}

	// check & build packet
	PCOMPKT	pPacked = pComPkt;
	bool	is_pack = false;
	if(pbody && 0L < blength){
		if(NULL == (pPacked = reinterpret_cast<PCOMPKT>(malloc(sizeof(COMPKT) + blength)))){
			ERR_CHMPRN("Could not allocate memory as %zd length.", sizeof(COMPKT) + blength);
			return false;
		}
		COPY_COMHEAD(&(pPacked->head), &(pComPkt->head));
		pPacked->length	= sizeof(COMPKT) + blength;
		pPacked->offset	= sizeof(COMPKT);

		unsigned char*	pdest = reinterpret_cast<unsigned char*>(pPacked) + sizeof(COMPKT);
		memcpy(pdest, pbody, blength);

		is_pack = true;
	}
	// for stat
	size_t	stat_length = pPacked->length;

	// get socket
	int	sock = CHM_INVALID_SOCK;
	if(!GetLockedSendSock(pComPkt->head.term_ids.chmpxid, sock, true) || CHM_INVALID_SOCK == sock){		// LOCK SOCKET
		WAN_CHMPRN("Could not find chmpxid(0x%016" PRIx64 ") sock.", pComPkt->head.term_ids.chmpxid);
		if(is_pack){
			CHM_Free(pPacked);
		}
		return false;
	}

	// send
	bool	is_closed = false;
	if(!ChmEventSock::RawSend(sock, GetSSL(sock), pPacked, is_closed, false, sock_retry_count, sock_wait_time)){
		ERR_CHMPRN("Failed to send COMPKT to sock(%d).", sock);
		UnlockSendSock(sock);			// UNLOCK SOCKET

		if(!is_closed){
			CloseSSL(sock);				// close socket
		}
		if(is_pack){
			CHM_Free(pPacked);
		}
		return false;
	}
	UnlockSendSock(sock);				// UNLOCK SOCKET

	// stat
	if(is_measure){
		struct timespec	fin_time;
		struct timespec	elapsed_time;
		FIN_MATE_TIMESPEC2(&start_time, &fin_time, &elapsed_time);

		if(!pImData->AddStat(term_chmpxid, true, stat_length, elapsed_time)){
			ERR_CHMPRN("Failed to update stat(send).");
		}
		if(pImData->IsTraceEnable() && !pImData->AddTrace(CHMLOG_TYPE_OUT_SOCKET, stat_length, start_time, fin_time)){
			ERR_CHMPRN("Failed to add trace log.");
		}
	}

	if(is_pack){
		CHM_Free(pPacked);
	}
	return true;
}

bool ChmEventSock::Receive(int fd)
{
	if(CHM_INVALID_HANDLE == fd){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}

	bool	result = false;
	if(procthreads.HasThread()){
		// Has processing threads, so run(do work) processing thread.
		chmthparam_t	wp_param = static_cast<chmthparam_t>(fd);

		if(false == (result = procthreads.DoWorkThread(wp_param))){
			ERR_CHMPRN("Failed to wake up thread for processing(receiving).");
		}
	}else{
		// Does not have processing thread, so call directly.
		//
		// [NOTE]
		// We use edge triggerd epoll, in this type the event is accumulated and not sent when the data to
		// the socket is left. Thus we must read more until EAGAIN. Otherwise we lost data.
		// So we checked socket for rest data here.
		//
		bool		is_closed;
		int			rtcode;
		suseconds_t	waittime = sock_wait_time;
		do{
			is_closed = false;
			if(false == (result = RawReceive(fd, is_closed))){
				if(!is_closed){
					ERR_CHMPRN("Failed to receive processing directly.");
				}else{
					MSG_CHMPRN("sock(%d) is closed while processing directly.", fd);
					result = true;
				}
				break;
			}
		}while(0 == (rtcode = ChmEventSock::WaitForReady(fd, WAIT_READ_FD, 0, false, waittime)));		// check rest data & return assap

		if(0 != rtcode && ETIMEDOUT != rtcode){
			if(RawNotifyHup(fd)){
				ERR_CHMPRN("Failed to closing socket(%d), but continue...", fd);
			}
			result = true;
		}
	}
	return result;
}

//
// If fd is sock, it means connecting from other servers.
// After receiving this method is processing own work. And if it needs to dispatch
// processing event after own processing, do it by calling control object.
//
// [NOTE]
// This method calls RawReceiveByte and RawReceiveAny methods, these methods read only packet size
// from socket. We use edge triggerd epoll, thus we must read more until EAGAIN. Otherwise we
// lost data. So we checked rest data in caller method.
//
bool ChmEventSock::RawReceive(int fd, bool& is_closed)
{
	is_closed = false;

	if(CHM_INVALID_HANDLE == fd){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}

	PCOMPKT		pComPkt = NULL;
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();
	int			sock	= CHM_INVALID_SOCK;
	int			ctlsock	= CHM_INVALID_SOCK;
	if(!pImData->GetSelfSocks(sock, ctlsock)){
		ERR_CHMPRN("Could not get self sock and ctlsock, but continue...");
	}

	// Processing
	if(fd == sock){
		// self port -> new connection
		if(!Accept(sock)){
			ERR_CHMPRN("Could not accept new connection.");
			return false;
		}

	}else if(fd == ctlsock){
		// self ctlport
		if(!AcceptCtlport(ctlsock)){
			ERR_CHMPRN("Could not accept new control port connection.");
			return false;
		}

	}else if(seversockmap.find(fd)){
		// server chmpx

		// for stat
		struct timespec	start_time;
		STR_MATE_TIMESPEC(&start_time);

		if(!ChmEventSock::RawReceive(fd, GetSSL(fd), is_closed, &pComPkt, 0L, false, sock_retry_count, sock_wait_time) || !pComPkt){
			CHM_Free(pComPkt);

			if(!is_closed){
				ERR_CHMPRN("Failed to receive ComPkt from sock(%d), sock is not closed.", fd);
			}else{
				MSG_CHMPRN("Failed to receive ComPkt from sock(%d), sock is closed.", fd);
				// close socket
				chmpxid_t	chmpxid	= seversockmap[fd];
				if(!RawNotifyHup(fd)){
					ERR_CHMPRN("Failed to closing \"to server socket\" for chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
				}
			}
			return false;
		}
		// stat
		if(COM_C2C == pComPkt->head.type){
			struct timespec	fin_time;
			struct timespec	elapsed_time;
			FIN_MATE_TIMESPEC2(&start_time, &fin_time, &elapsed_time);

			if(!pImData->AddStat(pComPkt->head.dept_ids.chmpxid, false, pComPkt->length, elapsed_time)){
				ERR_CHMPRN("Failed to update stat(receive).");
			}
			if(pImData->IsTraceEnable() && !pImData->AddTrace(CHMLOG_TYPE_IN_SOCKET, pComPkt->length, start_time, fin_time)){
				ERR_CHMPRN("Failed to add trace log.");
			}
		}

	}else if(slavesockmap.find(fd)){
		// slave chmpx

		// for stat
		struct timespec	start_time;
		STR_MATE_TIMESPEC(&start_time);

		if(!ChmEventSock::RawReceive(fd, GetSSL(fd), is_closed, &pComPkt, 0L, false, sock_retry_count, sock_wait_time) || !pComPkt){
			CHM_Free(pComPkt);

			if(!is_closed){
				ERR_CHMPRN("Failed to receive ComPkt from sock(%d), sock is not closed.", fd);
			}else{
				MSG_CHMPRN("Failed to receive ComPkt from sock(%d), sock is closed.", fd);
				// close socket
				chmpxid_t	chmpxid	= slavesockmap[fd];
				if(!RawNotifyHup(fd)){
					ERR_CHMPRN("Failed to closing \"from slave socket\" for chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
				}
			}
			return false;
		}
		// stat
		if(COM_C2C == pComPkt->head.type){
			struct timespec	fin_time;
			struct timespec	elapsed_time;
			FIN_MATE_TIMESPEC2(&start_time, &fin_time, &elapsed_time);

			if(!pImData->AddStat(pComPkt->head.dept_ids.chmpxid, false, pComPkt->length, elapsed_time)){
				ERR_CHMPRN("Failed to update stat(receive).");
			}
			if(pImData->IsTraceEnable() && !pImData->AddTrace(CHMLOG_TYPE_IN_SOCKET, pComPkt->length, start_time, fin_time)){
				ERR_CHMPRN("Failed to add trace log.");
			}
		}

	}else if(acceptingmap.find(fd)){
		// slave socket before accepting
		string	achname = acceptingmap[fd];

		if(!ChmEventSock::RawReceive(fd, GetSSL(fd), is_closed, &pComPkt, 0L, false, sock_retry_count, sock_wait_time) || !pComPkt){
			CHM_Free(pComPkt);

			if(!is_closed){
				ERR_CHMPRN("Failed to receive ComPkt from accepting sock(%d), sock is not closed.", fd);
			}else{
				// remove mapping
				MSG_CHMPRN("Failed to receive ComPkt from accepting sock(%d), sock is closed.", fd);
				if(!RawNotifyHup(fd)){
					ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", achname.c_str());
				}
			}
			return false;
		}

		//
		// Accepting logic needs socket, so we processing here.
		//

		// check COMPKT(length, type, offset)
		if(COM_PX2PX != pComPkt->head.type || (sizeof(COMPKT) + sizeof(PXCOM_CONINIT_REQ)) != pComPkt->length || sizeof(COMPKT) != pComPkt->offset){
			ERR_CHMPRN("ComPkt is wrong for accepting, type(%" PRIu64 ":%s), length(%zu), offset(%jd).", pComPkt->head.type, STRCOMTYPE(pComPkt->head.type), pComPkt->length, static_cast<intmax_t>(pComPkt->offset));
			CHM_Free(pComPkt);
			return false;
		}
		// check CONINIT_REQ
		PPXCOM_ALL	pComAll = CHM_OFFSET(pComPkt, pComPkt->offset, PPXCOM_ALL);
		if(CHMPX_COM_CONINIT_REQ != pComAll->val_head.type){
			ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s), but allow only CHMPX_COM_CONINIT_REQ.", pComAll->val_head.type, STRPXCOMTYPE(pComAll->val_head.type));
			CHM_Free(pComPkt);
			return false;
		}

		// Parse
		PCOMPKT		pResComPkt	= NULL;	// dummy
		chmpxid_t	slvchmpxid	= CHM_INVALID_CHMPXID;
		short		ctlport		= CHM_INVALID_PORT;
		PCHMPXSSL	pssl		= NULL;
		if(!PxComReceiveConinitReq(&(pComPkt->head), pComAll, &pResComPkt, slvchmpxid, ctlport)){
			ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pComAll->val_head.type, STRPXCOMTYPE(pComAll->val_head.type));
			CHM_Free(pComPkt);
			return false;
		}
		CHM_Free(pComPkt);

		// check ACL(with port)
		if(!pImData->IsAllowHostname(achname.c_str(), &ctlport, &pssl)){
			// Not allow accessing from slave.
			ERR_CHMPRN("Denied %s(sock:%d):%d by not allowed.", achname.c_str(), fd, ctlport);
			PxComSendConinitRes(fd, slvchmpxid, CHMPX_COM_RES_ERROR);

			// close
			if(!RawNotifyHup(fd)){
				ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", achname.c_str());
			}
			is_closed = true;
			return false;
		}

		// Set nonblock
		if(!ChmEventSock::SetNonblocking(fd)){
			WAN_CHMPRN("Failed to nonblock socket(sock:%d), but continue...", fd);
		}
		// update slave list in CHMSHM
		//
		// [NOTICE]
		// All server status which connects to this server is "SLAVE".
		// If other server connects this server for chain of RING, then this server sets that server status as "SLAVE" in slaves list.
		// See: comment in chmstructure.h
		//
		if(!pImData->SetSlaveAll(slvchmpxid, achname.c_str(), ctlport, pssl, fd, CHMPXSTS_SLAVE_UP_NORMAL)){
			ERR_CHMPRN("Failed to add/update slave list, chmpid=0x%016" PRIx64 ", hostname=%s, ctlport=%d, sock=%d", slvchmpxid, achname.c_str(), ctlport, fd);
			PxComSendConinitRes(fd, slvchmpxid, CHMPX_COM_RES_ERROR);

			// close
			if(!RawNotifyHup(fd)){
				ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", achname.c_str());
			}
			is_closed = true;
			return false;
		}

		// add internal mapping
		slavesockmap.set(fd, slvchmpxid);
		// remove mapping
		acceptingmap.erase(fd, NULL, NULL);		// do not call callback because this socket is used by slave map.

		// Send CHMPX_COM_CONINIT_RES(success)
		if(!PxComSendConinitRes(fd, slvchmpxid, CHMPX_COM_RES_SUCCESS)){
			ERR_CHMPRN("Failed to send CHMPX_COM_CONINIT_RES, but do(could) not recover for automatic closing.");
			return false;
		}

	}else if(ctlsockmap.find(fd)){
		// control port
		char		szReceive[CTL_COMMAND_MAX_LENGTH];
		size_t		RecLength	= CTL_COMMAND_MAX_LENGTH - 1;
		chmpxid_t	chmpxid		= ctlsockmap[fd];

		// receive command
		memset(szReceive, 0, CTL_COMMAND_MAX_LENGTH);
		if(!ChmEventSock::RawReceiveAny(fd, is_closed, reinterpret_cast<unsigned char*>(szReceive), &RecLength, false, sock_retry_count, sock_wait_time)){
			if(!is_closed){
				ERR_CHMPRN("Failed to receive data from ctlport ctlsock(%d), ctlsock is not closed.", fd);
			}else{
				MSG_CHMPRN("Failed to receive data from ctlport ctlsock(%d), ctlsock is closed.", fd);
				if(!RawNotifyHup(fd)){
					ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
				}
			}
			return false;
		}
		if(RecLength <= 0L){
			MSG_CHMPRN("Receive null command on control port from ctlsock(%d)", fd);
			if(!RawNotifyHup(fd)){
				ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
			}
			return true;
		}
		szReceive[RecLength] = '\0';		// terminate(not need this)

		// processing command.
		if(!Processing(fd, szReceive)){
			ERR_CHMPRN("Failed to do command(%s) from ctlsock(%d)", szReceive, fd);
			if(!RawNotifyHup(fd)){
				ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
			}
			return false;
		}

		// close ctlport
		if(!RawNotifyHup(fd)){
			ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", chmpxid);
		}
		return true;

	}else{
		// from other chmpx
		WAN_CHMPRN("Received from unknown sock(%d).", fd);

		if(!ChmEventSock::RawReceive(fd, GetSSL(fd), is_closed, &pComPkt, 0L, false, sock_retry_count, sock_wait_time) || !pComPkt){
			CHM_Free(pComPkt);

			if(!is_closed){
				ERR_CHMPRN("Failed to receive ComPkt from sock(%d), sock is not closed.", fd);
			}else{
				MSG_CHMPRN("Failed to receive ComPkt from sock(%d), sock is closed.", fd);
				if(!RawNotifyHup(fd)){
					ERR_CHMPRN("Failed to closing server(sock:%d), but continue...", fd);
				}
			}
			return false;
		}
	}

	// Dispatching
	if(pComPkt){
		if(pChmCntrl && !pChmCntrl->Processing(pComPkt, ChmCntrl::EVOBJ_TYPE_EVSOCK)){
			ERR_CHMPRN("Failed processing after receiving from socket.");
			CHM_Free(pComPkt);
			return false;
		}
		CHM_Free(pComPkt);
	}
	return true;
}

//
// [NOTICE]
// We set EPOLLRDHUP for socket event, then we will catch EPOLLRDHUP at here.
// EPOLLRDHUP has possibility of a problem that the packet just before FIN is replaced with FIN.
// So we need to receive rest data and 0 byte(EOF) in Receive() for closing.
//
bool ChmEventSock::NotifyHup(int fd)
{
	if(!Receive(fd)){
		ERR_CHMPRN("Failed to receive data from event fd(%d).", fd);
		return false;
	}
	return true;
}

//
// The connection which this server connect to other servers for joining RING
// when the mode is both server and slave is set EPOLLRDHUP for epoll.
// Then the connection occurres EPOLLRDHUP event by epoll, thus this method is
// called.
// This method closes the socket and reconnect next server on RING.
//
// [NOTE]
// This method is certainly close socket and remove epoll event.
//
bool ChmEventSock::RawNotifyHup(int fd)
{
	if(CHM_INVALID_HANDLE == fd){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	while(!fullock::flck_trylock_noshared_mutex(&sockfd_lockval));	// LOCK
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		// try to close for safety
		CloseSocketWithEpoll(fd);		// not check result
		fullock::flck_unlock_noshared_mutex(&sockfd_lockval);		// UNLOCK
		return false;
	}

	// [NOTE]
	// following method call is with locking sockfd_lockval.
	// so should not connect to new servers while locking.
	//
	bool		result	= true;
	bool		is_close= false;
	chmpxid_t	chmpxid	= CHM_INVALID_CHMPXID;
	if(!ServerSockNotifyHup(fd, is_close, chmpxid)){
		ERR_CHMPRN("Something error occured in closing server socket(%d) by HUP.", fd);
		result = false;
	}
	if(!is_close && !SlaveSockNotifyHup(fd, is_close)){
		ERR_CHMPRN("Something error occured in closing slave socket(%d) by HUP.", fd);
		result = false;
	}
	if(!is_close && !AcceptingSockNotifyHup(fd, is_close)){
		ERR_CHMPRN("Something error occured in closing accepting socket(%d) by HUP.", fd);
		result = false;
	}
	if(!is_close && !ControlSockNotifyHup(fd, is_close)){
		ERR_CHMPRN("Something error occured in closing control socket(%d) by HUP.", fd);
		result = false;
	}
	if(!is_close){
		// try to close for safety
		CloseSocketWithEpoll(fd);		// not check result
	}

	// Clear worker thread queue
	//
	// [NOTE]
	// Must clear before close socket.
	//
	if(procthreads.HasThread()){
		int	sock = static_cast<int>(procthreads.GetSelfWorkerParam());
		if(sock == fd){
			// fd is worker thread's socket, so clear worker loop count(queued work count).
			if(!procthreads.ClearSelfWorkerStatus()){
				ERR_CHMPRN("Something error occured in clearing worker thread loop count for sock(%d), but continue...", fd);
			}
		}
	}
	fullock::flck_unlock_noshared_mutex(&sockfd_lockval);		// UNLOCK

	// do rechain RING when closed fd is for servers.
	if(CHM_INVALID_CHMPXID != chmpxid){
		if(!ServerDownNotifyHup(chmpxid)){
			ERR_CHMPRN("Something error occured in setting chmpxid(0x%016" PRIx64 ") - fd(%d) to down status.", chmpxid, fd);
			result = false;
		}
	}

	return result;
}

bool ChmEventSock::ServerDownNotifyHup(chmpxid_t chmpxid)
{
	if(CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();

	// backup operation mode.
	bool	is_operating = pImData->IsOperating();

	// set status
	chmpxsts_t	status = pImData->GetServerStatus(chmpxid);

	CHANGE_CHMPXSTS_TO_DOWN(status);
	if(!pImData->SetServerStatus(chmpxid, status)){
		ERR_CHMPRN("Could not set status(0x%016" PRIx64 ":%s) to chmpxid(0x%016" PRIx64 "), but continue...", status, STR_CHMPXSTS_FULL(status).c_str(), chmpxid);
	}

	if(!is_server_mode){
		// do nothing no more for slave mode.
		return true;
	}

	// get next chmpxid on RING
	chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID != nextchmpxid){
		// rechain
		bool	is_rechain	= false;
		if(!CheckRechainRing(nextchmpxid, is_rechain)){
			ERR_CHMPRN("Something error occured in rehcaining RING, but continue...");
		}else{
			if(is_rechain){
				MSG_CHMPRN("Rechained RING to chmpxid(0x%016" PRIx64 ") for down chmpxid(0x%016" PRIx64 ").", nextchmpxid, chmpxid);
			}else{
				MSG_CHMPRN("Not rechained RING to chmpxid(0x%016" PRIx64 ") for down chmpxid(0x%016" PRIx64 ").", nextchmpxid, chmpxid);
			}
		}
	}

	// [NOTICE]
	// If merging, at first stop merging before sending "SERVER_DOWN".
	// Then do not care about status and pengins hash when receiving "SERVER_DOWN".
	//
	if(CHM_INVALID_CHMPXID == nextchmpxid){
		// there is no server on RING.
		if(is_operating){
			if(!MergeAbort()){
				ERR_CHMPRN("Got server down notice during merging, but failed to abort merge.");
				return false;
			}
		}
	}else{
		if(is_operating){
			if(!PxComSendMergeAbort(nextchmpxid)){
				ERR_CHMPRN("Got server down notice during merging, but failed to abort merge. but continue...");
			}
		}
		// send SERVER_DOWN
		if(!PxComSendServerDown(nextchmpxid, chmpxid)){
			ERR_CHMPRN("Failed to send down chmpx server information to chmpxid(0x%016" PRIx64 ").", nextchmpxid);
			return false;
		}
	}
	return true;
}

// [NOTE]
// The method which calls this method locks sockfd_lockval.
// So this method returns assap without doing any.
//
bool ChmEventSock::ServerSockNotifyHup(int fd, bool& is_close, chmpxid_t& chmpxid)
{
	is_close= false;
	chmpxid	= CHM_INVALID_CHMPXID;

	// [NOTE]
	// we not check fd and object empty because NotifyHup() already checks it.
	//
	if(!seversockmap.find(fd)){
		return true;
	}
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();

	// get sock to chmpxid
	if(CHM_INVALID_CHMPXID == (chmpxid = pImData->GetChmpxIdBySock(fd, CLOSETG_SERVERS))){
		ERR_CHMPRN("Could not find sock(%d) in servers list, but continue...", fd);
	}

	// close socket at first.(check both server and slave)
	seversockmap.erase(fd);			// call default cb function
	is_close = true;

	return true;
}

// [NOTE]
// The method which calls this method locks sockfd_lockval.
// So this method returns assap without doing any.
//
bool ChmEventSock::SlaveSockNotifyHup(int fd, bool& is_close)
{
	is_close = false;

	// [NOTE]
	// we not check fd and object empty because NotifyHup() already checks it.
	//
	if(!slavesockmap.find(fd)){
		return true;
	}
	// close socket
	slavesockmap.erase(fd);
	is_close = true;

	return true;
}

// [NOTE]
// The method which calls this method locks sockfd_lockval.
// So this method returns assap without doing any.
//
bool ChmEventSock::AcceptingSockNotifyHup(int fd, bool& is_close)
{
	is_close = false;

	// [NOTE]
	// we not check fd and object empty because NotifyHup() already checks it.
	//
	if(!acceptingmap.find(fd)){
		return true;
	}
	// close socket
	acceptingmap.erase(fd);
	is_close = true;

	return true;
}

// [NOTE]
// The method which calls this method locks sockfd_lockval.
// So this method returns assap without doing any.
//
bool ChmEventSock::ControlSockNotifyHup(int fd, bool& is_close)
{
	is_close = false;

	// [NOTE]
	// we not check fd and object empty because NotifyHup() already checks it.
	//
	if(!ctlsockmap.find(fd)){
		return true;
	}
	// close socket
	ctlsockmap.erase(fd);
	is_close = true;

	return true;
}

//---------------------------------------------------------
// Methods for Communication controls
//---------------------------------------------------------
bool ChmEventSock::CloseSelfSocks(void)
{
	if(IsEmpty()){
		return true;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// close own liten sockets
	int	sock	= CHM_INVALID_SOCK;
	int	ctlsock	= CHM_INVALID_SOCK;
	if(!pImData->GetSelfSocks(sock, ctlsock)){
		ERR_CHMPRN("Could not get self sock and ctlsock.");
		return false;
	}
	if(CHM_INVALID_SOCK != sock){
		epoll_ctl(eqfd, EPOLL_CTL_DEL, sock, NULL);
	}
	if(CHM_INVALID_SOCK != ctlsock){
		epoll_ctl(eqfd, EPOLL_CTL_DEL, ctlsock, NULL);
	}
	CloseSSL(sock);
	CloseSSL(ctlsock);

	// update sock in chmshm
	bool	result = true;
	if(!pImData->SetSelfSocks(CHM_INVALID_SOCK, CHM_INVALID_SOCK)){
		ERR_CHMPRN("Could not clean self sock and ctlsock, but continue...");
		result = false;
	}

	// update status in chmshm
	chmpxsts_t	status = pImData->GetSelfStatus();

	CHANGE_CHMPXSTS_TO_DOWN(status);
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Could not set status(0x%016" PRIx64 ":%s) for self.", status, STR_CHMPXSTS_FULL(status).c_str());
		result = false;
	}

	return result;
}

bool ChmEventSock::CloseFromSlavesSocks(void)
{
	if(IsEmpty()){
		return true;
	}
	// close sockets connecting from clients
	slavesockmap.clear();

	return true;
}

bool ChmEventSock::CloseToServersSocks(void)
{
	if(IsEmpty()){
		return true;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// close all server ssl
	seversockmap.clear();	// call default cb function

	// close sockets connecting to other servers.
	if(!pImData->CloseSocks(CLOSETG_SERVERS)){
		ERR_CHMPRN("Failed to close connection to other servers.");
		return false;
	}
	return true;
}

bool ChmEventSock::CloseCtlportClientSocks(void)
{
	if(IsEmpty()){
		return true;
	}
	ctlsockmap.clear();
	return true;
}

bool ChmEventSock::CloseFromSlavesAcceptingSocks(void)
{
	if(IsEmpty()){
		return true;
	}
	// close accepting sockets connecting from clients
	acceptingmap.clear();
	return true;
}

bool ChmEventSock::CloseSSL(int sock, bool with_sock)
{
	if(IsEmpty()){
		return true;
	}
	sslmap.erase(sock);

	if(with_sock){
		CHM_CLOSESOCK(sock);
	}
	return true;
}

bool ChmEventSock::CloseAllSSL(void)
{
	if(IsEmpty()){
		return true;
	}
	sslmap.clear();
	return true;
}

//
// [NOTE]
// Must lock sockfd_lockval outside.
//
bool ChmEventSock::CloseSocketWithEpoll(int sock)
{
	if(CHM_INVALID_SOCK == sock){
		return false;
	}
	if(0 != epoll_ctl(eqfd, EPOLL_CTL_DEL, sock, NULL)){
		// [NOTE]
		// Sometimes epoll_ctl retuns error, because socket is not add epoll yet.
		//
		MSG_CHMPRN("Failed to delete socket(%d) from epoll event, probably already remove it.", sock);
	}
	CloseSSL(sock);

	return true;
}

//
// [NOTICE] This method blocks receiving data, means waits connecting.
// (See. RawConnectServer method)
//
bool ChmEventSock::ConnectServer(chmpxid_t chmpxid, int& sock, bool without_set_imdata)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// [NOTE]
	// This method checks only existing socket to chmpxid. Do not check lock
	// status for socket and count of socket.
	//
	socklist_t	socklist;
	if(pImData->GetServerSock(chmpxid, socklist) && !socklist.empty()){
		// already connected.
		sock = socklist.front();
		return true;
	}
	return RawConnectServer(chmpxid, sock, without_set_imdata, false);		// Do not lock socket
}

//
// [NOTICE] This method blocks receiving data, means waits connecting.
//
// This method try to connect chmpxid server, and send CHMPX_COM_CONINIT_REQ request
// after connecting the server and receive CHMPX_COM_CONINIT_RES. This is flow for
// connecting server.
// So that this method calls RawSend and RawReceive in PxComSendConinitReq().
//
bool ChmEventSock::RawConnectServer(chmpxid_t chmpxid, int& sock, bool without_set_imdata, bool is_lock)
{
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// get hostanme/port
	string	hostname;
	short	port	= CHM_INVALID_PORT;
	short	ctlport	= CHM_INVALID_PORT;
	if(!pImData->GetServerBase(chmpxid, hostname, port, ctlport)){
		ERR_CHMPRN("Could not get hostname/port/ctlport by chmpxid(0x%016" PRIx64 ").", chmpxid);
		return false;
	}

	// try to connect
	while(!fullock::flck_trylock_noshared_mutex(&sockfd_lockval));	// LOCK
	if(CHM_INVALID_SOCK == (sock = ChmEventSock::Connect(hostname.c_str(), port, false, con_retry_count, con_wait_time))){
		// could not connect
		MSG_CHMPRN("Could not connect to %s:%d, set status to DOWN.", hostname.c_str(), port);
		fullock::flck_unlock_noshared_mutex(&sockfd_lockval);		// UNLOCK
		return false;
	}
	// connected
	MSG_CHMPRN("Connected to %s:%d, set sock(%d).", hostname.c_str(), port, sock);
	fullock::flck_unlock_noshared_mutex(&sockfd_lockval);			// UNLOCK

	// SSL
	CHMPXSSL	ssldata;
	if(!pImData->GetServerBase(chmpxid, ssldata)){
		ERR_CHMPRN("Failed to get SSL structure from chmpxid(0x%" PRIx64 ").", chmpxid);
		CHM_CLOSESOCK(sock);
		return false;
	}
	if(ssldata.is_ssl){		// Check whichever the terget server is ssl
		// Get own keys
		if(!pImData->GetSelfSsl(ssldata)){
			ERR_CHMPRN("Failed to get SSL structure from self chmpx.");
			CHM_CLOSESOCK(sock);
			return false;
		}
		// get SSL context
		SSL_CTX*	ctx = ChmEventSock::GetSSLContext((!ssldata.is_ca_file ? ssldata.capath : NULL), (ssldata.is_ca_file ? ssldata.capath : NULL), NULL, NULL, ssldata.slave_cert, ssldata.slave_prikey, ssldata.verify_peer);
		if(!ctx){
			ERR_CHMPRN("Failed to get SSL context.");
			CHM_CLOSESOCK(sock);
			return false;
		}

		// Accept SSL
		SSL*	ssl = ChmEventSock::ConnectSSL(ctx, sock, con_retry_count, con_wait_time);
		if(!ssl){
			ERR_CHMPRN("Failed to connect SSL.");
			CHM_CLOSESOCK(sock);
			return false;
		}

		// Set SSL mapping
		sslmap.set(sock, ssl, true);		// over write whtere exitsts or does not.
	}

	// Send coninit_req
	if(!PxComSendConinitReq(sock, chmpxid)){
		ERR_CHMPRN("Failed to send PCCOM_CONINIT_REQ to sock(%d).", sock);
		CloseSSL(sock);
		return false;
	}

	// Receive resnponse
	// 
	// [NOTICE]
	// retry count is sock_retry_count
	// 
	bool	is_closed	= false;
	PCOMPKT	pComPkt		= NULL;
	if(!ChmEventSock::RawReceive(sock, GetSSL(sock), is_closed, &pComPkt, 0L, false, sock_retry_count, sock_wait_time) || !pComPkt){
		ERR_CHMPRN("Failed to receive ComPkt from sock(%d), sock is %s.", sock, is_closed ? "closed" : "not closed");
		CloseSSL(sock);
		CHM_Free(pComPkt);
		return false;
	}

	// Check response type
	if(COM_PX2PX != pComPkt->head.type){
		ERR_CHMPRN("Received data is %s(%" PRIu64 ") type, which does not COM_PX2PX type from sock(%d).", STRCOMTYPE(pComPkt->head.type), pComPkt->head.type, sock);
		CloseSSL(sock);
		CHM_Free(pComPkt);
		return false;
	}
	PPXCOM_ALL	pChmpxCom = CHM_OFFSET(pComPkt, pComPkt->offset, PPXCOM_ALL);
	if(CHMPX_COM_CONINIT_RES != pChmpxCom->val_head.type){
		ERR_CHMPRN("Received data is %s(%" PRIu64 ") type, which does not CHMPX_COM_CONINIT_RES type from sock(%d).", STRPXCOMTYPE(pChmpxCom->val_head.type), pChmpxCom->val_head.type, sock);
		CloseSSL(sock);
		CHM_Free(pComPkt);
		return false;
	}

	// Check response
	PCOMPKT		pResComPkt		= NULL;	// tmp
	pxcomres_t	coninit_result	= CHMPX_COM_RES_ERROR;
	if(!PxComReceiveConinitRes(&(pComPkt->head), pChmpxCom, &pResComPkt, coninit_result)){
		ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
		CloseSSL(sock);
		CHM_Free(pComPkt);
		CHM_Free(pResComPkt);
		return false;
	}
	CHM_Free(pComPkt);
	CHM_Free(pResComPkt);

	if(CHMPX_COM_RES_SUCCESS != coninit_result){
		ERR_CHMPRN("Connected to %s:%d, but could not allow from this server.", hostname.c_str(), port);
		CloseSSL(sock);
		return false;
	}

	// Lock
	if(is_lock){
		LockSendSock(sock);			// LOCK SOCKET
	}

	// Set IM data
	if(!without_set_imdata){
		// set mapping
		seversockmap.set(sock, chmpxid);

		// add event fd
		struct epoll_event	eqevent;
		memset(&eqevent, 0, sizeof(struct epoll_event));
		eqevent.data.fd	= sock;
		eqevent.events	= EPOLLIN | EPOLLET | EPOLLRDHUP;			// EPOLLRDHUP is set, connecting to server socket is needed to know server side down.
		if(-1 == epoll_ctl(eqfd, EPOLL_CTL_ADD, sock, &eqevent)){
			ERR_CHMPRN("Failed to add sock(port %d: sock %d) into epoll event(%d), errno=%d", port, sock, eqfd, errno);
			seversockmap.erase(sock);
			return false;
		}
		// set sock in server list and update status
		if(!pImData->SetServerSocks(chmpxid, sock, CHM_INVALID_SOCK, SOCKTG_SOCK)){
			MSG_CHMPRN("Could not set sock(%d) to chmpxid(0x%016" PRIx64 ").", sock, chmpxid);
			seversockmap.erase(sock);
			return false;
		}
	}
	return true;
}

//
// This method try to connect all server, and updates chmshm status for each server
// when connecting servers.
//
bool ChmEventSock::ConnectServers(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}

	ChmIMData*	pImData = pChmCntrl->GetImDataObj();
	if(is_server_mode){
		ERR_CHMPRN("Chmpx mode does not slave mode.");
		return false;
	}

	for(chmpxpos_t pos = pImData->GetNextServerPos(CHM_INVALID_CHMPXLISTPOS, CHM_INVALID_CHMPXLISTPOS, false, false); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(CHM_INVALID_CHMPXLISTPOS, pos, false, false)){
		// get base information
		string		name;
		socklist_t	socklist;
		short		port;
		short		ctlport;
		chmpxid_t	chmpxid	= CHM_INVALID_CHMPXID;
		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport) || CHM_INVALID_CHMPXID == chmpxid){
			ERR_CHMPRN("Could not get serer name/chmpxid/port/ctlport for pos(%" PRIu64 ").", pos);
			continue;
		}

		// status check
		chmpxsts_t	status = pImData->GetServerStatus(chmpxid);
		if(!IS_CHMPXSTS_SERVER(status) || !IS_CHMPXSTS_UP(status)){
			MSG_CHMPRN("chmpid(0x%016" PRIx64 ") which is pos(%" PRIu64 ") is status(0x%016" PRIx64 ":%s), not enough status to connecting.", chmpxid, pos, status, STR_CHMPXSTS_FULL(status).c_str());
			continue;
		}

		// check socket to server and try to connect if not exist.
		int	sock = CHM_INVALID_SOCK;
		if(!ConnectServer(chmpxid, sock, false) || CHM_INVALID_SOCK == sock){		// connect & set epoll & chmshm
			// could not connect
			ERR_CHMPRN("Could not connect to %s:%d, set status to DOWN.", name.c_str(), port);

			// [NOTICE]
			// Other server status is updated, because DOWN status can not be sent by that server.
			//
			CHANGE_CHMPXSTS_TO_DOWN(status);
			if(!pImData->SetServerStatus(chmpxid, status)){
				MSG_CHMPRN("Could not set status(0x%016" PRIx64 ":%s) by down to chmpid(%" PRIu64 ") which is pos(%" PRIu64 ").", status, STR_CHMPXSTS_FULL(status).c_str(), chmpxid, pos);
				continue;
			}
		}
	}
	return true;
}

bool ChmEventSock::ConnectRing(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Check self status, must be service out
	if(!is_server_mode){
		ERR_CHMPRN("Chmpx mode does not server-mode.");
		return false;
	}
	chmpxsts_t	selfstatus = pImData->GetSelfStatus();
	if(!IS_CHMPXSTS_SERVER(selfstatus) || !IS_CHMPXSTS_UP(selfstatus)){
		ERR_CHMPRN("This server is status(0x%016" PRIx64 ":%s), not enough of self status for connecting.", selfstatus, STR_CHMPXSTS_FULL(selfstatus).c_str());
		return false;
	}

	// Get self pos
	chmpxpos_t	selfpos = pImData->GetSelfServerPos();
	if(CHM_INVALID_CHMPXLISTPOS == selfpos){
		ERR_CHMPRN("Not found self chmpx data in list.");
		return false;
	}

	int			sock	= CHM_INVALID_SOCK;
	chmpxpos_t	pos		= selfpos;	// Start at self pos
	for(pos = pImData->GetNextServerPos(selfpos, pos, true, true); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(selfpos, pos, true, true)){
		// get server information
		string		name;
		short		port;
		short		ctlport;
		chmpxid_t	chmpxid	= CHM_INVALID_CHMPXID;
		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport)){
			ERR_CHMPRN("Could not get serer name/chmpxid/port/ctlport for pos(%" PRIu64 ").", pos);
			continue;
		}

		// status check
		chmpxsts_t	status = pImData->GetServerStatus(chmpxid);
		if(!IS_CHMPXSTS_SERVER(status) || !IS_CHMPXSTS_UP(status)){
			MSG_CHMPRN("chmpid(0x%016" PRIx64 "):%s which is pos(%" PRIu64 ") is status(0x%01lx:%s), not enough status to connecting.", chmpxid, name.c_str(), pos, status, STR_CHMPXSTS_FULL(status).c_str());
			continue;
		}

		// try to connect
		sock = CHM_INVALID_SOCK;
		if(!ConnectServer(chmpxid, sock, false) || CHM_INVALID_SOCK == sock){	// connect & set epoll & chmshm
			// could not connect
			ERR_CHMPRN("Could not connect to %s:%d, set status to DOWN.", name.c_str(), port);
		}else{
			// send CHMPX_COM_JOIN_RING
			CHMPXSVR	selfchmpxsvr;
			if(!pImData->GetSelfChmpxSvr(&selfchmpxsvr)){
				ERR_CHMPRN("Could not get self chmpx information, this error can not recover.");
				return false;
			}
			if(!PxComSendJoinRing(chmpxid, &selfchmpxsvr)){
				// Failed to join.
				ERR_CHMPRN("Failed to send CHMPX_COM_JOIN_RING to chmpxid(0x%016" PRIx64 ") on RING, so close connection to servers.", chmpxid);
				if(!CloseToServersSocks()){
					ERR_CHMPRN("Failed to close connection to other servers, but continue...");
				}
				return false;
			}
			break;
		}
	}
	if(CHM_INVALID_SOCK == sock){
		//
		// This case is there is no server UP on RING.
		// It means this server is first UP on RING.
		//
		ERR_CHMPRN("There is no server UP on RING.");
		return true;
	}
	return true;
}

bool ChmEventSock::CloseRechainRing(chmpxid_t nowchmpxid)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();

	chmpxpos_t	selfpos	= pImData->GetSelfServerPos();
	if(CHM_INVALID_CHMPXLISTPOS == selfpos){
		ERR_CHMPRN("Not found self chmpx data in list.");
		return false;
	}

	chmpxpos_t	pos = selfpos;	// Start at self pos
	for(pos = pImData->GetNextServerPos(selfpos, pos, true, true); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(selfpos, pos, true, true)){
		string		name;
		socklist_t	socklist;
		chmpxid_t	chmpxid	= CHM_INVALID_CHMPXID;
		short		port	= CHM_INVALID_PORT;
		short		ctlport	= CHM_INVALID_PORT;
		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport) || CHM_INVALID_CHMPXID == chmpxid){
			ERR_CHMPRN("Could not get chmpxid by position(%" PRIu64 ") in server list, but continue...", pos);
			continue;
		}
		if(nowchmpxid == chmpxid){
			continue;
		}

		// [NOTE]
		// Close all sockets to servers except next server.
		// 
		socklist.clear();
		if(!pImData->GetServerSock(chmpxid, socklist) || socklist.empty()){
			MSG_CHMPRN("Not have connection to chmpxid(0x%016" PRIx64 ").", chmpxid);
			continue;
		}
		// close
		for(socklist_t::iterator iter = socklist.begin(); iter != socklist.end(); iter = socklist.erase(iter)){
			seversockmap.erase(*iter);
		}
	}
	return true;
}

bool ChmEventSock::RechainRing(chmpxid_t newchmpxid)
{
	if(CHM_INVALID_CHMPXID == newchmpxid){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}

	// connect new server.
	int	newsock = CHM_INVALID_SOCK;
	if(!ConnectServer(newchmpxid, newsock, false) || CHM_INVALID_SOCK == newsock){	// update chmshm
		ERR_CHMPRN("Could not connect new server chmpxid=0x%016" PRIx64 ", and could not recover about this error.", newchmpxid);
		return false;
	}

	if(!CloseRechainRing(newchmpxid)){
		ERR_CHMPRN("Something error occured, close old chain ring except chmpxid(0x%016" PRIx64 ").", newchmpxid);
		return false;
	}
	return true;
}

bool ChmEventSock::CheckRechainRing(chmpxid_t newchmpxid, bool& is_rechain)
{
	if(CHM_INVALID_CHMPXID == newchmpxid){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	is_rechain = false;

	ChmIMData*	pImData		= pChmCntrl->GetImDataObj();
	chmpxid_t	nowchmpxid	= pImData->GetNextRingChmpxId();
	if(nowchmpxid == newchmpxid){
		socklist_t	nowsocklist;
		if(pImData->GetServerSock(nowchmpxid, nowsocklist) && !nowsocklist.empty()){
			MSG_CHMPRN("New next chmpxid(0x%016" PRIx64 ") is same as now chmpxid(0x%016" PRIx64 "), so do not need rechain RING.", newchmpxid, nowchmpxid);
			return true;
		}
	}

	chmpxpos_t	selfpos = pImData->GetSelfServerPos();
	if(CHM_INVALID_CHMPXLISTPOS == selfpos){
		ERR_CHMPRN("Not found self chmpx data in list.");
		return false;
	}

	chmpxpos_t	pos = selfpos;	// Start at self pos
	string		name;
	chmpxid_t	chmpxid;
	short		port;
	short		ctlport;
	for(pos = pImData->GetNextServerPos(selfpos, pos, true, true); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(selfpos, pos, true, true)){
		chmpxid	= CHM_INVALID_CHMPXID;
		port	= CHM_INVALID_PORT;
		ctlport	= CHM_INVALID_PORT;
		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport) || CHM_INVALID_CHMPXID == chmpxid){
			ERR_CHMPRN("Could not get chmpxid by position(%" PRIu64 ") in server list, but continue...", pos);
			continue;
		}
		if(nowchmpxid == chmpxid){
			socklist_t	nowsocklist;
			if(pImData->GetServerSock(nowchmpxid, nowsocklist) && !nowsocklist.empty()){
				MSG_CHMPRN("Found now next chmpxid(0x%016" PRIx64 ") before new chmpxid(0x%016" PRIx64 "), so do not need rechain RING.", nowchmpxid, newchmpxid);
			}else{
				// do rechain
				if(!RechainRing(nowchmpxid)){
					ERR_CHMPRN("Failed to rechain RING for chmpxid(0x%016" PRIx64 ").", nowchmpxid);
				}else{
					MSG_CHMPRN("Succeed to rechain RING for chmpxid(0x%016" PRIx64 ").", nowchmpxid);
					is_rechain = true;
				}
			}
			return true;

		}else if(newchmpxid == chmpxid){
			MSG_CHMPRN("Found new chmpxid(0x%016" PRIx64 ") before next chmpxid(0x%016" PRIx64 "), so need rechain RING.", newchmpxid, nowchmpxid);

			// do rechain
			if(!RechainRing(newchmpxid)){
				ERR_CHMPRN("Failed to rechain RING for chmpxid(0x%016" PRIx64 ").", newchmpxid);
			}else{
				MSG_CHMPRN("Succeed to rechain RING for chmpxid(0x%016" PRIx64 ").", newchmpxid);
				is_rechain = true;
			}
			return true;
		}
	}
	// why...?
	MSG_CHMPRN("There is no server UP on RING(= there is not the server of chmpxid(0x%016" PRIx64 ") in server list), so can not rechain RING.", newchmpxid);
	return true;
}

//
// If the RING from this server is broken, connect to next server in RING for recovering.
// Returns the chmpxid of next connected server in RING when TRUE.
//
chmpxid_t ChmEventSock::GetNextRingChmpxId(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData		= pChmCntrl->GetImDataObj();
	chmpxid_t	chmpxid;

	if(CHM_INVALID_CHMPXID != (chmpxid = pImData->GetNextRingChmpxId())){
		MSG_CHMPRN("Not need to connect rechain RING.");
		return chmpxid;
	}

	chmpxpos_t	selfpos = pImData->GetSelfServerPos();
	if(CHM_INVALID_CHMPXLISTPOS == selfpos){
		ERR_CHMPRN("Not found self chmpx data in list.");
		return CHM_INVALID_CHMPXID;
	}

	chmpxpos_t	pos = selfpos;	// Start at self pos
	string		name;
	short		port;
	short		ctlport;
	for(pos = pImData->GetNextServerPos(selfpos, pos, true, true); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(selfpos, pos, true, true)){
		chmpxid	= CHM_INVALID_CHMPXID;
		port	= CHM_INVALID_PORT;
		ctlport	= CHM_INVALID_PORT;

		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport) || CHM_INVALID_CHMPXID == chmpxid){
			WAN_CHMPRN("Could not get chmpxid by position(%" PRIu64 ") in server list, but continue...", pos);
			continue;
		}
		// status check
		chmpxsts_t	status = pImData->GetServerStatus(chmpxid);
		if(IS_CHMPXSTS_SERVER(status) && IS_CHMPXSTS_UP(status)){
			// try to connect server.
			int	sock = CHM_INVALID_SOCK;
			if(ConnectServer(chmpxid, sock, false) && CHM_INVALID_SOCK != sock){			// update chmshm
				MSG_CHMPRN("Connect new server chmpxid(0x%016" PRIx64 ") to sock(%d), and recover RING.", chmpxid, sock);
				return chmpxid;
			}else{
				ERR_CHMPRN("Could not connect new server chmpxid(0x%016" PRIx64 "), so try to next server.", chmpxid);
			}
		}
	}
	WAN_CHMPRN("There is no connect-able server in RING. probably there is no UP server in RING.");
	return CHM_INVALID_CHMPXID;
}

// Check next chmpxid which is over step deperture chmpxid in RING.
// On this case, if send packet to next chmpxid, it includes loop packet in RING.
// So we need to check next and deperture chmpxid before transfer packet.
//
bool ChmEventSock::IsSafeDeptAndNextChmpxId(chmpxid_t dept_chmpxid, chmpxid_t next_chmpxid)
{
	if(CHM_INVALID_CHMPXID == dept_chmpxid || CHM_INVALID_CHMPXID ==  next_chmpxid){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	if(dept_chmpxid == next_chmpxid){
		return true;
	}

	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();
	chmpxpos_t	selfpos = pImData->GetSelfServerPos();
	if(CHM_INVALID_CHMPXLISTPOS == selfpos){
		ERR_CHMPRN("Not found self chmpx data in list.");
		return false;
	}

	chmpxpos_t	pos = selfpos;	// Start at self pos
	string		name;
	chmpxid_t	chmpxid;
	short		port;
	short		ctlport;
	for(pos = pImData->GetNextServerPos(selfpos, pos, true, true); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(selfpos, pos, true, true)){
		chmpxid	= CHM_INVALID_CHMPXID;
		port	= CHM_INVALID_PORT;
		ctlport	= CHM_INVALID_PORT;
		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport) || CHM_INVALID_CHMPXID == chmpxid){
			ERR_CHMPRN("Could not get chmpxid by position(%" PRIu64 ") in server list, but continue...", pos);
			continue;
		}
		if(next_chmpxid == chmpxid){
			MSG_CHMPRN("Found next chmpxid(0x%016" PRIx64 ") before deperture chmpxid(0x%016" PRIx64 "), so can send this pkt.", next_chmpxid, dept_chmpxid);
			return true;
		}
		if(dept_chmpxid == chmpxid){
			MSG_CHMPRN("Found deperture chmpxid(0x%016" PRIx64 ") before next chmpxid(0x%016" PRIx64 "), so can not send this pkt.", dept_chmpxid, next_chmpxid);
			return false;
		}
	}
	// why...?
	MSG_CHMPRN("There is no server UP on RING(= there is not the server of chmpxid(0x%016" PRIx64 ") in server list), so can not send this pkt.", next_chmpxid);
	return false;
}

bool ChmEventSock::Accept(int sock)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	while(!fullock::flck_trylock_noshared_mutex(&sockfd_lockval));	// LOCK

	// accept
	int						newsock;
	struct sockaddr_storage	from;
	socklen_t				fromlen = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
	if(-1 == (newsock = accept(sock, reinterpret_cast<struct sockaddr*>(&from), &fromlen))){
		if(EAGAIN == errno || EWOULDBLOCK == errno){
			MSG_CHMPRN("There is no waiting accept request for sock(%d)", sock);
			fullock::flck_unlock_noshared_mutex(&sockfd_lockval);	// UNLOCK
			return true;	// return succeed
		}
		ERR_CHMPRN("Failed to accept from sock(%d), error=%d", sock, errno);
		fullock::flck_unlock_noshared_mutex(&sockfd_lockval);		// UNLOCK
		return false;
	}
	MSG_CHMPRN("Accept new socket(%d)", newsock);
	fullock::flck_unlock_noshared_mutex(&sockfd_lockval);			// UNLOCK

	// get hostanme for accessing control
	string	stripaddress;
	string	strhostname;
	if(!ChmNetDb::CvtAddrInfoToIpAddress(&from, fromlen, stripaddress)){
		ERR_CHMPRN("Failed to convert addrinfo(new sock=%d) to ipaddress.", newsock);
		CHM_CLOSESOCK(newsock);
		return false;
	}
	if(!ChmNetDb::Get()->GetHostname(stripaddress.c_str(), strhostname, true)){
		ERR_CHMPRN("Failed to convert FQDN from %s.", stripaddress.c_str());
		CHM_CLOSESOCK(newsock);
		return false;
	}

	// add tempolary accepting socket mapping.(before adding epoll for multi threading)
	acceptingmap.set(newsock, strhostname);

	// check ACL
	if(!pImData->IsAllowHostname(strhostname.c_str())){
		// Not allow accessing from slave.
		ERR_CHMPRN("Denied %s(sock:%d) by not allowed.", strhostname.c_str(), newsock);
		if(!NotifyHup(newsock)){
			ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", strhostname.c_str());
		}
		return false;
	}

	// Set nonblock
	if(!ChmEventSock::SetNonblocking(newsock)){
		WAN_CHMPRN("Failed to nonblock socket(sock:%d), but continue...", newsock);
	}

	//
	// Push accepting socket which is not allowed completely yet.
	//
	MSG_CHMPRN("%s(sock:%d) is accepting tempolary, but not allowed completely yet..", strhostname.c_str(), newsock);

	// SSL
	CHMPXSSL	ssldata;
	if(!pImData->GetSelfSsl(ssldata)){
		ERR_CHMPRN("Failed to get SSL structure from self chmpx.");
		if(!NotifyHup(newsock)){
			ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", strhostname.c_str());
		}
		return false;
	}
	if(ssldata.is_ssl){
		// get SSL context
		SSL_CTX*	ctx = ChmEventSock::GetSSLContext((!ssldata.is_ca_file ? ssldata.capath : NULL), (ssldata.is_ca_file ? ssldata.capath : NULL), ssldata.server_cert, ssldata.server_prikey, ssldata.slave_cert, ssldata.slave_prikey, ssldata.verify_peer);
		if(!ctx){
			ERR_CHMPRN("Failed to get SSL context.");
			if(!NotifyHup(newsock)){
				ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", strhostname.c_str());
			}
			return false;
		}

		// Accept SSL
		SSL*	ssl = ChmEventSock::AcceptSSL(ctx, newsock, con_retry_count, con_wait_time);
		if(!ssl){
			ERR_CHMPRN("Failed to accept SSL.");
			if(!NotifyHup(newsock)){
				ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", strhostname.c_str());
			}
			return false;
		}

		// Set SSL mapping
		sslmap.set(newsock, ssl, true);		// over write
	}

	// add event fd
	struct epoll_event	eqevent;
	memset(&eqevent, 0, sizeof(struct epoll_event));
	eqevent.data.fd	= newsock;
	eqevent.events	= EPOLLIN | EPOLLET | EPOLLRDHUP;				// EPOLLRDHUP is set.
	if(-1 == epoll_ctl(eqfd, EPOLL_CTL_ADD, newsock, &eqevent)){
		ERR_CHMPRN("Failed to add sock(sock %d) into epoll event(%d), errno=%d", newsock, eqfd, errno);
		if(!NotifyHup(newsock)){
			ERR_CHMPRN("Failed to closing \"accepting socket\" for %s, but continue...", strhostname.c_str());
		}
		return false;
	}
	//
	// CONINIT_REQ will get in main event loop.
	//
	return true;
}

// Accept socket for control port.
// The socket does not have chmpxid and not need to set chmshm.
// The sock value has only internal this class.
//
bool ChmEventSock::AcceptCtlport(int ctlsock)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// accept
	int						newctlsock;
	struct sockaddr_storage	from;
	socklen_t				fromlen = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
	if(-1 == (newctlsock = accept(ctlsock, reinterpret_cast<struct sockaddr*>(&from), &fromlen))){
		if(EAGAIN == errno || EWOULDBLOCK == errno){
			MSG_CHMPRN("There is no waiting accept request for ctlsock(%d)", ctlsock);
			return false;
		}
		ERR_CHMPRN("Failed to accept from ctlsock(%d), error=%d", ctlsock, errno);
		return false;
	}
	// add internal mapping
	ctlsockmap.set(newctlsock, CHM_INVALID_CHMPXID);	// control sock does not have chmpxid

	// Convert IPv4-mapped IPv6 addresses to plain IPv4.
	if(!ChmNetDb::CvtV4MappedAddrInfo(&from, fromlen)){
		ERR_CHMPRN("Something error occured during converting IPv4-mapped IPv6 addresses to plain IPv4, but continue...");
	}

	// get hostanme for accessing control
	string	stripaddress;
	string	strhostname;
	if(!ChmNetDb::CvtAddrInfoToIpAddress(&from, fromlen, stripaddress)){
		ERR_CHMPRN("Failed to convert addrinfo(new ctlsock=%d) to ipaddress.", newctlsock);
		if(!NotifyHup(newctlsock)){
			ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", CHM_INVALID_CHMPXID);
		}
		return false;
	}
	if(!ChmNetDb::Get()->GetHostname(stripaddress.c_str(), strhostname, true)){
		ERR_CHMPRN("Failed to convert FQDN from %s.", stripaddress.c_str());
		if(!NotifyHup(newctlsock)){
			ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", CHM_INVALID_CHMPXID);
		}
		return false;
	}

	// check ACL
	if(!pImData->IsAllowHostname(strhostname.c_str())){
		// Not allow accessing from slave.
		ERR_CHMPRN("Denied %s(ctlsock:%d) by not allowed.", strhostname.c_str(), newctlsock);
		if(!NotifyHup(newctlsock)){
			ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", CHM_INVALID_CHMPXID);
		}
		return false;
	}

	// Set nonblock
	if(!ChmEventSock::SetNonblocking(newctlsock)){
		WAN_CHMPRN("Failed to nonblock socket(sock:%d), but continue...", newctlsock);
	}

	// add event fd
	struct epoll_event	eqevent;
	memset(&eqevent, 0, sizeof(struct epoll_event));
	eqevent.data.fd	= newctlsock;
	eqevent.events	= EPOLLIN | EPOLLET | EPOLLRDHUP;	// EPOLLRDHUP is set.
	if(-1 == epoll_ctl(eqfd, EPOLL_CTL_ADD, newctlsock, &eqevent)){
		ERR_CHMPRN("Failed to add socket(ctlsock %d) into epoll event(%d), errno=%d", newctlsock, eqfd, errno);
		if(!NotifyHup(newctlsock)){
			ERR_CHMPRN("Failed to closing \"from control socket\" for chmpxid(0x%016" PRIx64 "), but continue...", CHM_INVALID_CHMPXID);
		}
		return false;
	}

	return true;
}

// [NOTICE]
// This function is updating all server status from a server on RING.
// This method opens the port of one of servers, and sends CHMPX_COM_STATUS_REQ,
// receives CHMPX_COM_STATUS_RES, updates all server status by result.
// This method uses the socket for that port, the socket is connecting very short time.
// After updating all server status from the result, this method closes that socket ASSAP.
//
bool ChmEventSock::InitialAllServerStatus(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Connect to ONE of servers.
	int			sock	= CHM_INVALID_SOCK;
	chmpxid_t	chmpxid	= CHM_INVALID_CHMPXID;
	for(chmpxpos_t pos = pImData->GetNextServerPos(CHM_INVALID_CHMPXLISTPOS, CHM_INVALID_CHMPXLISTPOS, true, false); CHM_INVALID_CHMPXLISTPOS != pos; pos = pImData->GetNextServerPos(CHM_INVALID_CHMPXLISTPOS, pos, true, false), sock = CHM_INVALID_SOCK){
		string	name;
		short	port;
		short	ctlport;
		if(!pImData->GetServerBase(pos, name, chmpxid, port, ctlport)){
			ERR_CHMPRN("Could not get serer name/chmpxid/port/ctlport for pos(%" PRIu64 ").", pos);
			continue;
		}
		if(ConnectServer(chmpxid, sock, true)){		// connect & not set epoll & not chmshm
			// connected.
			break;
		}
		MSG_CHMPRN("Could not connect to %s:%d, try to connect next server.", name.c_str(), port);
	}
	if(CHM_INVALID_SOCK == sock){
		MSG_CHMPRN("There is no server to connect port, so it means any server ready up.");
		return true;		// Succeed to udate
	}

	// Send request
	//
	// [NOTE]
	// This method is for initializing chmpx, so do not need to lock socket.
	//
	if(!PxComSendStatusReq(sock, chmpxid)){
		ERR_CHMPRN("Failed to send PXCOM_STATUS_REQ.");
		CloseSSL(sock);
		return false;
	}

	// Receive resnponse
	// 
	// [NOTICE]
	// retry count is sock_retry_count
	// 
	bool	is_closed	= false;
	PCOMPKT	pComPkt		= NULL;
	if(!ChmEventSock::RawReceive(sock, GetSSL(sock), is_closed, &pComPkt, 0L, false, sock_retry_count, sock_wait_time) || !pComPkt){
		ERR_CHMPRN("Failed to receive ComPkt from sock(%d), sock is %s.", sock, is_closed ? "closed" : "not closed");
		CloseSSL(sock);
		CHM_Free(pComPkt);
		return false;
	}

	// Check response type
	if(COM_PX2PX != pComPkt->head.type){
		ERR_CHMPRN("Received data is %s(%" PRIu64 ") type, which does not COM_PX2PX type from sock(%d).", STRCOMTYPE(pComPkt->head.type), pComPkt->head.type, sock);
		CloseSSL(sock);
		CHM_Free(pComPkt);
		return false;
	}
	PPXCOM_ALL	pChmpxCom = CHM_OFFSET(pComPkt, pComPkt->offset, PPXCOM_ALL);
	if(CHMPX_COM_STATUS_RES != pChmpxCom->val_head.type){
		ERR_CHMPRN("Received data is %s(%" PRIu64 ") type, which does not CHMPX_COM_STATUS_RES type from sock(%d).", STRPXCOMTYPE(pChmpxCom->val_head.type), pChmpxCom->val_head.type, sock);
		CloseSSL(sock);
		CHM_Free(pComPkt);
		return false;
	}

	// Check response & Do merge
	PCOMPKT	pResComPkt = NULL;	// tmp
	if(!PxComReceiveStatusRes(&(pComPkt->head), pChmpxCom, &pResComPkt, true)){
		ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
		CloseSSL(sock);
		CHM_Free(pComPkt);
		CHM_Free(pResComPkt);
		return false;
	}
	CloseSSL(sock);
	CHM_Free(pComPkt);
	CHM_Free(pResComPkt);

	return true;
}

bool ChmEventSock::Processing(PCOMPKT pComPkt)
{
	if(!pComPkt){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();		// not used

	if(COM_PX2PX == pComPkt->head.type){
		// check length
		if(pComPkt->length <= sizeof(COMPKT) || 0 == pComPkt->offset){
			ERR_CHMPRN("ComPkt type(%" PRIu64 ":%s) has invalid values.", pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
			return false;
		}
		// following datas
		PPXCOM_ALL	pChmpxCom	= CHM_OFFSET(pComPkt, pComPkt->offset, PPXCOM_ALL);

		if(CHMPX_COM_STATUS_REQ == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveStatusReq(&(pComPkt->head), pChmpxCom, &pResComPkt) || !pResComPkt){
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				CHM_Free(pResComPkt);
				return false;
			}
			// send response
			if(!Send(pResComPkt, NULL, 0L)){
				ERR_CHMPRN("Sending response ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
				CHM_Free(pResComPkt);
				return false;
			}
			CHM_Free(pResComPkt);

		}else if(CHMPX_COM_STATUS_RES == pChmpxCom->val_head.type){
			// processing
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveStatusRes(&(pComPkt->head), pChmpxCom, &pResComPkt, false) || pResComPkt){			// should be pResComPkt=NULL
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				CHM_Free(pResComPkt);
				return false;
			}

			// If this pkt receiver is this server, check status and merging.
			//
			if(is_server_mode && pComPkt->head.term_ids.chmpxid == pImData->GetSelfChmpxId()){
				chmpxsts_t	status = pImData->GetSelfStatus();

				// If change self status after processing, so need to update status to all servers.
				chmpxid_t	to_chmpxid = GetNextRingChmpxId();
				CHMPXSVR	selfchmpxsvr;
				if(!pImData->GetSelfChmpxSvr(&selfchmpxsvr)){
					ERR_CHMPRN("Could not get self chmpx information,");
				}else{
					if(CHM_INVALID_CHMPXID == to_chmpxid){
						// no server found, finish doing so pending hash updated self.
						WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So only sending status update to slaves.");
						if(!PxComSendSlavesStatusChange(&selfchmpxsvr)){
							ERR_CHMPRN("Failed to send self status change to slaves, but continue...");
						}
					}else{
						if(!PxComSendStatusChange(to_chmpxid, &selfchmpxsvr)){
							ERR_CHMPRN("Failed to send self status change, but continue...");
						}
					}
				}

				// When this server is up and after joining ring and the status is changed pending, do merge.
				//
				// [NOTICE]
				// MergeChmpxSvrs() in PxComReceiveStatusRes() can change self status from CHMPXSTS_SRVIN_DOWN_NORMAL or
				// CHMPXSTS_SRVIN_DOWN_DELPENDING to CHMPXSTS_SRVIN_UP_MERGING, if it is neeed.
				// If status is changed, we need to start merging.
				// 
				if(is_auto_merge){
					if(!IS_CHMPXSTS_NOTHING(status)){
						// start merge automatically
						if(CHM_INVALID_CHMPXID == to_chmpxid){
							if(!MergeStart()){
								ERR_CHMPRN("Failed to merge or complete merge for \"server up and automatically merging\".");
								return false;
							}
						}else{
							if(!RequestMergeStart()){
								ERR_CHMPRN("Failed to merge or complete merge for \"server up and automatically merging\".");
								return false;
							}
						}
					}
				}
			}
			CHM_Free(pResComPkt);

		}else if(CHMPX_COM_CONINIT_REQ == pChmpxCom->val_head.type){
			ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured, so exit with no processing.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
			return false;

		}else if(CHMPX_COM_CONINIT_RES == pChmpxCom->val_head.type){
			ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured, so exit with no processing.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
			return false;

		}else if(CHMPX_COM_JOIN_RING == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveJoinRing(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));

				// [NOTICE]
				// Close all socket, it occurres epoll event on the other servers.
				// Because other servers send server down notify, this server do 
				// nothing.
				//
				if(!CloseSelfSocks()){
					ERR_CHMPRN("Failed to close self sock and ctlsock, but continue...");
				}
				if(!CloseFromSlavesSocks()){
					ERR_CHMPRN("Failed to close connection from clients, but continue...");
				}
				if(!CloseToServersSocks()){
					ERR_CHMPRN("Failed to close connection to other servers, but continue...");
				}
				if(!CloseCtlportClientSocks()){
					ERR_CHMPRN("Failed to close control port connection from clients, but continue...");
				}
				return false;
			}
			if(pResComPkt){
				// send(transfer) compkt
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}else{
				// Success to join on RING, do next step(update pending hash)
				MSG_CHMPRN("Succeed to join RING, next step which is updating status and server list runs automatically.");
			}

		}else if(CHMPX_COM_STATUS_UPDATE == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveStatusUpdate(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed Status Update
				// (recoverd status update in receive funcion if possible)
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success Status Update, send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}else{
				// Success Status Update, wait to receive merging...(Nothing to do here)
			}

		}else if(CHMPX_COM_STATUS_CONFIRM == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveStatusConfirm(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed Status Confirm
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			// Success Status Confirm
			if(pResComPkt){
				// and send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}

		}else if(CHMPX_COM_STATUS_CHANGE == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveStatusChange(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success, send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}else{
				// Success
				MSG_CHMPRN("Succeed CHMPXCOM type(%" PRIu64 ":%s).", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
			}

			//
			// If any server chmpx is up, need to check up and connect it on slave mode.
			//
			if(!is_server_mode){
				if(!ConnectServers()){
					ERR_CHMPRN("Something error occurred during connecting server after status changed on slave mode chmpx, but continue...");
				}
			}

		}else if(CHMPX_COM_MERGE_START == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveMergeStart(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success, at first send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}
			// start merging
			//
			// [NOTICE]
			// must do it here after received and transferred "merge start".
			// because pending hash value must not change before receiving "merge start".
			// (MergeStart method sends "status change" in it)
			//
			if(!MergeStart()){
				WAN_CHMPRN("Failed to start merge.");
				return false;
			}

		}else if(CHMPX_COM_MERGE_ABORT == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveMergeAbort(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed Status Confirm
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success merge complete, at first send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}

			// abort merging
			//
			// [NOTICE]
			// must do it here after received and transferred "merge abort".
			// because pending hash value must not change before receiving "merge abort".
			// (MergeComplete method sends "status change" in it)
			//
			if(!MergeAbort()){
				WAN_CHMPRN("Failed to abort merge.");
				return false;
			}

			// [NOTICE]
			// If there are CHMPXSTS_SRVIN_DOWN_DELETED status server, that server can not send
			// "change status" by itself, so that if this server is start server of sending "merge abort",
			// this server send "status change" instead of down server.
			//
			if(pComPkt->head.dept_ids.chmpxid == pImData->GetSelfChmpxId()){
				chmpxid_t	nextchmpxid = GetNextRingChmpxId();
				chmpxid_t	downchmpxid;

				// [NOTICE]
				// Any DOWN status server does not have SUSPEND status.
				//
				while(CHM_INVALID_CHMPXID != (downchmpxid = pImData->GetChmpxIdByStatus(CHMPXSTS_SRVIN_DOWN_DELETED))){
					// status update
					if(!pImData->SetServerStatus(downchmpxid, CHMPXSTS_SRVIN_DOWN_DELPENDING)){
						ERR_CHMPRN("Failed to update status(CHMPXSTS_SRVIN_DOWN_DELPENDING) for down server(0x%016" PRIx64 "), but continue...", downchmpxid);
						continue;
					}
					// send status change
					CHMPXSVR	chmpxsvr;
					if(!pImData->GetChmpxSvr(downchmpxid, &chmpxsvr)){
						ERR_CHMPRN("Could not get chmpx information for down server(0x%016" PRIx64 "), but continue...", downchmpxid);
						continue;
					}
					if(CHM_INVALID_CHMPXID == nextchmpxid){
						if(!PxComSendSlavesStatusChange(&chmpxsvr)){
							ERR_CHMPRN("Failed to send self status change to slaves, but continue...");
						}
					}else{
						if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
							ERR_CHMPRN("Failed to send self status change, but continue...");
						}
					}
				}
			}

		}else if(CHMPX_COM_MERGE_COMPLETE == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveMergeComplete(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success, at first send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}

			// complete merging
			//
			// [NOTICE]
			// must do it here after received and transferred "merge complete".
			// because pending hash value must not change before receiving "merge complete".
			// (MergeComplete method sends "status change" in it)
			//
			if(!MergeComplete()){
				WAN_CHMPRN("Failed to complete merge on this server because of maybe merging now, so wait complete merge after finishing merge");
				return true;
			}

			// [NOTICE]
			// If there are CHMPXSTS_SRVIN_DOWN_DELETED status server, that server can not send
			// "change status" by itself, so that if this server is start server of sending "merge complete",
			// this server send "status change" instead of down server.
			//
			if(pComPkt->head.dept_ids.chmpxid == pImData->GetSelfChmpxId()){
				chmpxid_t	nextchmpxid = pImData->GetNextRingChmpxId();
				chmpxid_t	downchmpxid;

				// [NOTICE]
				// Any DOWN status server does not have SUSPEND status.
				//
				while(CHM_INVALID_CHMPXID != (downchmpxid = pImData->GetChmpxIdByStatus(CHMPXSTS_SRVIN_DOWN_DELETED))){
					// hash & status update
					if(!pImData->SetServerBaseHash(downchmpxid, CHM_INVALID_HASHVAL)){
						ERR_CHMPRN("Failed to update base hash(CHM_INVALID_HASHVAL) for down server(0x%016" PRIx64 "), but continue...", downchmpxid);
						continue;
					}
					if(!pImData->SetServerStatus(downchmpxid, CHMPXSTS_SRVOUT_DOWN_NORMAL)){
						ERR_CHMPRN("Failed to update status(CHMPXSTS_SRVOUT_DOWN_NORMAL) for down server(0x%016" PRIx64 "), but continue...", downchmpxid);
						continue;
					}
					// send status change
					CHMPXSVR	chmpxsvr;
					if(!pImData->GetChmpxSvr(downchmpxid, &chmpxsvr)){
						ERR_CHMPRN("Could not get chmpx information for down server(0x%016" PRIx64 "), but continue...", downchmpxid);
						continue;
					}
					if(CHM_INVALID_CHMPXID == nextchmpxid){
						if(!PxComSendSlavesStatusChange(&chmpxsvr)){
							ERR_CHMPRN("Failed to send self status change to slaves, but continue...");
						}
					}else{
						if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
							ERR_CHMPRN("Failed to send self status change, but continue...");
						}
					}
				}
			}

		}else if(CHMPX_COM_SERVER_DOWN == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveServerDown(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed Server Down(could not recover...)
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success Server Down, send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}

			// [NOTICE]
			// The status is alreay updated, because probably received "merge abort" before receiving "SERVER_DOWN",
			// so status changed at that time.
			// Thus do not care about status here.
			//

		}else if(CHMPX_COM_REQ_UPDATEDATA == pChmpxCom->val_head.type){
			PCOMPKT	pResComPkt = NULL;
			if(!PxComReceiveReqUpdateData(&(pComPkt->head), pChmpxCom, &pResComPkt)){
				// Failed request update data
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}
			if(pResComPkt){
				// Success Server Down, send(transfer) compkt to next server
				if(!Send(pResComPkt, NULL, 0L)){
					ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s) against ComPkt type(%" PRIu64 ":%s), Something error occured.", pResComPkt->head.type, STRCOMTYPE(pResComPkt->head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
					CHM_Free(pResComPkt);
					return false;
				}
				CHM_Free(pResComPkt);
			}

		}else if(CHMPX_COM_RES_UPDATEDATA == pChmpxCom->val_head.type){
			// only trans
			if(!PxComReceiveResUpdateData(&(pComPkt->head), pChmpxCom)){
				// Failed set update data
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}

		}else if(CHMPX_COM_RESULT_UPDATEDATA == pChmpxCom->val_head.type){
			// only trans
			if(!PxComReceiveResultUpdateData(&(pComPkt->head), pChmpxCom)){
				// Failed receive result of update data
				ERR_CHMPRN("Received CHMPXCOM type(%" PRIu64 ":%s). Something error occured.", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type));
				return false;
			}

		}else{
			ERR_CHMPRN("Could not handle ChmpxCom type(%" PRIu64 ":%s) in ComPkt type(%" PRIu64 ":%s).", pChmpxCom->val_head.type, STRPXCOMTYPE(pChmpxCom->val_head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
			return false;
		}

	}else if(COM_C2C == pComPkt->head.type){
		// This case is received MQ data to trans that to Socket.
		// (The other case is error.)
		//
		chmpxid_t	selfchmpxid = pImData->GetSelfChmpxId();
		if(pComPkt->head.dept_ids.chmpxid != selfchmpxid){
			ERR_CHMPRN("Why does not same chmpxid? COMPKT is received from socket, it should do processing MQ event object.");
			return false;
		}
		DUMPCOM_COMPKT("Sock::Processing(COM_C2C)", pComPkt);

		// [NOTICE]
		// Come here, following patturn:
		//
		// 1) client -> MQ -> chmpx(server) -> chmpx(slave)
		// 2) client -> MQ -> chmpx(slave)  -> chmpx(server)
		//
		// Case 1) Must be set end point of chmpxid and msgid
		// Case 2) Set or not set end point of chmpxid and msgid
		//         If not set these, decide these here.
		//
		// And both type end point must be connected.
		//

		// set deliver head in COMPKT
		//
		chmpxid_t		org_chmpxid = pComPkt->head.term_ids.chmpxid;	// backup
		chmpxidlist_t	ex_chmpxids;
		if(CHM_INVALID_CHMPXID == pComPkt->head.term_ids.chmpxid){
			long	ex_chmpxcnt = pImData->GetReceiverChmpxids(pComPkt->head.hash, pComPkt->head.c2ctype, ex_chmpxids);
			if(0 > ex_chmpxcnt){
				ERR_CHMPRN("Failed to get target chmpxids by something error occurred.");
				return false;
			}else if(0 == ex_chmpxcnt){
				WAN_CHMPRN("There are no target chmpxids, but method returns with success.");
				return true;
			}
		}else{
			ex_chmpxids.push_back(pComPkt->head.term_ids.chmpxid);
		}

		// Send
		chmpxid_t	tmpchmpxid;
		while(0 < ex_chmpxids.size()){
			tmpchmpxid = ex_chmpxids.front();

			PCOMPKT	pTmpPkt;
			bool	is_duplicate;
			if(1 < ex_chmpxids.size()){
				if(NULL == (pTmpPkt = ChmEventSock::DuplicateComPkt(pComPkt))){
					ERR_CHMPRN("Could not duplicate COMPKT.");
					return false;
				}
				is_duplicate = true;
			}else{
				pTmpPkt		 = pComPkt;
				is_duplicate = false;
			}

			pTmpPkt->head.mq_serial_num		= MIN_MQ_SERIAL_NUMBER;
			pTmpPkt->head.peer_dept_msgid	= CHM_INVALID_MSGID;
			pTmpPkt->head.peer_term_msgid	= CHM_INVALID_MSGID;
			pTmpPkt->head.term_ids.chmpxid	= tmpchmpxid;

			if(CHM_INVALID_CHMPXID == org_chmpxid || org_chmpxid != pTmpPkt->head.term_ids.chmpxid){
				// this case is not replying, so reset terminated msgid.
				pTmpPkt->head.term_ids.msgid= CHM_INVALID_MSGID;
			}
			ex_chmpxids.pop_front();

			// check connect
			if(is_server_mode){
				if(!pImData->IsConnectSlave(pTmpPkt->head.term_ids.chmpxid)){
					// there is no socket as slave connection for term chmpx.
					// if term chmpx is slave node, abort this processing.
					// (if server node, try to connect to it in Send() method.)
					if(!pImData->IsServerChmpxId(pTmpPkt->head.term_ids.chmpxid)){
						ERR_CHMPRN("Could not find slave socket for slave chmpx(0x%016" PRIx64 ").", pTmpPkt->head.term_ids.chmpxid);
						if(is_duplicate){
							CHM_Free(pTmpPkt);
						}
						return false;
					}
					MSG_CHMPRN("Could not find slave socket for server chmpx(0x%016" PRIx64 "), thus try to connect chmpx(server mode).", pTmpPkt->head.term_ids.chmpxid);

					// [NOTE]
					// if server node and deperture and terminate chmpx id are same,
					// need to send this packet to MQ directly.
					// and if do not send packet on this case, drop it here.
					//
					if(tmpchmpxid == selfchmpxid){
						if(IS_C2CTYPE_SELF(pComPkt->head.c2ctype)){
							bool	selfresult = false;
							if(pChmCntrl){
								selfresult = pChmCntrl->Processing(pTmpPkt, ChmCntrl::EVOBJ_TYPE_EVSOCK);
							}
							if(is_duplicate){
								CHM_Free(pTmpPkt);
							}
							if(!selfresult){
								ERR_CHMPRN("Failed processing(sending packet to self) to MQ directly.");
								return false;
							}
						}
						continue;
					}
				}
			}else{
				if(!pImData->IsConnectServer(pTmpPkt->head.term_ids.chmpxid)){
					// try to connect all servers
					ConnectServers();					//  not need to check error.
				}
			}

			if(!Send(pTmpPkt, NULL, 0L)){
				// decode for error message
				ChmEventSock::ntoh(pTmpPkt);
				ERR_CHMPRN("Sending ComPkt type(%" PRIu64 ":%s), Something error occured.", pTmpPkt->head.type, STRCOMTYPE(pTmpPkt->head.type));
				if(is_duplicate){
					CHM_Free(pTmpPkt);
				}
				return false;
			}
			if(is_duplicate){
				CHM_Free(pTmpPkt);
			}
		}

	}else if(COM_C2PX == pComPkt->head.type){
		// This case is received MQ data to chmpx process(PX)
		// (The other case is error.)
		//
		// check length
		if(pComPkt->length <= sizeof(COMPKT) || 0 == pComPkt->offset){
			ERR_CHMPRN("ComPkt type(%" PRIu64 ":%s) has invalid values.", pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
			return false;
		}
		// following datas
		PPXCLT_ALL	pPxCltCom	= CVT_CLT_ALL_PTR_PXCOMPKT(pComPkt);

		if(CHMPX_CLT_JOIN_NOTIFY == pPxCltCom->val_head.type){
			if(!PxCltReceiveJoinNotify(&(pComPkt->head), pPxCltCom)){
				// Failed Join Notify
				ERR_CHMPRN("Received PXCLTCOM type(%" PRIu64 ":%s). Something error occured.", pPxCltCom->val_head.type, STRPXCLTTYPE(pPxCltCom->val_head.type));
				return false;
			}
		}else{
			ERR_CHMPRN("Could not handle PxCltCom type(%" PRIu64 ":%s) in ComPkt type(%" PRIu64 ":%s).", pPxCltCom->val_head.type, STRPXCOMTYPE(pPxCltCom->val_head.type), pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
			return false;
		}

	}else{
		ERR_CHMPRN("Could not handle ComPkt type(%" PRIu64 ":%s).", pComPkt->head.type, STRCOMTYPE(pComPkt->head.type));
		return false;
	}
	return true;
}

bool ChmEventSock::Processing(int sock, const char* pCommand)
{
	if(CHM_INVALID_SOCK == sock || !pCommand){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	strlst_t	cmdarray;
	if(!str_paeser(pCommand, cmdarray) || 0 == cmdarray.size()){
		ERR_CHMPRN("Something wrong %s command, because could not parse it.", pCommand);
		return false;
	}
	string	strCommand = cmdarray.front();
	cmdarray.pop_front();

	string	strResponse;

	if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_DUMP_IMDATA)){
		// Dump
		stringstream	sstream;
		pImData->Dump(sstream);
		strResponse = sstream.str();

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_START_MERGE)){
		// Start Merge
		//
		// The merging flow is sending CHMPX_COM_STATUS_CONFIRM, and sending CHMPX_COM_MERGE_START
		// after returning CHMPX_COM_STATUS_CONFIRM result on RING. After getting CHMPX_COM_MERGE_START
		// start to merge for self.
		//
		if(!CtlComMergeStart(strResponse)){
			ERR_CHMPRN("CTL_COMMAND_START_MERGE is failed, so stop merging.");
		}else{
			MSG_CHMPRN("CTL_COMMAND_START_MERGE is succeed, so do next step after reciveing result.");
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_STOP_MERGE)){
		// Abort Merge
		if(!CtlComMergeAbort(strResponse)){
			ERR_CHMPRN("CTL_COMMAND_COMPLETE_MERGE is failed, so stop merging.");
		}else{
			MSG_CHMPRN("CTL_COMMAND_COMPLETE_MERGE is succeed, so do next step after reciveing result.");
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_COMPLETE_MERGE)){
		// Complete Merge
		if(!CtlComMergeComplete(strResponse)){
			ERR_CHMPRN("CTL_COMMAND_COMPLETE_MERGE is failed, so stop merging.");
		}else{
			MSG_CHMPRN("CTL_COMMAND_COMPLETE_MERGE is succeed, so do next step after receiving result.");
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_SERVICE_IN)){
		// Service IN
		//
		if(!CtlComServiceIn(strResponse)){
			ERR_CHMPRN("CTL_COMMAND_SERVICE_IN is failed.");
		}else{
			MSG_CHMPRN("CTL_COMMAND_SERVICE_IN is succeed.");
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_SERVICE_OUT)){
		// Service OUT(status is changed to delete pending)
		//
		if(0 == cmdarray.size()){
			ERR_CHMPRN("%s command must have parameter for server name/port.", strCommand.c_str());
			strResponse = CTL_RES_ERROR_SERVICE_OUT_PARAM;
		}else{
			string		strTmp = cmdarray.front();
			strlst_t	paramarray;
			if(!str_paeser(strTmp.c_str(), paramarray, ":") || 2 != paramarray.size()){
				ERR_CHMPRN("%s command parameter(%s) must be servername:port.", strCommand.c_str(), strTmp.c_str());
				strResponse = CTL_RES_ERROR_SERVICE_OUT_PARAM;
			}else{
				// retriving the server which is set SERVICE OUT on RING
				string	strServer	= paramarray.front();	paramarray.pop_front();
				string	strCtlPort	= paramarray.front();
				short	ctlport		= static_cast<short>(atoi(strCtlPort.c_str()));
				if(!CtlComServiceOut(strServer.c_str(), ctlport, pCommand, strResponse)){
					ERR_CHMPRN("CTL_COMMAND_SERVICE_OUT is failed.");
				}else{
					MSG_CHMPRN("CTL_COMMAND_SERVICE_OUT is succeed.");
				}
			}
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_SELF_STATUS)){
		// Print self status
		//
		if(!CtlComSelfStatus(strResponse)){
			ERR_CHMPRN("CTL_COMMAND_SELF_STATUS is failed.");
		}else{
			MSG_CHMPRN("CTL_COMMAND_SELF_STATUS is succeed.");
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_ALLSVR_STATUS)){
		// Print all server status
		//
		if(!CtlComAllServerStatus(strResponse)){
			ERR_CHMPRN("CTL_COMMAND_SELF_STATUS is failed.");
		}else{
			MSG_CHMPRN("CTL_COMMAND_SELF_STATUS is succeed.");
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_TRACE_SET)){
		// Trace dis/enable
		//
		if(0 == cmdarray.size()){
			ERR_CHMPRN("%s command must have parameter for enable/disable.", strCommand.c_str());
			strResponse = CTL_RES_ERROR_TRACE_SET_PARAM;
		}else{
			string	strTmp = cmdarray.front();
			bool	enable = false;
			if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_SET_ENABLE1) || 0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_SET_ENABLE2) || 0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_SET_ENABLE3)){
				enable = true;
			}else if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_SET_DISABLE1) || 0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_SET_DISABLE2) || 0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_SET_DISABLE3)){
				enable = false;
			}else{
				ERR_CHMPRN("%s command parameter(%s) must be enable or disable.", strCommand.c_str(), strTmp.c_str());
				strResponse = CTL_RES_ERROR_TRACE_SET_PARAM;
			}

			if(!CtlComAllTraceSet(strResponse, enable)){
				ERR_CHMPRN("CTL_COMMAND_TRACE_SET is failed.");
			}else{
				MSG_CHMPRN("CTL_COMMAND_TRACE_SET is succeed.");
			}
		}

	}else if(0 == strcasecmp(strCommand.c_str(), CTL_COMMAND_TRACE_VIEW)){
		// Print Trace log
		//
		long	view_count = pImData->GetTraceCount();

		if(0 == view_count){
			ERR_CHMPRN("Now trace count is 0.");
			strResponse = CTL_RES_ERROR_TRACE_VIEW_NOTRACE;
		}else{
			bool		isError	= false;
			long		count	= view_count;
			logtype_t	dirmask = CHMLOG_TYPE_UNKOWN;
			logtype_t	devmask = CHMLOG_TYPE_UNKOWN;

			if(0 == cmdarray.size()){
				MSG_CHMPRN("%s command does not have parameter, so use default value(dir=all, dev=all, count=all trace count).", strCommand.c_str());
			}else{
				for(string strTmp = ""; 0 < cmdarray.size(); cmdarray.pop_front()){
					strTmp = cmdarray.front();

					if(0 == strTmp.find(CTL_COMMAND_TRACE_VIEW_DIR)){
						strTmp = strTmp.substr(strlen(CTL_COMMAND_TRACE_VIEW_DIR));

						if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_VIEW_IN)){
							dirmask |= CHMLOG_TYPE_INPUT;
						}else if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_VIEW_OUT)){
							dirmask |= CHMLOG_TYPE_OUTPUT;
						}else if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_VIEW_ALL)){
							dirmask |= (CHMLOG_TYPE_INPUT | CHMLOG_TYPE_OUTPUT);
						}else{
							ERR_CHMPRN("DIR parameter %s does not defined.", strTmp.c_str());
							isError	= true;
							break;
						}
					}else if(0 == strTmp.find(CTL_COMMAND_TRACE_VIEW_DEV)){
						strTmp = strTmp.substr(strlen(CTL_COMMAND_TRACE_VIEW_DEV));

						if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_VIEW_SOCK)){
							devmask |= CHMLOG_TYPE_SOCKET;
						}else if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_VIEW_MQ)){
							devmask |= CHMLOG_TYPE_MQ;
						}else if(0 == strcasecmp(strTmp.c_str(), CTL_COMMAND_TRACE_VIEW_ALL)){
							devmask |= (CHMLOG_TYPE_SOCKET | CHMLOG_TYPE_MQ);
						}else{
							ERR_CHMPRN("DEV parameter %s does not defined.", strTmp.c_str());
							isError	= true;
							break;
						}
					}else{
						count = static_cast<long>(atoi(strTmp.c_str()));
						if(0 == count || view_count < count){
							ERR_CHMPRN("view count %ld is %s.", count, (0 == count ? "0" : "over trace maximum count"));
							isError	= true;
							break;
						}
					}
				}
			}
			if(isError){
				strResponse = CTL_RES_ERROR_TRACE_VIEW_PARAM;
			}else{
				if(0 == (dirmask & CHMLOG_MASK_DIRECTION)){
					dirmask |= (CHMLOG_TYPE_OUTPUT | CHMLOG_TYPE_INPUT);
				}
				if(0 == (devmask & CHMLOG_MASK_DEVICE)){
					devmask |= (CHMLOG_TYPE_SOCKET | CHMLOG_TYPE_MQ);
				}

				if(!CtlComAllTraceView(strResponse, dirmask, devmask, count)){
					ERR_CHMPRN("CTL_COMMAND_TRACE_VIEW is failed.");
				}else{
					MSG_CHMPRN("CTL_COMMAND_TRACE_VIEW is succeed.");
				}
			}
		}

	}else{
		// error
		ERR_CHMPRN("Unknown command(%s).", strCommand.c_str());
		strResponse  = "Unknown command(";
		strResponse += strCommand;
		strResponse += ")\n";
	}

	// send
	//
	// [NOTE] This socket is control port socket.
	//
	return ChmEventSock::LockedSend(sock, GetSSL(sock), reinterpret_cast<const unsigned char*>(strResponse.c_str()), strResponse.length());
}

bool ChmEventSock::ChangeStatusBeforeMergeStart(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Check status
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(!IS_CHMPXSTS_SRVIN(status)){
		// [NOTE]
		// For auto merging
		// local status value is changed SERVICEIN/UP/ADD/PENDING/NOSUSPEND here, when server is SERVICEOUT/UP/NOACT(ADD)/NOTHING(ANY)/NOSUSPEND.
		//
		if(is_auto_merge && IS_CHMPXSTS_SRVOUT(status) && IS_CHMPXSTS_UP(status) && !IS_CHMPXSTS_DELETE(status) && IS_CHMPXSTS_NOSUP(status)){
			SET_CHMPXSTS_SRVIN(status);
			SET_CHMPXSTS_ADD(status);
			SET_CHMPXSTS_PENDING(status);
		}else{
			MSG_CHMPRN("Server status(0x%016" PRIx64 ":%s) is not CHMPXSTS_VAL_SRVIN, so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
			return false;
		}
	}
	if(!IS_CHMPXSTS_PENDING(status)){
		if(IS_CHMPXSTS_DOING(status) || IS_CHMPXSTS_DONE(status)){
			return true;
		}
		if(!IS_CHMPXSTS_NOACT(status)){
			MSG_CHMPRN("Already not pending/doing/done status(0x%016" PRIx64 ":%s), so not change status and nothing to do.", status, STR_CHMPXSTS_FULL(status).c_str());
			return false;
		}

		// check whichever this server is needed to merge, it can check by hash values.
		chmhash_t	base_hash		= CHM_INVALID_HASHVAL;
		chmhash_t	pending_hash	= CHM_INVALID_HASHVAL;
		chmhash_t	max_base_hash	= CHM_INVALID_HASHVAL;
		chmhash_t	max_pending_hash= CHM_INVALID_HASHVAL;
		if(!pImData->GetSelfHash(base_hash, pending_hash) || !pImData->GetMaxHashCount(max_base_hash, max_pending_hash)){
			ERR_CHMPRN("Could not get base/pending hash value and those max value.");
			return false;
		}
		if(base_hash == pending_hash && max_base_hash == max_pending_hash){
			MSG_CHMPRN("this server has status(0x%016" PRIx64 ":%s) and base/pending hash(0x%016" PRIx64 ") is same, so nothing to do.", status, STR_CHMPXSTS_FULL(status).c_str(), base_hash);
			return false;
		}
		SET_CHMPXSTS_PENDING(status);
	}
	if(IS_CHMPXSTS_ADD(status) && IS_CHMPXSTS_SUSPEND(status)){
		ERR_CHMPRN("Server status(0x%016" PRIx64 ":%s) is SUSPEND(on ADD), so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// new status
	if(IS_CHMPXSTS_NOACT(status)){
		if(!IS_CHMPXSTS_SUSPEND(status)){
			status = CHMPXSTS_SRVIN_UP_MERGING;
		}else{
			status = CHMPXSTS_SRVIN_UP_MERGED;
		}
	}else if(IS_CHMPXSTS_ADD(status)){
		if(!IS_CHMPXSTS_SUSPEND(status)){
			status = CHMPXSTS_SRVIN_UP_ADDING;
		}else{
			status = CHMPXSTS_SRVIN_UP_ADDED;
		}
	}else if(IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_DELETE(status)){
		if(!IS_CHMPXSTS_SUSPEND(status)){
			status = CHMPXSTS_SRVIN_UP_DELETING;
		}else{
			status = CHMPXSTS_SRVIN_UP_DELETED;
		}
	}else if(IS_CHMPXSTS_DOWN(status) && IS_CHMPXSTS_DELETE(status)){
		status = CHMPXSTS_SRVIN_DOWN_DELETED;
	}else{	// why?
		ERR_CHMPRN("Un-safe status(0x%016" PRIx64 ":%s).", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// set new status(set suspend in this method)
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to change self status to 0x%016" PRIx64 ":%s", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// send status update
	// 
	// If error occured in sending status change, but continue to merge.
	// 
	chmpxid_t	to_chmpxid = GetNextRingChmpxId();
	CHMPXSVR	selfchmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&selfchmpxsvr)){
		ERR_CHMPRN("Could not get self chmpx information,");
		return false;
	}
	if(CHM_INVALID_CHMPXID == to_chmpxid){
		// no server found, finish doing so pending hash updated self.
		WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So only sending status update to slaves.");

		if(!PxComSendSlavesStatusChange(&selfchmpxsvr)){
			ERR_CHMPRN("Failed to send self status change to slaves, but continue...");
		}
	}else{
		if(!PxComSendStatusChange(to_chmpxid, &selfchmpxsvr)){
			ERR_CHMPRN("Failed to send self status change, but continue...");
		}
	}
	return true;
}

// [NOTE]
// If there are down servers, this server changes down server's status and
// send it on behalf of down server before starting merging.
//
bool ChmEventSock::ChangeDownSvrStatusBeforeMerge(CHMDOWNSVRMERGE type)
{
	if(CHM_DOWNSVR_MERGE_START != type && CHM_DOWNSVR_MERGE_COMPLETE != type && CHM_DOWNSVR_MERGE_ABORT != type){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// get all server status
	PCHMPXSVR	pchmpxsvrs	= NULL;
	long		count		= 0L;
	if(!pImData->GetChmpxSvrs(&pchmpxsvrs, count)){
		ERR_CHMPRN("Could not get all server status, so stop update status.");
		return false;
	}

	chmpxid_t	to_chmpxid = GetNextRingChmpxId();

	// loop: check down servers and update it's status.
	for(long cnt = 0; cnt < count; ++cnt){
		// check status:	merge start		-> SERVICEIN / DOWN / DELETE / NOTHING or PENDING
		// 					merge complete	-> SERVICEIN / DOWN / DELETE / ANYTHING
		//					merge abort		-> SERVICEIN / DOWN / DELETE / DOING or DONE
		if(	(CHM_DOWNSVR_MERGE_START == type && IS_CHMPXSTS_SRVIN(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_DOWN(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_DELETE(pchmpxsvrs[cnt].status) && (!IS_CHMPXSTS_DOING(pchmpxsvrs[cnt].status) && !IS_CHMPXSTS_DONE(pchmpxsvrs[cnt].status))) ||
			(CHM_DOWNSVR_MERGE_COMPLETE == type && IS_CHMPXSTS_SRVIN(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_DOWN(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_DELETE(pchmpxsvrs[cnt].status)) ||
			(CHM_DOWNSVR_MERGE_ABORT == type && IS_CHMPXSTS_SRVIN(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_DOWN(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_DELETE(pchmpxsvrs[cnt].status) && (IS_CHMPXSTS_DOING(pchmpxsvrs[cnt].status) || IS_CHMPXSTS_DONE(pchmpxsvrs[cnt].status))) )
		{
			// set new status:	merge start		-> SERVICEIN / DOWN / DELETE / DONE
			//					merge complete	-> SERVICEOUT / DOWN / NOACT / NOTHING
			//					merge abort		-> SERVICEIN / DOWN / DELETE / PENDING
			chmpxsts_t	newstatus = CHM_DOWNSVR_MERGE_START == type ? CHMPXSTS_SRVIN_DOWN_DELETED : CHM_DOWNSVR_MERGE_COMPLETE == type ? CHMPXSTS_SRVOUT_DOWN_NORMAL : CHMPXSTS_SRVIN_DOWN_DELPENDING;

			// set new status and hash
			if(!pImData->SetServerStatus(pchmpxsvrs[cnt].chmpxid, newstatus) || (CHM_DOWNSVR_MERGE_COMPLETE == type && !pImData->SetServerBaseHash(pchmpxsvrs[cnt].chmpxid, CHM_INVALID_HASHVAL))){
				ERR_CHMPRN("Could not change server(0x%016" PRIx64 ") status to 0x%016" PRIx64 ":%s.", pchmpxsvrs[cnt].chmpxid, newstatus, STR_CHMPXSTS_FULL(newstatus).c_str());

			}else{
				// reget status
				CHMPXSVR	chmpxsvr;
				if(!pImData->GetChmpxSvr(pchmpxsvrs[cnt].chmpxid, &chmpxsvr)){
					ERR_CHMPRN("Could not get server(0x%016" PRIx64 ") information.", pchmpxsvrs[cnt].chmpxid);

				}else{
					// send status update
					if(CHM_INVALID_CHMPXID == to_chmpxid){
						// no server found, finish doing so pending hash updated self.
						WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So only sending status update to slaves.");
						if(!PxComSendSlavesStatusChange(&chmpxsvr)){
							ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status to slaves, but continue...", pchmpxsvrs[cnt].chmpxid);
						}
					}else{
						if(!PxComSendStatusChange(to_chmpxid, &chmpxsvr)){
							ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status to servers, but continue...", pchmpxsvrs[cnt].chmpxid);
						}
					}
				}
			}
		}
	}
	CHM_Free(pchmpxsvrs);

	return true;
}

//
// CAREFUL
// This method is called from another thread for merging worker.
//
bool ChmEventSock::MergeDone(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Check status
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(!IS_CHMPXSTS_SRVIN(status)){
		ERR_CHMPRN("Server status(0x%016" PRIx64 ":%s) is not CHMPXSTS_VAL_SRVIN, so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}
	if(!IS_CHMPXSTS_DOING(status)){
		WAN_CHMPRN("Already status(0x%016" PRIx64 ":%s) is not \"DOING\", so not change status and nothing to do.", status, STR_CHMPXSTS_FULL(status).c_str());
		if(IS_CHMPXSTS_DONE(status)){
			return true;
		}
		return false;
	}
	if((IS_CHMPXSTS_NOACT(status) || IS_CHMPXSTS_ADD(status)) && IS_CHMPXSTS_SUSPEND(status)){
		ERR_CHMPRN("Server status(0x%016" PRIx64 ":%s) is SUSPEND(not on DELETE), so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// new status
	if(IS_CHMPXSTS_NOACT(status)){
		status = CHMPXSTS_SRVIN_UP_MERGED;
	}else if(IS_CHMPXSTS_ADD(status)){
		status = CHMPXSTS_SRVIN_UP_ADDED;
	}else if(IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_DELETE(status)){
		status = CHMPXSTS_SRVIN_UP_DELETED;
	}else if(IS_CHMPXSTS_DOWN(status) && IS_CHMPXSTS_DELETE(status)){
		status = CHMPXSTS_SRVIN_DOWN_DELETED;		// No case come here.
	}else{	// why?
		ERR_CHMPRN("Un-safe status(0x%016" PRIx64 ":%s).", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// set new status(set suspend in this method)
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to change self status to 0x%016" PRIx64 ":%s", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// send status change
	chmpxid_t	to_chmpxid = GetNextRingChmpxId();
	CHMPXSVR	selfchmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&selfchmpxsvr)){
		WAN_CHMPRN("Could not get self chmpx information, but continue...");
	}else{
		if(CHM_INVALID_CHMPXID == to_chmpxid){
			// no server found, finish doing so pending hash updated self.
			WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So only sending status update to slaves.");

			if(!PxComSendSlavesStatusChange(&selfchmpxsvr)){
				WAN_CHMPRN("Failed to send self status change to slaves, but continue...");
			}
		}else{
			// Update pending hash.
			if(!pImData->UpdatePendingHash()){
				ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
				return false;
			}
			// push status changed
			if(!PxComSendStatusChange(to_chmpxid, &selfchmpxsvr)){
				WAN_CHMPRN("Failed to send self status change, but continue...");
			}
		}
	}

	// if the mode for merge is automatical, do complete here.
	if(is_auto_merge){
		if(!RequestMergeComplete()){
			ERR_CHMPRN("Could not change status merge \"COMPLETE\", probabry another server does not change status yet, DO COMPMERGE BY MANUAL!");
		}
	}
	return true;
}

bool ChmEventSock::IsSuspendServerInRing(void) const
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// get all server status
	PCHMPXSVR	pchmpxsvrs	= NULL;
	long		count		= 0L;
	if(!pImData->GetChmpxSvrs(&pchmpxsvrs, count)){
		ERR_CHMPRN("Could not get all server status, so stop update status.");
		return false;
	}

	// loop: check wrong status
	bool	result = true;
	for(long cnt = 0; cnt < count; ++cnt){
		if(IS_CHMPXSTS_SRVIN(pchmpxsvrs[cnt].status)){
			if(IS_CHMPXSTS_NOACT(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_NOSUP(pchmpxsvrs[cnt].status)){
				result = false;
			}else if(IS_CHMPXSTS_ADD(pchmpxsvrs[cnt].status) && IS_CHMPXSTS_NOSUP(pchmpxsvrs[cnt].status)){
				result = false;
			}
			// If DELETE action, do not care for suspend status.
		}
		// If SERVICEOUT, do not care for suspend status.
	}
	CHM_Free(pchmpxsvrs);

	if(!result){
		MSG_CHMPRN("Found SERVICEIN & SUSPEND server(not have clinet, but join RING), so could not start to merge.");
	}
	return result;
}

//
// This method sends "STATUS_COMFIRM", and After receiving "STATUS_COMFIRM", 
// sends "MERGE_START" automatically.
//
bool ChmEventSock::RequestMergeStart(string* pstring)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		if(pstring){
			*pstring = CTL_RES_INT_ERROR;
		}
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// check SUSPEND status
	if(IsSuspendServerInRing()){
		ERR_CHMPRN("Some server in RING have SUSPEND status, so could not change status.");
		if(pstring){
			*pstring = CTL_RES_ERROR_SOME_SERVER_SUSPEND;
		}
		return false;
	}

	// get terminated chmpxid
	chmpxid_t	chmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == chmpxid){
		// no server found, finish doing so pending hash updated self.
		WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So stop sending status update.");

		// Start to merge(only self on RING)
		//
		// If there is no server in RING, do start merge.
		//
		if(!MergeStart()){
			ERR_CHMPRN("Failed to start merge.");
			if(pstring){
				*pstring = CTL_RES_ERROR_MERGE_START;
			}
			return false;
		}else{
			if(pstring){
				*pstring = CTL_RES_SUCCESS_NOSERVER;
			}
			return true;
		}
	}

	// get all server status
	PCHMPXSVR	pchmpxsvrs	= NULL;
	long		count		= 0L;
	if(!pImData->GetChmpxSvrs(&pchmpxsvrs, count)){
		ERR_CHMPRN("Could not get all server status, so stop update status.");
		if(pstring){
			*pstring = CTL_RES_INT_ERROR_NOTGETCHMPX;
		}
		return false;
	}

	// send status_confirm
	if(!PxComSendStatusConfirm(chmpxid, pchmpxsvrs, count)){
		ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_CONFIRM.");
		if(pstring){
			*pstring = CTL_RES_ERROR_COMMUNICATION;
		}
		return false;
	}
	if(pstring){
		*pstring = CTL_RES_SUCCESS;
	}
	return true;
}

bool ChmEventSock::MergeStart(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}

	// change status "DOING" before merging.
	if(!ChangeStatusBeforeMergeStart()){
		MSG_CHMPRN("Failed to change status \"DOING\", probabry nothing to do.");
		return true;
	}
	// change down server status before merging.
	if(!ChangeDownSvrStatusBeforeMerge(CHM_DOWNSVR_MERGE_START)){
		ERR_CHMPRN("Failed to change down server status, but continue...");
	}

	// start merging.
	if(is_do_merge){
		if(is_run_merge){
			ERR_CHMPRN("Already to run merge.");
			return false;
		}

		if(!mergethread.HasThread()){
			ERR_CHMPRN("The thread for merging is not running, why?");
			return false;
		}

		// get chmpxids(targets) which have same basehash or replica basehash.
		ChmIMData*	pImData = pChmCntrl->GetImDataObj();
		chmhash_t	basehash;
		if(!pImData->GetSelfBaseHash(basehash)){
			// there is no server, so set status "DONE" here.
			if(!MergeDone()){
				ERR_CHMPRN("Failed to change status \"DOING to DONE to COMPLETE\".");
				return false;
			}
			return true;
		}
		// we need to collect minimum servers
		chmpxidlist_t	alllist;
		if(pImData->IsPendingExchangeData()){
			// need all servers which are up/noact/nosuspend
			pImData->GetServerChmpxIdForMerge(alllist);
		}else{
			// this case is suspend to nosuspend or down to up(servicein)
			pImData->GetServerChmpxIdByBaseHash(basehash, alllist);
		}

		while(!fullock::flck_trylock_noshared_mutex(&mergeidmap_lockval));	// LOCK

		// make chmpxid map for update datas
		chmpxid_t	selfchmpxid = pImData->GetSelfChmpxId();
		mergeidmap.clear();
		for(chmpxidlist_t::iterator iter = alllist.begin(); iter != alllist.end(); ++iter){
			if(selfchmpxid != *iter){
				mergeidmap[*iter] = CHMPX_COM_REQ_UPDATE_INIVAL;
			}
		}
		if(0 == mergeidmap.size()){
			// there is no server, so set status "DONE" here.
			fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);		// UNLOCK

			if(!MergeDone()){
				ERR_CHMPRN("Failed to change status \"DOING to DONE to COMPLETE\".");
				return false;
			}
			return true;
		}
		fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);			// UNLOCK

		// set "run" status for merging worker method.
		is_run_merge = true;

		// run merging worker thread method.
		if(!mergethread.DoWorkThread()){
			ERR_CHMPRN("Failed to wake up thread for merging.");
			is_run_merge = false;
			return false;
		}
	}else{
		// Do not run merging, so set status "DONE" here.
		//
		if(!MergeDone()){
			ERR_CHMPRN("Failed to change status \"DONE\".");
			return false;
		}
	}
	return true;
}

bool ChmEventSock::MergeAbort(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// status check
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(!IS_CHMPXSTS_SRVIN(status) || (!IS_CHMPXSTS_DOING(status) && !IS_CHMPXSTS_DONE(status))){
		// [NOTICE]
		// CHMPXSTS_SRVIN_UP_MERGING & CHMPXSTS_SRVIN_UP_MERGED could not abort.
		// Only doing merge complete or merging on no-suspend
		//
		MSG_CHMPRN("Now status is status(0x%016" PRIx64 ":%s) which is not \"DOING\" nor \"DONE\", so nothing to do.", status, STR_CHMPXSTS_FULL(status).c_str());
		return true;
	}

	if(is_do_merge){
		if(!mergethread.HasThread()){
			ERR_CHMPRN("The thread for merging is not running, why? but continue...");
		}else{
			// set "stop" status for merging worker method.
			is_run_merge = false;
		}
	}

	// send abort update data to all client process
	if(!pChmCntrl->MergeAbortUpdateData()){
		ERR_CHMPRN("Failed to send abort update data, but continue...");
	}

	// set original status
	CHANGE_CHMPXSTS_TO_MERGESTOP(status);
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to update(rewind) status(0x%061lx:%s).", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// Update pending hash.
	if(!pImData->UpdatePendingHash()){
		ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
		return false;
	}

	// send status change
	CHMPXSVR	chmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&chmpxsvr)){
		ERR_CHMPRN("Could not get self chmpx information.");
		return false;
	}
	chmpxid_t	nextchmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == nextchmpxid){
		WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");

		if(!PxComSendSlavesStatusChange(&chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change to slaves.");
			return false;
		}
	}else{
		if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change.");
			return false;
		}
	}

	// change down server status.
	if(!ChangeDownSvrStatusBeforeMerge(CHM_DOWNSVR_MERGE_ABORT)){
		ERR_CHMPRN("Failed to change down server status, but continue...");
	}
	return true;
}

bool ChmEventSock::RequestMergeComplete(string* pstring)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		if(pstring){
			*pstring = CTL_RES_INT_ERROR;
		}
		return false;
	}
//	ChmIMData*	pImData = pChmCntrl->GetImDataObj();	not used

	// check SUSPEND status
	if(IsSuspendServerInRing()){
		MSG_CHMPRN("Some server in RING have SUSPEND status, so could not change status.");
		if(pstring){
			*pstring = CTL_RES_ERROR_SOME_SERVER_SUSPEND;
		}
		return false;
	}

	// get terminated chmpxid
	chmpxid_t	chmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == chmpxid){
		// no server found, finish doing so pending hash updated self.
		WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So stop sending status update.");

		// Complete to merge
		//
		// If there is no server in RING, do complete merge.
		//
		if(!MergeComplete()){
			ERR_CHMPRN("Failed to complete merge.");
			if(pstring){
				*pstring = CTL_RES_ERROR_MERGE_COMPLETE;
			}
			return false;
		}
		if(pstring){
			*pstring = CTL_RES_SUCCESS_NOSERVER;
		}
		return true;
	}

	// send complete_merge
	if(!PxComSendMergeComplete(chmpxid)){
		ERR_CHMPRN("Failed to send CHMPX_COM_MERGE_COMPLETE.");
		if(pstring){
			*pstring = CTL_RES_ERROR_COMMUNICATION;
		}
		return false;
	}
	if(pstring){
		*pstring = CTL_RES_SUCCESS;
	}
	return true;
}

bool ChmEventSock::RequestServiceIn(string* pstring)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		if(pstring){
			*pstring = CTL_RES_INT_ERROR;
		}
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	if(pImData->IsOperating()){
		ERR_CHMPRN("Servers are now operating.");
		if(pstring){
			*pstring = CTL_RES_ERROR_OPERATING;
		}
		return false;
	}

	// check self status
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(!(IS_CHMPXSTS_SRVIN(status) && IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_DELETE(status)) && !(IS_CHMPXSTS_SRVOUT(status) && IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_NOACT(status))){
		ERR_CHMPRN("Server is status(0x%016" PRIx64 ":%s), so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		if(pstring){
			*pstring = CTL_RES_ERROR_STATUS_NOT_ALLOWED;
		}
		return false;
	}
	if(IS_CHMPXSTS_SRVOUT(status) && IS_CHMPXSTS_SUSPEND(status)){
		ERR_CHMPRN("Server status(0x%016" PRIx64 ":%s) is SUSPEND, so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		if(pstring){
			*pstring = CTL_RES_ERROR_STATUS_HAS_SUSPEND;
		}
		return false;
	}

	// new status
	if(IS_CHMPXSTS_SRVIN(status) && IS_CHMPXSTS_SUSPEND(status)){		// SERVICEIN & UP & DELETE & SUSPEND
		status = CHMPXSTS_SRVIN_UP_ADDPENDING;
	}else if(IS_CHMPXSTS_SRVIN(status) && IS_CHMPXSTS_NOSUP(status)){	// SERVICEIN & UP & DELETE & NOT SUSPEND
		status = CHMPXSTS_SRVIN_UP_NORMAL;
	}else if(IS_CHMPXSTS_SRVOUT(status)){								// SERVICEOUT & UP & NOACT & NOT SUSPEND
		status = CHMPXSTS_SRVIN_UP_ADDPENDING;
	}

	// set status(set suspend in this method)
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to change server status(0x%016" PRIx64 ").", status);
		if(pstring){
			*pstring = CTL_RES_ERROR_CHANGE_STATUS;
		}
		return false;
	}

	// Update pending hash.
	if(!pImData->UpdatePendingHash()){
		ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
		if(pstring){
			*pstring = CTL_RES_ERROR_CHANGE_STATUS;
		}
		return false;
	}

	// send status update
	CHMPXSVR	chmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&chmpxsvr)){
		ERR_CHMPRN("Could not get self chmpx information.");
		if(pstring){
			*pstring = CTL_RES_ERROR_STATUS_NOTICE;
		}
		return false;
	}
	chmpxid_t	nextchmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == nextchmpxid){
		WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");
		if(!PxComSendSlavesStatusChange(&chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change to slaves.");
			if(pstring){
				*pstring = CTL_RES_ERROR_STATUS_NOTICE;
			}
			return false;
		}
		if(pstring){
			*pstring = CTL_RES_SUCCESS_STATUS_NOTICE;
		}
	}else{
		if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change.");
			if(pstring){
				*pstring = CTL_RES_ERROR_STATUS_NOTICE;
			}
			return false;
		}
	}
	return true;
}

bool ChmEventSock::MergeComplete(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// change down server status before merging.
	if(!ChangeDownSvrStatusBeforeMerge(CHM_DOWNSVR_MERGE_COMPLETE)){
		ERR_CHMPRN("Failed to change down server status, but continue...");
	}

	// status check
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(!IS_CHMPXSTS_SRVIN(status)){
		MSG_CHMPRN("Server status(0x%016" PRIx64 ":%s) is not CHMPXSTS_VAL_SRVIN, so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		return true;
	}
	if(!IS_CHMPXSTS_DONE(status)){
		if(IS_CHMPXSTS_NOTHING(status)){
			MSG_CHMPRN("status(0x%016" PRIx64 ":%s) is not \"DONE\", but not need to change status and nothing to do.", status, STR_CHMPXSTS_FULL(status).c_str());
			return true;
		}
		WAN_CHMPRN("status(0x%016" PRIx64 ":%s) is not \"DONE\", so probably now doing thus could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}
	if(IS_CHMPXSTS_ADD(status) && IS_CHMPXSTS_SUSPEND(status)){
		// allow NOACT & SUSPEND
		ERR_CHMPRN("Server status(0x%016" PRIx64 ":%s) is SUSPEND(on ADD), so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// get pending hash
	chmhash_t	hash;
	if(!pImData->GetSelfPendingHash(hash)){
		ERR_CHMPRN("Failed to get pending hash value.");
		return false;
	}

	// new status
	if(IS_CHMPXSTS_NOACT(status)){
		status	= CHMPXSTS_SRVIN_UP_NORMAL;
	}else if(IS_CHMPXSTS_ADD(status)){
		status	= CHMPXSTS_SRVIN_UP_NORMAL;
	}else if(IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_DELETE(status)){
		SET_CHMPXSTS_UP(status);
		SET_CHMPXSTS_SRVOUT(status);
		SET_CHMPXSTS_NOACT(status);
		SET_CHMPXSTS_NOTHING(status);
	}else if(IS_CHMPXSTS_DOWN(status) && IS_CHMPXSTS_DELETE(status)){
		status	= CHMPXSTS_SRVOUT_DOWN_NORMAL;
		hash	= CHM_INVALID_HASHVAL;
	}else{	// why?
		ERR_CHMPRN("Un-safe status(0x%016" PRIx64 ":%s).", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// hash update
	if(!pImData->SetSelfBaseHash(hash)){
		ERR_CHMPRN("Failed to update base hash(0x%016" PRIx64 ").", hash);
		return false;
	}

	// set new status(set suspend in this method)
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to change self status to 0x%016" PRIx64 ":%s", status, STR_CHMPXSTS_FULL(status).c_str());
		return false;
	}

	// send status change
	CHMPXSVR	chmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&chmpxsvr)){
		ERR_CHMPRN("Could not get self chmpx information.");
		return false;
	}
	chmpxid_t	nextchmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == nextchmpxid){
		WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");

		if(!PxComSendSlavesStatusChange(&chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change to slaves.");
			return false;
		}
	}else{
		if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change.");
			return false;
		}
	}
	return true;
}

bool ChmEventSock::RequestMergeAbort(string* pstring)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		if(pstring){
			*pstring = CTL_RES_INT_ERROR;
		}
		return false;
	}
	//ChmIMData*	pImData = pChmCntrl->GetImDataObj();		not used

	// get terminated chmpxid
	chmpxid_t	chmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == chmpxid){
		// no server found, finish doing so pending hash updated self.
		WAN_CHMPRN("Could not get to chmpxid, probably there is no server without self chmpx on RING. So stop sending status update.");

		// abort merge
		//
		// If there is no server in RING, do abort merge.
		//
		if(!MergeAbort()){
			MSG_CHMPRN("Failed to abort merge, maybe status does not DOING nor DONE now.");
			if(pstring){
				*pstring = CTL_RES_ERROR_MERGE_ABORT;
			}
		}else{
			if(pstring){
				*pstring = CTL_RES_SUCCESS_NOSERVER;
			}
		}
		return true;
	}

	// send merge abort
	if(!PxComSendMergeAbort(chmpxid)){
		ERR_CHMPRN("Failed to send CHMPX_COM_MERGE_ABORT.");
		if(pstring){
			*pstring = CTL_RES_ERROR_COMMUNICATION;
		}
		return false;
	}
	if(pstring){
		*pstring = CTL_RES_SUCCESS;
	}
	return true;
}

bool ChmEventSock::DoSuspend(void)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	if(!pImData->IsChmpxProcess()){
		ERR_CHMPRN("This method must be called on Chmpx process.");
		return false;
	}

	// check status
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(IS_CHMPXSTS_SUSPEND(status)){
		MSG_CHMPRN("Server is already SUSPEND status(0x%016" PRIx64 ":%s).", status, STR_CHMPXSTS_FULL(status).c_str());
		return true;
	}
	if(IS_CHMPXSTS_DOWN(status) || IS_CHMPXSTS_SLAVE(status)){
		MSG_CHMPRN("Server is status(0x%016" PRIx64 ":%s), so could not need to add status SUSPEND.", status, STR_CHMPXSTS_FULL(status).c_str());
		return true;
	}

	// add suspend status
	CHANGE_CHMPXSTS_TO_SUSPEND(status);
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to change server status(0x%016" PRIx64 ").", status);
		return false;
	}

	// Update pending hash.
	if(!pImData->UpdatePendingHash()){
		ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
		return false;
	}

	// send status update
	CHMPXSVR	chmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&chmpxsvr)){
		ERR_CHMPRN("Could not get self chmpx information.");
		return false;
	}
	chmpxid_t	nextchmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == nextchmpxid){
		WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");
		if(!PxComSendSlavesStatusChange(&chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change to slaves.");
			return false;
		}
	}else{
		if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change.");
			return false;
		}
	}

	// If merging, stop it.
	if(!RequestMergeAbort()){
		ERR_CHMPRN("Failed stopping merging.");
		return false;
	}
	return true;
}

//---------------------------------------------------------
// Methods for Ctlport Command
//---------------------------------------------------------
bool ChmEventSock::SendCtlPort(chmpxid_t chmpxid, const unsigned char* pbydata, size_t length, string& strResult)
{
	if(CHM_INVALID_CHMPXID == chmpxid || !pbydata || 0L == length){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	string	hostname;
	short	port	= CHM_INVALID_PORT;
	short	ctlport	= CHM_INVALID_PORT;
	if(!pImData->GetServerBase(chmpxid, hostname, port, ctlport)){
		ERR_CHMPRN("Could not find server by chmpxid(0x%016" PRIx64 ").", chmpxid);
		return false;
	}
	return ChmEventSock::RawSendCtlPort(hostname.c_str(), ctlport, pbydata, length, strResult, sockfd_lockval, sock_retry_count, sock_wait_time, con_retry_count, con_wait_time);
}

bool ChmEventSock::CtlComDump(string& strResponse)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	stringstream	sstream;
	pImData->Dump(sstream);
	strResponse = sstream.str();

	return true;
}

bool ChmEventSock::CtlComMergeStart(string& strResponse)
{
	return RequestMergeStart(&strResponse);
}

bool ChmEventSock::CtlComMergeAbort(string& strResponse)
{
	return RequestMergeAbort(&strResponse);
}

bool ChmEventSock::CtlComMergeComplete(string& strResponse)
{
	return RequestMergeComplete(&strResponse);
}

bool ChmEventSock::CtlComServiceIn(string& strResponse)
{
	if(!RequestServiceIn(&strResponse)){
		return false;
	}

	// If do not merge(almost random deliver mode), do merging, completing it.
	if(is_auto_merge){
		chmpxid_t	nextchmpxid = GetNextRingChmpxId();

		// start merge automatically
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			if(!MergeStart()){
				ERR_CHMPRN("Failed to merge or complete merge for \"service in\".");
				strResponse += "\n";
				strResponse += CTL_RES_ERROR_MERGE_AUTO;
				return false;
			}
		}else{
			if(!RequestMergeStart(&strResponse)){
				ERR_CHMPRN("Failed to merge or complete merge for \"service in\".");
				strResponse += "\n";
				strResponse += CTL_RES_ERROR_MERGE_AUTO;
				return false;
			}
		}
	}
	if(0 == strResponse.length()){
		strResponse = CTL_RES_SUCCESS;
	}
	return true;
}

bool ChmEventSock::CtlComServiceOut(const char* hostname, short ctlport, const char* pOrgCommand, string& strResponse)
{
	if(CHMEMPTYSTR(hostname) || CHM_INVALID_PORT == ctlport || CHMEMPTYSTR(pOrgCommand)){
		ERR_CHMPRN("Parameters are wrong.");
		strResponse = CTL_RES_ERROR_PARAMETER;
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		strResponse = CTL_RES_INT_ERROR;
		return false;
	}
	ChmIMData*	pImData		= pChmCntrl->GetImDataObj();
	chmpxid_t	selfchmpxid	= pImData->GetSelfChmpxId();
	chmpxid_t	chmpxid;
	if(CHM_INVALID_CHMPXID == (chmpxid = pImData->GetChmpxIdByToServerName(hostname, ctlport))){
		ERR_CHMPRN("Could not find a server as %s:%d.", hostname, ctlport);
		strResponse = CTL_RES_ERROR_NOT_FOUND_SVR;
		return false;
	}

	if(selfchmpxid == chmpxid){
		// set self status
		chmpxsts_t	status = pImData->GetSelfStatus();
		if(IS_CHMPXSTS_SLAVE(status) || IS_CHMPXSTS_SRVOUT(status) || IS_CHMPXSTS_DOWN(status) || IS_CHMPXSTS_DELETE(status)){
			ERR_CHMPRN("Server is status(0x%016" PRIx64 ":%s), not enough status to deleting.", status, STR_CHMPXSTS_FULL(status).c_str());
			strResponse = CTL_RES_ERROR_STATUS_NOT_ALLOWED;
			return false;
		}

		// new status
		SET_CHMPXSTS_DELETE(status);
		SET_CHMPXSTS_PENDING(status);

		// set new status(set suspend in this method)
		if(!pImData->SetSelfStatus(status)){
			ERR_CHMPRN("Failed to change server status(0x%016" PRIx64 ":%s).", status, STR_CHMPXSTS_FULL(status).c_str());
			strResponse = CTL_RES_ERROR_CHANGE_STATUS;
			return false;
		}

		// Update pending hash.
		if(!pImData->UpdatePendingHash()){
			ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
			strResponse = CTL_RES_ERROR_CHANGE_STATUS;
			return false;
		}

		// send status update
		CHMPXSVR	chmpxsvr;
		if(!pImData->GetSelfChmpxSvr(&chmpxsvr)){
			ERR_CHMPRN("Could not get self chmpx information.");
			strResponse = CTL_RES_ERROR_STATUS_NOTICE;
			return false;
		}
		chmpxid_t	nextchmpxid = GetNextRingChmpxId();
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");
			if(!PxComSendSlavesStatusChange(&chmpxsvr)){
				ERR_CHMPRN("Failed to send self status change to slaves.");
				strResponse = CTL_RES_ERROR_STATUS_NOTICE;
				return false;
			}
			strResponse = CTL_RES_SUCCESS_STATUS_NOTICE;
		}
		if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change.");
			strResponse = CTL_RES_ERROR_STATUS_NOTICE;
			return false;
		}

		// If do not merge(almost random deliver mode), do merging, completing it.
		if(is_auto_merge){
			// start merge automatically
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				if(!MergeStart()){
					ERR_CHMPRN("Failed to merge or complete merge for \"service out\".");
					strResponse += "\n";
					strResponse += CTL_RES_ERROR_MERGE_AUTO;
					return false;
				}
			}else{
				if(!RequestMergeStart(&strResponse)){
					ERR_CHMPRN("Failed to merge or complete merge for \"service out\".");
					strResponse += "\n";
					strResponse += CTL_RES_ERROR_MERGE_AUTO;
					return false;
				}
			}
		}

	}else{
		// set other server status
		chmpxsts_t	status = pImData->GetServerStatus(chmpxid);
		if(IS_CHMPXSTS_SLAVE(status) || IS_CHMPXSTS_SRVOUT(status) || IS_CHMPXSTS_DELETE(status)){
			ERR_CHMPRN("Server is status(0x%016" PRIx64 ":%s), not enough status to deleting.", status, STR_CHMPXSTS_FULL(status).c_str());
			strResponse = CTL_RES_ERROR_STATUS_NOT_ALLOWED;
			return false;
		}

		if(!IS_CHMPXSTS_DOWN(status)){
			// can transfer command and do on server.
			if(!ChmEventSock::RawSendCtlPort(hostname, ctlport, reinterpret_cast<const unsigned char*>(pOrgCommand), strlen(pOrgCommand) + 1, strResponse, sockfd_lockval, sock_retry_count, sock_wait_time, con_retry_count, con_wait_time)){
				ERR_CHMPRN("Failed to transfer command to %s:%d", hostname, ctlport);
				strResponse = CTL_RES_ERROR_TRANSFER;
				return false;
			}
			MSG_CHMPRN("Success to transfer command to %s:%d", hostname, ctlport);

		}else{
			// set status on this server and send status update.
			//
			if(!IS_CHMPXSTS_NOTHING(status)){		// SERVICEIN & DOWN
				ERR_CHMPRN("Server is status(0x%016" PRIx64 ":%s), already deleting or deleted.", status, STR_CHMPXSTS_FULL(status).c_str());
				strResponse = CTL_RES_ERROR_STATUS_NOT_ALLOWED;
				return false;
			}

			// new status
			SET_CHMPXSTS_DELETE(status);
			SET_CHMPXSTS_PENDING(status);

			// set new status
			if(!pImData->SetServerStatus(chmpxid, status)){
				ERR_CHMPRN("Failed to change server(chmpxid:0x%016" PRIx64 ") status(0x%016" PRIx64 ":%s).", chmpxid, status, STR_CHMPXSTS_FULL(status).c_str());
				strResponse = CTL_RES_ERROR_CHANGE_STATUS;
				return false;
			}

			// Update pending hash.
			if(!pImData->UpdatePendingHash()){
				ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
				strResponse = CTL_RES_ERROR_CHANGE_STATUS;
				return false;
			}

			// send status update
			CHMPXSVR	chmpxsvr;
			if(!pImData->GetChmpxSvr(chmpxid, &chmpxsvr)){
				ERR_CHMPRN("Could not get server(0x%016" PRIx64 ") information.", chmpxid);
				strResponse = CTL_RES_ERROR_STATUS_NOTICE;
				return false;
			}
			chmpxid_t	nextchmpxid = GetNextRingChmpxId();
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");

				if(!PxComSendSlavesStatusChange(&chmpxsvr)){
					ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status change to slaves.", chmpxid);
					strResponse = CTL_RES_ERROR_STATUS_NOTICE;
					return false;
				}
				strResponse = CTL_RES_SUCCESS_STATUS_NOTICE;
			}else{
				if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
					ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status change.", chmpxid);
					strResponse = CTL_RES_ERROR_STATUS_NOTICE;
					return false;
				}
			}

			// If do not merge(almost random deliver mode), do merging, completing it.
			if(is_auto_merge){
				// start merge automatically
				if(CHM_INVALID_CHMPXID == nextchmpxid){
					if(!MergeStart()){
						ERR_CHMPRN("Failed to merge or complete merge for \"service out\".");
						strResponse += "\n";
						strResponse += CTL_RES_ERROR_MERGE_AUTO;
						return false;
					}
				}else{
					if(!RequestMergeStart(&strResponse)){
						ERR_CHMPRN("Failed to merge or complete merge for \"service out\".");
						strResponse += "\n";
						strResponse += CTL_RES_ERROR_MERGE_AUTO;
						return false;
					}
				}
			}
		}
	}

	strResponse = CTL_RES_SUCCESS;
	return true;
}

bool ChmEventSock::CtlComSelfStatus(string& strResponse)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		strResponse = CTL_RES_INT_ERROR;
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// get all information for self chmpx
	long		maxmqcnt	= pImData->GetMaxMQCount();
	long		maxqppxmq	= pImData->GetMaxQueuePerChmpxMQ();
	long		maxqpcmq	= pImData->GetMaxQueuePerClientMQ();
	bool		is_server	= pImData->IsServerMode();
	long		svrcnt		= pImData->GetServerCount();
	long		slvcnt		= pImData->GetSlaveCount();
	size_t		clntcnt		= ctlsockmap.count();
	CHMPXSVR	chmpxsvr;
	string		group;
	int			sock		= CHM_INVALID_SOCK;
	int			ctlsock		= CHM_INVALID_SOCK;
	CHMSTAT		server_stat;
	CHMSTAT		slave_stat;
	if(!pImData->GetGroup(group) || !pImData->GetSelfChmpxSvr(&chmpxsvr) || !pImData->GetSelfSocks(sock, ctlsock)){
		ERR_CHMPRN("Failed to get all information for self chmpx.");
		strResponse = CTL_RES_ERROR_GET_CHMPXSVR;
		return false;
	}
	if(!pImData->GetStat(&server_stat, &slave_stat)){
		ERR_CHMPRN("Failed to get all stat data.");
		strResponse = CTL_RES_ERROR_GET_STAT;
		return false;
	}

	// set output buffer
	stringstream	ss;
	ss << "Server Name               = "	<< chmpxsvr.name									<< endl;
	ss << "Internal ID               = 0x"	<< to_hexstring(chmpxsvr.chmpxid)					<< endl;
	ss << "RING Name                 = "	<< group											<< endl;
	ss << "Mode                      = "	<< (is_server ? "Server" : "Slave")					<< endl;
	ss << "Maximum MQ Count          = "	<< to_string(maxmqcnt)								<< endl;
	ss << "Maximum Queue / Chmpx MQ  = "	<< to_string(maxqppxmq)								<< endl;
	ss << "Maximum Queue / Client MQ = "	<< to_string(maxqpcmq)								<< endl;
	ss << "Hash"																				<< endl;
	ss << "    Enable Hash Value     = 0x"	<< to_hexstring(chmpxsvr.base_hash)					<< endl;
	ss << "    Pending Hash Value    = 0x"	<< to_hexstring(chmpxsvr.pending_hash)				<< endl;
	ss << "Status"																				<< endl;
	ss << "    Server Status         = 0x"	<< to_hexstring(chmpxsvr.status)					<< STR_CHMPXSTS_FULL(chmpxsvr.status) << ")" << endl;
	ss << "    Last Update           = "	<< to_string(chmpxsvr.last_status_time)				<< endl;
	ss << "Connection"																			<< endl;
	ss << "    Port(socket)          = "	<< to_string(chmpxsvr.port)		<< "("				<< (CHM_INVALID_SOCK == sock ? "n/a" : to_string(sock))			<< ")" << endl;
	ss << "    Control Port(socket)  = "	<< to_string(chmpxsvr.ctlport)	<< "("				<< (CHM_INVALID_SOCK == ctlsock ? "n/a" : to_string(ctlsock))	<< ")" << endl;
	ss << "    Use SSL               = "	<< (chmpxsvr.ssl.is_ssl ? "yes" : "no")				<< endl;
	ss << "      Verify Peer         = "	<< (chmpxsvr.ssl.verify_peer ? "yes" : "no")		<< endl;
	ss << "      CA path type        = "	<< (chmpxsvr.ssl.is_ca_file ? "file" : "dir")		<< endl;
	ss << "      CA path             = "	<< chmpxsvr.ssl.capath								<< endl;
	ss << "      Server Cert         = "	<< chmpxsvr.ssl.server_cert							<< endl;
	ss << "      Server Private Key  = "	<< chmpxsvr.ssl.server_prikey						<< endl;
	ss << "      Slave Cert          = "	<< chmpxsvr.ssl.slave_cert							<< endl;
	ss << "      Slave Private Key   = "	<< chmpxsvr.ssl.slave_prikey						<< endl;
	ss << "Connection Count"																	<< endl;
	ss << "    To Servers            = "	<< to_string(svrcnt)								<< endl;
	ss << "    From Slaves           = "	<< to_string(slvcnt)								<< endl;
	ss << "    To Control port       = "	<< to_string(clntcnt)								<< endl;
	ss << "Stats"																				<< endl;
	ss << "  To(From) Servers"																	<< endl;
	ss << "    send count            = "	<< to_string(server_stat.total_sent_count)			<< " count"	<< endl;
	ss << "    receive count         = "	<< to_string(server_stat.total_received_count)		<< " count"	<< endl;
	ss << "    total                 = "	<< to_string(server_stat.total_body_bytes)			<< " bytes"	<< endl;
	ss << "    minimum               = "	<< to_string(server_stat.min_body_bytes)			<< " bytes"	<< endl;
	ss << "    maximum               = "	<< to_string(server_stat.max_body_bytes)			<< " bytes"	<< endl;
	ss << "    total                 = "	<< to_string(server_stat.total_elapsed_time.tv_sec)	<< "s "	<< to_string(server_stat.total_elapsed_time.tv_nsec) << "ns"	<< endl;
	ss << "    minmum                = "	<< to_string(server_stat.min_elapsed_time.tv_sec)	<< "s "	<< to_string(server_stat.min_elapsed_time.tv_nsec) << "ns"	<< endl;
	ss << "    maximum               = "	<< to_string(server_stat.max_elapsed_time.tv_sec)	<< "s "	<< to_string(server_stat.max_elapsed_time.tv_nsec) << "ns"	<< endl;
	ss << "  To(From) Slaves"																	<< endl;
	ss << "    send count            = "	<< to_string(slave_stat.total_sent_count)			<< " count"	<< endl;
	ss << "    receive count         = "	<< to_string(slave_stat.total_received_count)		<< " count"	<< endl;
	ss << "    total                 = "	<< to_string(slave_stat.total_body_bytes)			<< " bytes"	<< endl;
	ss << "    minimum               = "	<< to_string(slave_stat.min_body_bytes)				<< " bytes"	<< endl;
	ss << "    maximum               = "	<< to_string(slave_stat.max_body_bytes)				<< " bytes"	<< endl;
	ss << "    total                 = "	<< to_string(slave_stat.total_elapsed_time.tv_sec)	<< "s "	<< to_string(slave_stat.total_elapsed_time.tv_nsec) << "ns"	<< endl;
	ss << "    minmum                = "	<< to_string(slave_stat.min_elapsed_time.tv_sec)	<< "s "	<< to_string(slave_stat.min_elapsed_time.tv_nsec) << "ns"	<< endl;
	ss << "    maximum               = "	<< to_string(slave_stat.max_elapsed_time.tv_sec)	<< "s "	<< to_string(slave_stat.max_elapsed_time.tv_nsec) << "ns"	<< endl;

	strResponse = ss.str();

	return true;
}

bool ChmEventSock::CtlComAllServerStatus(string& strResponse)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		strResponse = CTL_RES_INT_ERROR;
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// get information for all servers
	PCHMPXSVR	pchmpxsvrs	= NULL;
	long		count		= 0L;
	string		group;
	if(!pImData->GetGroup(group) || !pImData->GetChmpxSvrs(&pchmpxsvrs, count) || !pchmpxsvrs){
		ERR_CHMPRN("Failed to get information for all server.");
		strResponse = CTL_RES_ERROR_GET_CHMPXSVR;
		return false;
	}

	// set output buffer
	stringstream	ss;
	ss << "RING Name                  = "	<< group										<< endl << endl;

	for(long cnt = 0; cnt < count; cnt++){
		ss << "No."							<< to_string(cnt + 1)							<< endl;
		ss << "  Server Name          = "	<< pchmpxsvrs[cnt].name							<< endl;
		ss << "    Port               = "	<< to_string(pchmpxsvrs[cnt].port)				<< endl;
		ss << "    Control Port       = "	<< to_string(pchmpxsvrs[cnt].ctlport)			<< endl;
		ss << "    Use SSL            = "	<< (pchmpxsvrs[cnt].ssl.is_ssl ? "yes" : "no")	<< endl;
		if(pchmpxsvrs[cnt].ssl.is_ssl){
			ss << "    Verify Peer        = "	<< (pchmpxsvrs[cnt].ssl.verify_peer ? "yes" : "no")	<< endl;
			ss << "    CA path type       = "	<< (pchmpxsvrs[cnt].ssl.is_ca_file ? "file" : "dir")<< endl;
			ss << "    CA path            = "	<< pchmpxsvrs[cnt].ssl.capath						<< endl;
			ss << "    Server Cert        = "	<< pchmpxsvrs[cnt].ssl.server_cert					<< endl;
			ss << "    Server Private Key = "	<< pchmpxsvrs[cnt].ssl.server_prikey				<< endl;
			ss << "    Slave Cert         = "	<< pchmpxsvrs[cnt].ssl.slave_cert					<< endl;
			ss << "    Slave Private Key  = "	<< pchmpxsvrs[cnt].ssl.slave_prikey					<< endl;
		}
		ss << "    Server Status      = 0x"	<< to_hexstring(pchmpxsvrs[cnt].status)			<< STR_CHMPXSTS_FULL(pchmpxsvrs[cnt].status) << ")" << endl;
		ss << "    Last Update        = "	<< to_string(pchmpxsvrs[cnt].last_status_time)	<< endl;
		ss << "    Enable Hash Value  = 0x"	<< to_hexstring(pchmpxsvrs[cnt].base_hash)		<< endl;
		ss << "    Pending Hash Value = 0x"	<< to_hexstring(pchmpxsvrs[cnt].pending_hash)	<< endl;
	}
	CHM_Free(pchmpxsvrs);
	strResponse = ss.str();

	return true;
}

bool ChmEventSock::CtlComAllTraceSet(string& strResponse, bool enable)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		strResponse = CTL_RES_ERROR;
		return false;
	}
	ChmIMData*	pImData		= pChmCntrl->GetImDataObj();
	bool		now_enable	= pImData->IsTraceEnable();

	if(enable == now_enable){
		ERR_CHMPRN("Already trace is %s.", (enable ? "enabled" : "disabled"));
		strResponse = CTL_RES_ERROR_TRACE_SET_ALREADY;
		return false;
	}

	bool	result;
	if(enable){
		result = pImData->EnableTrace();
	}else{
		result = pImData->DisableTrace();
	}
	if(!result){
		ERR_CHMPRN("Something error is occured in setting dis/enable TRACE.");
		strResponse = CTL_RES_ERROR_TRACE_SET_FAILED;
		return false;
	}

	strResponse = CTL_RES_SUCCESS;
	return true;
}

bool ChmEventSock::CtlComAllTraceView(string& strResponse, logtype_t dirmask, logtype_t devmask, long count)
{
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		strResponse = CTL_RES_ERROR;
		return false;
	}
	ChmIMData*	pImData	= pChmCntrl->GetImDataObj();
	long		trcount	= pImData->GetTraceCount();

	if(!IS_SAFE_CHMLOG_MASK(dirmask) || !IS_SAFE_CHMLOG_MASK(devmask)){
		ERR_CHMPRN("dirmask(0x%016" PRIx64 ") or devmask(0x%016" PRIx64 ") are wrong.", dirmask, devmask);
		strResponse = CTL_RES_ERROR_TRACE_VIEW_INTERR;
		return false;
	}
	if(count <= 0 || trcount < count){
		WAN_CHMPRN("TRACEVIEW count is wrong, so set all.");
		count = trcount;
	}

	// get TRACE log.
	PCHMLOGRAW	plograwarr;
	if(NULL == (plograwarr = reinterpret_cast<PCHMLOGRAW>(malloc(sizeof(CHMLOGRAW) * count)))){
		ERR_CHMPRN("Could not allocate memory.");
		strResponse = CTL_RES_ERROR_TRACE_VIEW_INTERR;
		return false;
	}
	if(!pImData->GetTrace(plograwarr, count, dirmask, devmask)){
		ERR_CHMPRN("Failed to get trace log.");
		strResponse = CTL_RES_ERROR;
		CHM_Free(plograwarr);
		return false;
	}

	// make result string
	if(0 == count){
		MSG_CHMPRN("There is no trace log.");
		strResponse = CTL_RES_ERROR_TRACE_VIEW_NODATA;
	}else{
		for(long cnt = 0; cnt < count; ++cnt){
			strResponse += STR_CHMLOG_TYPE(plograwarr[cnt].log_type);
			strResponse += "\t";

			strResponse += to_string(plograwarr[cnt].length);
			strResponse += " byte\t";

			strResponse += "START(";
			strResponse += to_string(plograwarr[cnt].start_time.tv_sec);
			strResponse += "s ";
			strResponse += to_string(plograwarr[cnt].start_time.tv_nsec);
			strResponse += "ns) - ";

			strResponse += "FINISH(";
			strResponse += to_string(plograwarr[cnt].fin_time.tv_sec);
			strResponse += "s ";
			strResponse += to_string(plograwarr[cnt].fin_time.tv_nsec);
			strResponse += "ns)\n";
		}
	}
	CHM_Free(plograwarr);
	return true;
}

//---------------------------------------------------------
// Methods for PX2PX Command
//---------------------------------------------------------
//
// PxComSendStatusReq uses sock directly.
//
// [NOTE]
// If you need to lock the socket, you must lock it before calling this method.
//
bool ChmEventSock::PxComSendStatusReq(int sock, chmpxid_t chmpxid)
{
	if(CHM_INVALID_SOCK == sock || CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_REQ))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_STATUS_REQ	pStatusReq	= CVT_COMPTR_STATUS_REQ(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_STATUS_REQ, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pStatusReq->head.type	= CHMPX_COM_STATUS_REQ;
	pStatusReq->head.result	= CHMPX_COM_RES_SUCCESS;
	pStatusReq->head.length	= sizeof(PXCOM_STATUS_REQ);

	// Send request
	//
	// [NOTE]
	// Should lock the socket in caller if you need.
	//
	bool	is_closed = false;
	if(!ChmEventSock::RawSend(sock, GetSSL(sock), pComPkt, is_closed, false, sock_retry_count, sock_wait_time)){		// as default nonblocking
		ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_REQ to sock(%d).", sock);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveStatusReq(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	//PPXCOM_STATUS_REQ	pStatusReq	= CVT_COMPTR_STATUS_REQ(pComAll);	// unused now
	*ppResComPkt					= NULL;

	// get all server status
	PCHMPXSVR	pchmpxsvrs	= NULL;
	long		count		= 0L;
	pxcomres_t	result		= CHMPX_COM_RES_SUCCESS;
	if(!pImData->GetChmpxSvrs(&pchmpxsvrs, count)){
		ERR_CHMPRN("Could not get all server status, but continue to response a error.");
		pchmpxsvrs	= NULL;
		count		= 0L;
		result		= CHMPX_COM_RES_ERROR;
	}

	// make response data
	PCOMPKT	pResComPkt;
	if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_RES, sizeof(CHMPXSVR) * count))){
		ERR_CHMPRN("Could not allocation memory.");
		CHM_Free(pchmpxsvrs);
		return false;
	}

	// compkt
	PPXCOM_ALL			pResComAll	= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
	PPXCOM_STATUS_RES	pStatusRes	= CVT_COMPTR_STATUS_RES(pResComAll);
	SET_PXCOMPKT(pResComPkt, CHMPX_COM_STATUS_RES, pComHead->term_ids.chmpxid, pComHead->dept_ids.chmpxid, true, (sizeof(CHMPXSVR) * count));	// switch chmpxid

	pStatusRes->head.type			= CHMPX_COM_STATUS_RES;
	pStatusRes->head.result			= result;
	pStatusRes->head.length			= sizeof(PXCOM_STATUS_RES) + (sizeof(CHMPXSVR) * count);
	pStatusRes->count				= count;
	pStatusRes->pchmpxsvr_offset	= sizeof(PXCOM_STATUS_RES);

	unsigned char*	pbyres			= CHM_OFFSET(pStatusRes, sizeof(PXCOM_STATUS_RES), unsigned char*);
	if(pchmpxsvrs && 0 < count){
		memcpy(pbyres, pchmpxsvrs, sizeof(CHMPXSVR) * count);
	}

	CHM_Free(pchmpxsvrs);
	*ppResComPkt = pResComPkt;

	return true;
}

bool ChmEventSock::PxComReceiveStatusRes(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt, bool is_init_process)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_STATUS_RES	pStatusRes	= CVT_COMPTR_STATUS_RES(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->term_ids.chmpxid == selfchmpxid){
		// To me
		if(CHMPX_COM_RES_SUCCESS != pStatusRes->head.result){
			// Something error occured by status response
			ERR_CHMPRN("PXCOM_STATUS_RES is failed.");
			return false;
		}

		// Succeed status response
		PCHMPXSVR	pchmpxsvrs	= CHM_OFFSET(pStatusRes, pStatusRes->pchmpxsvr_offset, PCHMPXSVR);
		if(!pchmpxsvrs){
			ERR_CHMPRN("There is no CHMPXSVR data in received PXCOM_STATUS_RES.");
			return false;
		}

		// bup status
		chmpxsts_t	bupstatus = pImData->GetSelfStatus();

		// Merge all status
		if(!pImData->MergeChmpxSvrs(pchmpxsvrs, pStatusRes->count, true, is_init_process, eqfd)){	// remove server chmpx data if there is not in list
			ERR_CHMPRN("Failed to merge server CHMPXLIST from CHMPXSVR list.");
			return false;
		}

		// If server mode, update hash values
		if(is_server_mode){
			if(!pImData->UpdatePendingHash()){
				ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
				return false;
			}
			// check self status changing( normal -> merging )
			//
			// [NOTICE]
			// MergeChmpxSvrs() can change self status from CHMPXSTS_SRVIN_DOWN_NORMAL or CHMPXSTS_SRVIN_DOWN_DELPENDING
			// to CHMPXSTS_SRVIN_UP_MERGING, if it is neeed.
			// If status is changed, we need to start merging.
			//
			// BUT we do not start merging here, because the caller funstion do it. 
			//
			chmpxsts_t	newstatus = pImData->GetSelfStatus();
			if((IS_CHMPXSTS_NOTHING(bupstatus) || IS_CHMPXSTS_PENDING(bupstatus)) && (IS_CHMPXSTS_DOING(newstatus) || IS_CHMPXSTS_DONE(newstatus))){
				WAN_CHMPRN("self status changed from 0x%016" PRIx64 ":%s to 0x%016" PRIx64 ":%s.", bupstatus, STR_CHMPXSTS_FULL(bupstatus).c_str(), newstatus, STR_CHMPXSTS_FULL(newstatus).c_str());
			}
		}
	}else{
		// To other chmpxid
		ERR_CHMPRN("Received PXCOM_STATUS_RES packet, but terminal chmpxid(0x%016" PRIx64 ") is not self chmpxid(0x%016" PRIx64 ").", pComHead->term_ids.chmpxid, selfchmpxid);
		return false;
	}
	return true;
}

//
// PxComSendConinitReq uses sock directly.
//
// [NOTE]
// PPXCOM_CONINIT_REQ does not select socket.(Do not use GetLockedSendSock method).
//
bool ChmEventSock::PxComSendConinitReq(int sock, chmpxid_t chmpxid)
{
	if(CHM_INVALID_SOCK == sock || CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// datas
	chmpxid_t	selfchmpxid = pImData->GetSelfChmpxId();
	short		port		= CHM_INVALID_PORT;
	short		ctlport		= CHM_INVALID_PORT;
	if(!pImData->GetSelfPorts(port, ctlport)){
		ERR_CHMPRN("Could not get self ctlport.");
		return false;
	}

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_CONINIT_REQ))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_CONINIT_REQ	pConinitReq	= CVT_COMPTR_CONINIT_REQ(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_CONINIT_REQ, selfchmpxid, chmpxid, true, 0L);

	pConinitReq->head.type		= CHMPX_COM_CONINIT_REQ;
	pConinitReq->head.result	= CHMPX_COM_RES_SUCCESS;
	pConinitReq->head.length	= sizeof(PXCOM_CONINIT_REQ);
	pConinitReq->chmpxid		= selfchmpxid;
	pConinitReq->ctlport		= ctlport;

	// Send request
	if(!ChmEventSock::LockedSend(sock, GetSSL(sock), pComPkt)){		// as default nonblocking
		ERR_CHMPRN("Failed to send CHMPX_COM_CONINIT_REQ to sock(%d).", sock);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveConinitReq(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt, chmpxid_t& from_chmpxid, short& ctlport)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_CONINIT_REQ	pConinitReq	= CVT_COMPTR_CONINIT_REQ(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->term_ids.chmpxid == selfchmpxid){
		// To me
		from_chmpxid= pConinitReq->chmpxid;
		ctlport		= pConinitReq->ctlport;

	}else{
		// To other chmpxid
		ERR_CHMPRN("Received PPXCOM_CONINIT_REQ packet, but terminal chmpxid(0x%016" PRIx64 ") is not self chmpxid(0x%016" PRIx64 ").", pComHead->term_ids.chmpxid, selfchmpxid);
		return false;
	}
	return true;
}

//
// PxComSendConinitRes uses sock directly.
//
// [NOTE]
// PPXCOM_CONINIT_RES does not select socket.(Do not use GetLockedSendSock method).
//
bool ChmEventSock::PxComSendConinitRes(int sock, chmpxid_t chmpxid, pxcomres_t result)
{
	if(CHM_INVALID_SOCK == sock || CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_CONINIT_RES))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_CONINIT_RES	pConinitRes	= CVT_COMPTR_CONINIT_RES(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_CONINIT_RES, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pConinitRes->head.type		= CHMPX_COM_CONINIT_RES;
	pConinitRes->head.result	= result;
	pConinitRes->head.length	= sizeof(PXCOM_CONINIT_RES);

	// Send request
	if(!ChmEventSock::LockedSend(sock, GetSSL(sock), pComPkt)){		// as default nonblocking
		ERR_CHMPRN("Failed to send CHMPX_COM_CONINIT_RES to sock(%d).", sock);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveConinitRes(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt, pxcomres_t& result)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_CONINIT_RES	pConinitRes	= CVT_COMPTR_CONINIT_RES(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->term_ids.chmpxid == selfchmpxid){
		// To me
		result = pConinitRes->head.result;

	}else{
		// To other chmpxid
		ERR_CHMPRN("Received PPXCOM_CONINIT_RES packet, but terminal chmpxid(0x%016" PRIx64 ") is not self chmpxid(0x%016" PRIx64 ").", pComHead->term_ids.chmpxid, selfchmpxid);
		return false;
	}
	return true;
}

bool ChmEventSock::PxComSendJoinRing(chmpxid_t chmpxid, PCHMPXSVR pserver)
{
	if(CHM_INVALID_CHMPXID == chmpxid || !pserver){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_JOIN_RING))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_JOIN_RING	pJoinRing	= CVT_COMPTR_JOIN_RING(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_JOIN_RING, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pJoinRing->head.type	= CHMPX_COM_JOIN_RING;
	pJoinRing->head.result	= CHMPX_COM_RES_SUCCESS;
	pJoinRing->head.length	= sizeof(PXCOM_JOIN_RING);
	COPY_PCHMPXSVR(&(pJoinRing->server), pserver);

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_JOIN_RING to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveJoinRing(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_JOIN_RING	pJoinRing	= CVT_COMPTR_JOIN_RING(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around -> next step: update pending hash value
		//
		if(CHMPX_COM_RES_SUCCESS == pJoinRing->head.result){
			// Succeed adding this server on RING
			//
			// Next step is update all server status.
			//
			chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				ERR_CHMPRN("Could not get next server chmpxid, maybe there is no server on RING...But WHY?");
				return true;
			}

			int	nextsock = CHM_INVALID_SOCK;
			if(!GetLockedSendSock(nextchmpxid, nextsock, false) || CHM_INVALID_SOCK == nextsock){		// LOCK SOCKET
				ERR_CHMPRN("Could not get socket for chmpxid(0x%016" PRIx64 ").", nextchmpxid);
				return false;
			}

			// Send request
			if(!PxComSendStatusReq(nextsock, nextchmpxid)){
				ERR_CHMPRN("Failed to send PXCOM_STATUS_REQ.");
				UnlockSendSock(nextsock);			// UNLOCK SOCKET
				return false;
			}
			UnlockSendSock(nextsock);				// UNLOCK SOCKET
			return true;

		}else{
			// Failed adding this server on RING
			//
			// This function returns false, so caller gets it and close all connection.
			// So that, this function do nothing.
			//
			ERR_CHMPRN("PXCOM_JOIN_RING is failed, hope to recover automatically.");
			return false;
		}
	}else{
		// update own chmshm & transfer packet.
		//

		// update chmshm
		pxcomres_t	ResultCode;
		if(!pImData->MergeChmpxSvrs(&(pJoinRing->server), 1, false, false, eqfd)){	// not remove other
			// error occured, so transfer packet.
			ERR_CHMPRN("Could not update server chmpx information.");
			ResultCode = CHMPX_COM_RES_ERROR;
		}else{
			// succeed.
			ResultCode = CHMPX_COM_RES_SUCCESS;
		}
		if(CHMPX_COM_RES_SUCCESS != pJoinRing->head.result){
			MSG_CHMPRN("Already error occured before this server.");
			ResultCode = CHMPX_COM_RES_ERROR;
		}

		// check & rechain
		bool	is_rechain = false;
		if(!CheckRechainRing(pJoinRing->server.chmpxid, is_rechain)){
			ERR_CHMPRN("Something error occured in rehcaining RING, but continue...");
			ResultCode = CHMPX_COM_RES_ERROR;
		}else{
			if(is_rechain){
				MSG_CHMPRN("Rechained RING after joining chmpxid(0x%016" PRIx64 ").", pJoinRing->server.chmpxid);
			}else{
				MSG_CHMPRN("Not rechained RING after joining chmpxid(0x%016" PRIx64 ").", pJoinRing->server.chmpxid);
			}
		}

		// next chmpxid(after rechaining)
		chmpxid_t	nextchmpxid;
		if(is_rechain){
			// After rechaining, new server does not in server list.
			// So get old next server by GetNextRingChmpxId(), force set new server chmpxid.
			//
			nextchmpxid = pJoinRing->server.chmpxid;
		}else{
			nextchmpxid	= GetNextRingChmpxId();
		}
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			ERR_CHMPRN("Could not get next server chmpxid.");
			return false;
		}

		if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
			// Deperture chmpx maybe DOWN!
			//
			// [NOTICE]
			// This case is very small case, deperture server sends this packet but that server could not
			// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
			// So we do not transfer this packet, because it could not be stopped in RING.
			//
			ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
			*ppResComPkt = NULL;

		}else{
			// make response data buffer
			PCOMPKT	pResComPkt;
			if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_JOIN_RING))){
				ERR_CHMPRN("Could not allocation memory.");
				return false;
			}

			// compkt
			PPXCOM_ALL			pResComAll	= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
			PPXCOM_JOIN_RING	pResJoinRing= CVT_COMPTR_JOIN_RING(pResComAll);

			SET_PXCOMPKT(pResComPkt, CHMPX_COM_JOIN_RING, pComHead->dept_ids.chmpxid, nextchmpxid, false, 0L);	// dept chmpxid is not changed
			COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

			// join_ring(copy)
			pResJoinRing->head.type				= pJoinRing->head.type;
			pResJoinRing->head.result			= ResultCode;
			pResJoinRing->head.length			= pJoinRing->head.length;
			COPY_PCHMPXSVR(&(pResJoinRing->server), &(pJoinRing->server));

			*ppResComPkt = pResComPkt;
		}
	}
	return true;
}

bool ChmEventSock::PxComSendStatusUpdate(chmpxid_t chmpxid, PCHMPXSVR pchmpxsvrs, long count)
{
	if(CHM_INVALID_CHMPXID == chmpxid || !pchmpxsvrs || count <= 0L){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*		pImData = pChmCntrl->GetImDataObj();

	// make data
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_UPDATE, sizeof(CHMPXSVR) * count))){
		ERR_CHMPRN("Could not allocation memory.");
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_STATUS_UPDATE	pStsUpdate	= CVT_COMPTR_STATUS_UPDATE(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_STATUS_UPDATE, pImData->GetSelfChmpxId(), chmpxid, true, (sizeof(CHMPXSVR) * count));

	// status_update
	pStsUpdate->head.type			= CHMPX_COM_STATUS_UPDATE;
	pStsUpdate->head.result			= CHMPX_COM_RES_SUCCESS;
	pStsUpdate->head.length			= sizeof(PXCOM_STATUS_UPDATE) + (sizeof(CHMPXSVR) * count);
	pStsUpdate->count				= count;
	pStsUpdate->pchmpxsvr_offset	= sizeof(PXCOM_STATUS_UPDATE);

	// extra area
	PCHMPXSVR	pStsUpChmsvr = CHM_OFFSET(pStsUpdate, sizeof(PXCOM_STATUS_UPDATE), PCHMPXSVR);
	for(long cnt = 0; cnt < count; cnt++){
		COPY_PCHMPXSVR(&pStsUpChmsvr[cnt], &pchmpxsvrs[cnt]);
	}

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_UPDATE to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveStatusUpdate(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*				pImData			= pChmCntrl->GetImDataObj();
	PPXCOM_STATUS_UPDATE	pReqStsUpdate	= CVT_COMPTR_STATUS_UPDATE(pComAll);
	PCHMPXSVR				pReqChmsvrs		= CHM_OFFSET(pReqStsUpdate, sizeof(PXCOM_STATUS_UPDATE), PCHMPXSVR);
	chmpxid_t				selfchmpxid		= pImData->GetSelfChmpxId();
	*ppResComPkt							= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around
		//
		if(CHMPX_COM_RES_SUCCESS == pReqStsUpdate->head.result){
			// Succeed updating status
			return true;
		}else{
			// Failed updating status, for recovering...
			ERR_CHMPRN("PXCOM_STATUS_UPDATE is failed, NEED to check all servers and recover MANUALLY.");
			return false;
		}

	}else{
		// update own chmshm & transfer packet.
		//
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			ERR_CHMPRN("Could not get next server chmpxid.");
			return false;
		}

		pxcomres_t	ResultCode		= pReqStsUpdate->head.result;
		long		ResultCount		= pReqStsUpdate->count;
		PCHMPXSVR	pResultChmsvrs	= pReqChmsvrs;

		// Already error occured, skip merging
		if(CHMPX_COM_RES_SUCCESS != ResultCode){
			MSG_CHMPRN("Already error occured before this server.");
			ResultCode = CHMPX_COM_RES_ERROR;

		}else{
			// update status into chmshm
			if(!pImData->MergeChmpxSvrsForStatusUpdate(pResultChmsvrs, ResultCount, eqfd)){		// not remove other
				// error occured.
				ERR_CHMPRN("Could not update all server status, maybe recovering in function...");
				ResultCode = CHMPX_COM_RES_ERROR;
			}else{
				MSG_CHMPRN("Succeed update status.");
				ResultCode = CHMPX_COM_RES_SUCCESS;

				// Update pending hash.
				if(!pImData->UpdatePendingHash()){
					ERR_CHMPRN("Failed to update pending hash for all servers, but continue....");
				}
			}
		}

		if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
			// Deperture chmpx maybe DOWN!
			//
			// [NOTICE]
			// This case is very small case, deperture server sends this packet but that server could not
			// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
			// So we do not transfer this packet, because it could not be stopped in RING.
			//
			ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
			*ppResComPkt = NULL;

		}else{
			// make response data buffer
			if(NULL == ((*ppResComPkt) = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_UPDATE, sizeof(CHMPXSVR) * ResultCount))){
				ERR_CHMPRN("Could not allocation memory.");
				return false;
			}

			// compkt
			PPXCOM_ALL				pResComAll		= CVT_COM_ALL_PTR_PXCOMPKT(*ppResComPkt);
			PPXCOM_STATUS_UPDATE	pResStsUpdate	= CVT_COMPTR_STATUS_UPDATE(pResComAll);

			SET_PXCOMPKT((*ppResComPkt), CHMPX_COM_STATUS_UPDATE, pComHead->dept_ids.chmpxid, nextchmpxid, false, (sizeof(CHMPXSVR) * ResultCount));	// dept chmpxid is not changed.
			COPY_TIMESPEC(&((*ppResComPkt)->head.reqtime), &(pComHead->reqtime));	// not change

			// status_update(copy)
			pResStsUpdate->head.type				= pReqStsUpdate->head.type;
			pResStsUpdate->head.result				= ResultCode;
			pResStsUpdate->head.length				= pReqStsUpdate->head.length;
			pResStsUpdate->count					= ResultCount;
			pResStsUpdate->pchmpxsvr_offset			= sizeof(PXCOM_STATUS_UPDATE);

			// extra area
			PCHMPXSVR	pResChmsvrs = CHM_OFFSET(pResStsUpdate, sizeof(PXCOM_STATUS_UPDATE), PCHMPXSVR);
			for(long cnt = 0; cnt < ResultCount; cnt++){
				COPY_PCHMPXSVR(&pResChmsvrs[cnt], &pResultChmsvrs[cnt]);
			}
		}
	}
	return true;
}

bool ChmEventSock::PxComSendStatusConfirm(chmpxid_t chmpxid, PCHMPXSVR pchmpxsvrs, long count)
{
	if(CHM_INVALID_CHMPXID == chmpxid || !pchmpxsvrs || count <= 0L){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*		pImData = pChmCntrl->GetImDataObj();

	// make data
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_CONFIRM, sizeof(CHMPXSVR) * count))){
		ERR_CHMPRN("Could not allocation memory.");
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_STATUS_CONFIRM	pStsConfirm	= CVT_COMPTR_STATUS_CONFIRM(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_STATUS_CONFIRM, pImData->GetSelfChmpxId(), chmpxid, true, (sizeof(CHMPXSVR) * count));

	// status_update
	pStsConfirm->head.type			= CHMPX_COM_STATUS_CONFIRM;
	pStsConfirm->head.result		= CHMPX_COM_RES_SUCCESS;
	pStsConfirm->head.length		= sizeof(PXCOM_STATUS_CONFIRM) + (sizeof(CHMPXSVR) * count);
	pStsConfirm->count				= count;
	pStsConfirm->pchmpxsvr_offset	= sizeof(PXCOM_STATUS_CONFIRM);

	// extra area
	PCHMPXSVR	pStsChmsvr = CHM_OFFSET(pStsConfirm, sizeof(PXCOM_STATUS_CONFIRM), PCHMPXSVR);
	for(long cnt = 0; cnt < count; cnt++){
		COPY_PCHMPXSVR(&pStsChmsvr[cnt], &pchmpxsvrs[cnt]);
	}

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_CONFIRM to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveStatusConfirm(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*				pImData			= pChmCntrl->GetImDataObj();
	PPXCOM_STATUS_CONFIRM	pReqStsConfirm	= CVT_COMPTR_STATUS_CONFIRM(pComAll);
	PCHMPXSVR				pReqChmsvrs		= CHM_OFFSET(pReqStsConfirm, sizeof(PXCOM_STATUS_CONFIRM), PCHMPXSVR);
	chmpxid_t				selfchmpxid		= pImData->GetSelfChmpxId();
	*ppResComPkt							= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around
		//
		if(CHMPX_COM_RES_SUCCESS == pReqStsConfirm->head.result){
			// Succeed status confirm, do next step
			//
			chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				ERR_CHMPRN("Could not get next server chmpxid.");
				return false;
			}

			// Check suspending server
			if(IsSuspendServerInRing()){
				WAN_CHMPRN("Found suspendind servers, so then could not start to merge.");
				return true;
			}

			// Send start merge
			if(!PxComSendMergeStart(nextchmpxid)){
				ERR_CHMPRN("Failed to send CHMPX_COM_MERGE_START to next chmpxid(0x%016" PRIx64 ").", nextchmpxid);
				return false;
			}

		}else{
			// Failed status confirm
			ERR_CHMPRN("PXCOM_STATUS_CONFIRM is failed, do nothing to recover...");
			return false;
		}

	}else{
		// check chmshm & transfer packet.
		//
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			ERR_CHMPRN("Could not get next server chmpxid.");
			return false;
		}

		pxcomres_t	ResultCode = pReqStsConfirm->head.result;

		// Already error occured, skip merging
		if(CHMPX_COM_RES_SUCCESS != ResultCode){
			MSG_CHMPRN("Already error occured before this server.");
			ResultCode = CHMPX_COM_RES_ERROR;
		}else{
			// Compare
			if(!pImData->CompareChmpxSvrs(pReqChmsvrs, pReqStsConfirm->count)){
				ResultCode = CHMPX_COM_RES_ERROR;
			}else{
				ResultCode = CHMPX_COM_RES_SUCCESS;
			}
		}

		if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
			// Deperture chmpx maybe DOWN!
			//
			// [NOTICE]
			// This case is very small case, deperture server sends this packet but that server could not
			// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
			// So we do not transfer this packet, because it could not be stopped in RING.
			//
			ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
			*ppResComPkt = NULL;

		}else{
			// make response data buffer
			if(NULL == ((*ppResComPkt) = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_CONFIRM, sizeof(CHMPXSVR) * pReqStsConfirm->count))){
				ERR_CHMPRN("Could not allocation memory.");
				return false;
			}

			// compkt
			PPXCOM_ALL				pResComAll		= CVT_COM_ALL_PTR_PXCOMPKT(*ppResComPkt);
			PPXCOM_STATUS_CONFIRM	pResStsConfirm	= CVT_COMPTR_STATUS_CONFIRM(pResComAll);

			SET_PXCOMPKT((*ppResComPkt), CHMPX_COM_STATUS_CONFIRM, pComHead->dept_ids.chmpxid, nextchmpxid, false, (sizeof(CHMPXSVR) * pReqStsConfirm->count));	// dept chmpxid is not changed.
			COPY_TIMESPEC(&((*ppResComPkt)->head.reqtime), &(pComHead->reqtime));	// not change

			// status_update(copy)
			pResStsConfirm->head.type				= pReqStsConfirm->head.type;
			pResStsConfirm->head.result				= ResultCode;
			pResStsConfirm->head.length				= pReqStsConfirm->head.length;
			pResStsConfirm->count					= pReqStsConfirm->count;
			pResStsConfirm->pchmpxsvr_offset		= sizeof(PXCOM_STATUS_CONFIRM);

			// extra area
			PCHMPXSVR	pResChmsvrs = CHM_OFFSET(pResStsConfirm, sizeof(PXCOM_STATUS_CONFIRM), PCHMPXSVR);
			for(long cnt = 0; cnt < pReqStsConfirm->count; cnt++){
				COPY_PCHMPXSVR(&pResChmsvrs[cnt], &pReqChmsvrs[cnt]);
			}
		}
	}
	return true;
}

//
// STATUS_CHANGE is only sending packet to slaves.
//
bool ChmEventSock::PxComSendStatusChange(chmpxid_t chmpxid, PCHMPXSVR pserver, bool is_send_slaves)
{
	if(CHM_INVALID_CHMPXID == chmpxid || !pserver){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_CHANGE))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_STATUS_CHANGE	pStatusChange	= CVT_COMPTR_STATUS_CHANGE(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_STATUS_CHANGE, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pStatusChange->head.type	= CHMPX_COM_STATUS_CHANGE;
	pStatusChange->head.result	= CHMPX_COM_RES_SUCCESS;
	pStatusChange->head.length	= sizeof(PXCOM_STATUS_CHANGE);
	COPY_PCHMPXSVR(&(pStatusChange->server), pserver);

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_CHANGE to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}

	// send to slaves
	if(is_send_slaves){
		if(!PxComSendSlavesStatusChange(pserver)){
			ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_CHANGE to slaves.");
		}
	}
	CHM_Free(pComPkt);

	return true;
}

//
// Only send slaves
//
bool ChmEventSock::PxComSendSlavesStatusChange(PCHMPXSVR pserver)
{
	if(!pserver){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// get all slave chmpxid.
	chmpxid_t		selfchmpxid = pImData->GetSelfChmpxId();
	chmpxidlist_t	chmpxidlist;
	if(0L == pImData->GetSlaveChmpxIds(chmpxidlist)){
		MSG_CHMPRN("There is no slave, so not need to send STATUS_CHANGE.");
		return true;
	}

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_CHANGE))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_STATUS_CHANGE	pStatusChange	= CVT_COMPTR_STATUS_CHANGE(pComAll);

	// loop: send to slaves
	for(chmpxidlist_t::const_iterator iter = chmpxidlist.begin(); iter != chmpxidlist.end(); ++iter){
		// Slave chmpxid list has server mode chmpxid.
		// Because the server connects other server for RING as SLAVE.
		// Then if server chmpxid, skip it.
		//
		if(pImData->IsServerChmpxId(*iter)){
			continue;
		}

		// set status change struct data
		SET_PXCOMPKT(pComPkt, CHMPX_COM_STATUS_CHANGE, selfchmpxid, (*iter), true, 0L);

		pStatusChange->head.type	= CHMPX_COM_STATUS_CHANGE;
		pStatusChange->head.result	= CHMPX_COM_RES_SUCCESS;
		pStatusChange->head.length	= sizeof(PXCOM_STATUS_CHANGE);
		COPY_PCHMPXSVR(&(pStatusChange->server), pserver);

		// send
		if(!Send(pComPkt, NULL, 0L)){
			ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_CHANGE to slave chmpxid(0x%016" PRIx64 "), but continue...", *iter);
		}
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveStatusChange(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*				pImData			= pChmCntrl->GetImDataObj();
	PPXCOM_STATUS_CHANGE	pStatusChange	= CVT_COMPTR_STATUS_CHANGE(pComAll);
	chmpxid_t				selfchmpxid		= pImData->GetSelfChmpxId();
	*ppResComPkt							= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around
		//
		if(CHMPX_COM_RES_SUCCESS == pStatusChange->head.result){
			// Succeed change status
			MSG_CHMPRN("PXCOM_STATUS_CHANGE is around the RING with success.");
		}else{
			// Failed change status
			ERR_CHMPRN("PXCOM_STATUS_CHANGE is failed, hope to recover automatic");
			return false;
		}
	}else{
		// update own chmshm & transfer packet.
		//

		// update chmshm
		pxcomres_t	ResultCode;
		if(!pImData->MergeChmpxSvrs(&(pStatusChange->server), 1, false, false, eqfd)){	// not remove other
			// error occured, so transfer packet.
			ERR_CHMPRN("Could not update server chmpx information.");
			ResultCode = CHMPX_COM_RES_ERROR;
		}else{
			// succeed.
			ResultCode = CHMPX_COM_RES_SUCCESS;
		}
		if(CHMPX_COM_RES_SUCCESS != pStatusChange->head.result){
			MSG_CHMPRN("Already error occured before this server.");
			ResultCode = CHMPX_COM_RES_ERROR;
		}

		// Update pending hash.
		if(is_server_mode){
			if(!pImData->UpdatePendingHash()){
				WAN_CHMPRN("Failed to update pending hash for all servers, so stop update status, but continue...");
			}
		}

		// [NOTICE]
		// This STATUS_CHANGE is only received by Slave and Server.
		// If server mode, transfer packet to RING, if slave mode, do not transfer it.
		//
		if(is_server_mode){
			// always, new updated status to send to slaves
			//
			if(!PxComSendSlavesStatusChange(&(pStatusChange->server))){
				ERR_CHMPRN("Failed to send CHMPX_COM_STATUS_CHANGE to slaves, but continue...");
			}

			// check & rechain
			//
			bool	is_rechain = false;
			if(!CheckRechainRing(pStatusChange->server.chmpxid, is_rechain)){
				ERR_CHMPRN("Something error occured in rehcaining RING, but continue...");
				ResultCode = CHMPX_COM_RES_ERROR;
			}else{
				if(is_rechain){
					MSG_CHMPRN("Rechained RING after joining chmpxid(0x%016" PRIx64 ").", pStatusChange->server.chmpxid);
				}else{
					MSG_CHMPRN("Not rechained RING after joining chmpxid(0x%016" PRIx64 ").", pStatusChange->server.chmpxid);
				}
			}

			// next chmpxid(after rechaining)
			chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				ERR_CHMPRN("Could not get next server chmpxid.");
				return false;
			}

			if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
				// Deperture chmpx maybe DOWN!
				//
				// [NOTICE]
				// This case is very small case, deperture server sends this packet but that server could not
				// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
				// So we do not transfer this packet, because it could not be stopped in RING.
				//
				ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
				*ppResComPkt = NULL;

			}else{
				// make response data buffer
				PCOMPKT	pResComPkt;
				if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_STATUS_CHANGE))){
					ERR_CHMPRN("Could not allocation memory.");
					return false;
				}

				// compkt
				PPXCOM_ALL				pResComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
				PPXCOM_STATUS_CHANGE	pResStatusChange= CVT_COMPTR_STATUS_CHANGE(pResComAll);

				SET_PXCOMPKT(pResComPkt, CHMPX_COM_STATUS_CHANGE, pComHead->dept_ids.chmpxid, nextchmpxid, false, 0L);	// dept chmpxid is not changed
				COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

				// change_status(copy)
				pResStatusChange->head.type			= pStatusChange->head.type;
				pResStatusChange->head.result		= ResultCode;
				pResStatusChange->head.length		= pStatusChange->head.length;
				COPY_PCHMPXSVR(&(pResStatusChange->server), &(pStatusChange->server));

				*ppResComPkt = pResComPkt;
			}
		}
	}
	return true;
}

bool ChmEventSock::PxComSendMergeStart(chmpxid_t chmpxid)
{
	if(CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_MERGE_START))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_MERGE_START	pMergeStart	= CVT_COMPTR_MERGE_START(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_MERGE_START, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pMergeStart->head.type		= CHMPX_COM_MERGE_START;
	pMergeStart->head.result	= CHMPX_COM_RES_SUCCESS;
	pMergeStart->head.length	= sizeof(PXCOM_MERGE_START);

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_MERGE_START to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveMergeStart(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_MERGE_START	pMergeStart	= CVT_COMPTR_MERGE_START(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around -> next step: do merging
		//
		if(CHMPX_COM_RES_SUCCESS == pMergeStart->head.result){
			// Succeed
			MSG_CHMPRN("PXCOM_MERGE_START is succeed, All server start to merge.");

			// If there is DELETE Pending server, do DELETE done status here!
			chmpxid_t	nextchmpxid = GetNextRingChmpxId();
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				MSG_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");
			}
			chmpxid_t	downchmpxid;
			while(CHM_INVALID_CHMPXID != (downchmpxid = pImData->GetChmpxIdByStatus(CHMPXSTS_SRVIN_DOWN_DELPENDING, false))){
				// change down server status
				if(!pImData->SetServerStatus(downchmpxid, CHMPXSTS_SRVIN_DOWN_DELETED)){
					ERR_CHMPRN("Failed to change server(0x%016" PRIx64 ") status to CHMPXSTS_SRVIN_DOWN_DELETED.", downchmpxid);
					return false;
				}
				// send down server status update
				CHMPXSVR	chmpxsvr;
				if(!pImData->GetChmpxSvr(downchmpxid, &chmpxsvr)){
					ERR_CHMPRN("Could not get server(0x%016" PRIx64 ") information.", downchmpxid);
					return false;
				}
				if(CHM_INVALID_CHMPXID == nextchmpxid){
					if(!PxComSendSlavesStatusChange(&chmpxsvr)){
						ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status change to slaves.", downchmpxid);
						return false;
					}
				}else{
					if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
						ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status change.", downchmpxid);
						return false;
					}
				}
			}
		}else{
			// Something error occured on RING.
			ERR_CHMPRN("PXCOM_MERGE_START is failed, could not recover, but returns true.");
			return true;
		}
	}else{
		// update own chmshm & transfer packet.
		//
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			ERR_CHMPRN("Could not get next server chmpxid.");
			return false;
		}

		if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
			// Deperture chmpx maybe DOWN!
			//
			// [NOTICE]
			// This case is very small case, deperture server sends this packet but that server could not
			// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
			// So we do not transfer this packet, because it could not be stopped in RING.
			//
			ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
			*ppResComPkt = NULL;

		}else{
			// make response data buffer
			PCOMPKT	pResComPkt;
			if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_MERGE_START))){
				ERR_CHMPRN("Could not allocation memory.");
				return false;
			}

			// compkt
			PPXCOM_ALL			pResComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
			PPXCOM_MERGE_START	pResMergeStart	= CVT_COMPTR_MERGE_START(pResComAll);

			SET_PXCOMPKT(pResComPkt, CHMPX_COM_MERGE_START, pComHead->dept_ids.chmpxid, nextchmpxid, false, 0L);	// dept chmpxid is not changed.
			COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

			// merge_start(copy)
			pResMergeStart->head.type			= pMergeStart->head.type;
			pResMergeStart->head.result			= pMergeStart->head.result;		// always no error.
			pResMergeStart->head.length			= pMergeStart->head.length;

			if(CHMPX_COM_RES_SUCCESS != pMergeStart->head.result){
				// already error occured, so ignore this packet.
				WAN_CHMPRN("Already error occured before this server, but doing.");
			}
			*ppResComPkt = pResComPkt;
		}
	}
	return true;
}

bool ChmEventSock::PxComSendMergeAbort(chmpxid_t chmpxid)
{
	if(CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_MERGE_ABORT))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_MERGE_ABORT	pMergeAbort	= CVT_COMPTR_MERGE_ABORT(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_MERGE_ABORT, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pMergeAbort->head.type		= CHMPX_COM_MERGE_ABORT;
	pMergeAbort->head.result	= CHMPX_COM_RES_SUCCESS;
	pMergeAbort->head.length	= sizeof(PXCOM_MERGE_ABORT);

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_MERGE_ABORT to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveMergeAbort(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_MERGE_ABORT	pMergeAbort	= CVT_COMPTR_MERGE_ABORT(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around -> next step: do merging
		//
		if(CHMPX_COM_RES_SUCCESS == pMergeAbort->head.result){
			// Succeed
			MSG_CHMPRN("PXCOM_MERGE_ABORT is succeed, All server start to merge.");
		}else{
			// Something error occured on RING.
			ERR_CHMPRN("PXCOM_MERGE_ABORT is failed, could not recover, but returns true.");
			return true;
		}
	}else{
		// update own chmshm & transfer packet.
		//
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			ERR_CHMPRN("Could not get next server chmpxid.");
			return false;
		}

		if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
			// Deperture chmpx maybe DOWN!
			//
			// [NOTICE]
			// This case is very small case, deperture server sends this packet but that server could not
			// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
			// So we do not transfer this packet, because it could not be stopped in RING.
			//
			ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
			*ppResComPkt = NULL;

		}else{
			// make response data buffer
			PCOMPKT	pResComPkt;
			if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_MERGE_ABORT))){
				ERR_CHMPRN("Could not allocation memory.");
				return false;
			}

			// compkt
			PPXCOM_ALL			pResComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
			PPXCOM_MERGE_ABORT	pResMergeAbort	= CVT_COMPTR_MERGE_ABORT(pResComAll);

			SET_PXCOMPKT(pResComPkt, CHMPX_COM_MERGE_ABORT, pComHead->dept_ids.chmpxid, nextchmpxid, false, 0L);	// dept chmpxid is not changed.
			COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

			// merge_start(copy)
			pResMergeAbort->head.type		= pMergeAbort->head.type;
			pResMergeAbort->head.result		= pMergeAbort->head.result;			// always no error.
			pResMergeAbort->head.length		= pMergeAbort->head.length;

			if(CHMPX_COM_RES_SUCCESS != pMergeAbort->head.result){
				// already error occured, so ignore this packet.
				WAN_CHMPRN("Already error occured before this server, but doing.");
			}
			*ppResComPkt = pResComPkt;
		}
	}
	return true;
}

bool ChmEventSock::PxComSendMergeComplete(chmpxid_t chmpxid)
{
	if(CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_MERGE_COMPLETE))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_MERGE_COMPLETE	pMergeComplete	= CVT_COMPTR_MERGE_COMPLETE(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_MERGE_COMPLETE, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pMergeComplete->head.type	= CHMPX_COM_MERGE_COMPLETE;
	pMergeComplete->head.result	= CHMPX_COM_RES_SUCCESS;
	pMergeComplete->head.length	= sizeof(PXCOM_MERGE_COMPLETE);

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_MERGE_COMPLETE to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveMergeComplete(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*				pImData			= pChmCntrl->GetImDataObj();
	PPXCOM_MERGE_COMPLETE	pMergeComplete	= CVT_COMPTR_MERGE_COMPLETE(pComAll);
	chmpxid_t				selfchmpxid		= pImData->GetSelfChmpxId();
	*ppResComPkt							= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around -> next step: do merging
		//
		if(CHMPX_COM_RES_SUCCESS == pMergeComplete->head.result){
			// Succeed
			MSG_CHMPRN("PXCOM_MERGE_COMPLETE is succeed, All server start to merge.");

			// If there is DELETE Done server, do SERVICEOUT status here!
			chmpxid_t	nextchmpxid = GetNextRingChmpxId();
			if(CHM_INVALID_CHMPXID == nextchmpxid){
				MSG_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");
			}
			chmpxid_t	downchmpxid;
			while(CHM_INVALID_CHMPXID != (downchmpxid = pImData->GetChmpxIdByStatus(CHMPXSTS_SRVIN_DOWN_DELETED, false))){
				// change down server status
				if(!pImData->SetServerStatus(downchmpxid, CHMPXSTS_SRVOUT_DOWN_NORMAL)){
					ERR_CHMPRN("Failed to change server(0x%016" PRIx64 ") status to CHMPXSTS_SRVOUT_DOWN_NORMAL.", downchmpxid);
					return false;
				}
				// send down server status update
				CHMPXSVR	chmpxsvr;
				if(!pImData->GetChmpxSvr(downchmpxid, &chmpxsvr)){
					ERR_CHMPRN("Could not get server(0x%016" PRIx64 ") information.", downchmpxid);
					return false;
				}
				if(CHM_INVALID_CHMPXID == nextchmpxid){
					if(!PxComSendSlavesStatusChange(&chmpxsvr)){
						ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status change to slaves.", downchmpxid);
						return false;
					}
				}else{
					if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
						ERR_CHMPRN("Failed to send server(0x%016" PRIx64 ") status change.", downchmpxid);
						return false;
					}
				}
			}
		}else{
			// Something error occured on RING.
			WAN_CHMPRN("PXCOM_MERGE_COMPLETE is failed, maybe other servers are merging now, thus returns true.");
			return true;
		}
	}else{
		// update own chmshm & transfer packet.
		//
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			ERR_CHMPRN("Could not get next server chmpxid.");
			return false;
		}

		if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
			// Deperture chmpx maybe DOWN!
			//
			// [NOTICE]
			// This case is very small case, deperture server sends this packet but that server could not
			// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
			// So we do not transfer this packet, because it could not be stopped in RING.
			//
			ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
			*ppResComPkt = NULL;

		}else{
			// make response data buffer
			PCOMPKT	pResComPkt;
			if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_MERGE_COMPLETE))){
				ERR_CHMPRN("Could not allocation memory.");
				return false;
			}

			// compkt
			PPXCOM_ALL				pResComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
			PPXCOM_MERGE_COMPLETE	pResMergeComplete	= CVT_COMPTR_MERGE_COMPLETE(pResComAll);

			SET_PXCOMPKT(pResComPkt, CHMPX_COM_MERGE_COMPLETE, pComHead->dept_ids.chmpxid, nextchmpxid, false, 0L);	// dept chmpxid is not changed.
			COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

			// merge_start(copy)
			pResMergeComplete->head.type		= pMergeComplete->head.type;
			pResMergeComplete->head.result		= pMergeComplete->head.result;	// always no error.
			pResMergeComplete->head.length		= pMergeComplete->head.length;

			if(CHMPX_COM_RES_SUCCESS != pMergeComplete->head.result){
				// already error occured, so ignore this packet.
				WAN_CHMPRN("Already error occured before this server, but doing.");
				pResMergeComplete->head.result = pMergeComplete->head.result;
			}else{
				// check self status
				chmpxsts_t	status = pImData->GetSelfStatus();
				if(!IS_CHMPXSTS_SRVOUT(status)){
					if((IS_CHMPXSTS_NOACT(status) || IS_CHMPXSTS_ADD(status)) && IS_CHMPXSTS_SUSPEND(status)){
						if(!IS_CHMPXSTS_DONE(status)){
							WAN_CHMPRN("Server status(0x%016" PRIx64 ":%s) is SUSPEND, so could not change status.", status, STR_CHMPXSTS_FULL(status).c_str());
							pResMergeComplete->head.result = CHMPX_COM_RES_ERROR;
						}else{
							// [NOTE]
							// maybe, case of SERVICEIN/UP/NOACT/DONE/SUSPEND
							// This status can be because serviceout command for other server when there many suspend server.
						}
					}
				}else{
					MSG_CHMPRN("Server status(0x%016" PRIx64 ":%s) is SERVICEOUT, so could not change status, but return success", status, STR_CHMPXSTS_FULL(status).c_str());
				}
			}
			*ppResComPkt = pResComPkt;
		}
	}
	return true;
}

bool ChmEventSock::PxComSendServerDown(chmpxid_t chmpxid, chmpxid_t downchmpxid)
{
	if(CHM_INVALID_CHMPXID == chmpxid || CHM_INVALID_CHMPXID == downchmpxid){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_SERVER_DOWN))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL			pComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_SERVER_DOWN	pServerDown	= CVT_COMPTR_SERVER_DOWN(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_SERVER_DOWN, pImData->GetSelfChmpxId(), chmpxid, true, 0L);

	pServerDown->head.type		= CHMPX_COM_SERVER_DOWN;
	pServerDown->head.result	= CHMPX_COM_RES_SUCCESS;
	pServerDown->head.length	= sizeof(PXCOM_SERVER_DOWN);
	pServerDown->chmpxid		= downchmpxid;

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_SERVER_DOWN to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveServerDown(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
	PPXCOM_SERVER_DOWN	pServerDown	= CVT_COMPTR_SERVER_DOWN(pComAll);
	chmpxid_t			selfchmpxid	= pImData->GetSelfChmpxId();
	*ppResComPkt					= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around -> next step: update pending hash value
		//
		if(CHMPX_COM_RES_SUCCESS == pServerDown->head.result){
			// Succeed notify server down & status update on RING
			return true;

		}else{
			// Something error occured on RING, but nothing to do for recovering.
			ERR_CHMPRN("PXCOM_SERVER_DOWN is failed, could not recover...");
			return false;
		}
	}else{
		// update own chmshm & transfer packet.
		//
		// [NOTICE]
		// Before receiving this "SERVER_DOWN", this server got "merge abort" if merging.
		// Thus do not care about merge abort.
		//
		pxcomres_t	ResultCode = CHMPX_COM_RES_SUCCESS;;

		// close down server sock (probably all sockets are already closed)
		socklist_t	socklist;
		if(pImData->GetServerSock(pServerDown->chmpxid, socklist) && !socklist.empty()){
			for(socklist_t::iterator iter = socklist.begin(); iter != socklist.end(); iter = socklist.erase(iter)){
				seversockmap.erase(*iter);
			}
		}

		// set status.
		chmpxsts_t	status = pImData->GetServerStatus(pServerDown->chmpxid);

		CHANGE_CHMPXSTS_TO_DOWN(status);
		if(!pImData->SetServerStatus(pServerDown->chmpxid, status)){
			ERR_CHMPRN("Could not set status(0x%016" PRIx64 ":%s) to chmpxid(0x%016" PRIx64 "), but continue...", status, STR_CHMPXSTS_FULL(status).c_str(), pServerDown->chmpxid);
			ResultCode = CHMPX_COM_RES_ERROR;
		}
		if(CHMPX_COM_RES_SUCCESS != pServerDown->head.result){
			MSG_CHMPRN("Already error occured before this server.");
			ResultCode = CHMPX_COM_RES_ERROR;
		}

		// get next chmpxid on RING
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();

		if(CHM_INVALID_CHMPXID != nextchmpxid){
			if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
				// Deperture chmpx maybe DOWN!
				//
				// [NOTICE]
				// This case is very small case, deperture server sends this packet but that server could not
				// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
				// So we do not transfer this packet, because it could not be stopped in RING.
				//
				ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);
				*ppResComPkt = NULL;

			}else{
				// make response data buffer
				PCOMPKT	pResComPkt;
				if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_SERVER_DOWN))){
					ERR_CHMPRN("Could not allocation memory.");
					return false;
				}

				// compkt
				PPXCOM_ALL			pResComAll		= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
				PPXCOM_SERVER_DOWN	pResServerDown	= CVT_COMPTR_SERVER_DOWN(pResComAll);

				SET_PXCOMPKT(pResComPkt, CHMPX_COM_SERVER_DOWN, pComHead->dept_ids.chmpxid, nextchmpxid, false, 0L);	// dept chmpxid is not changed.
				COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

				// server_down(copy)
				pResServerDown->head.type			= pServerDown->head.type;
				pResServerDown->head.result			= ResultCode;
				pResServerDown->head.length			= pServerDown->head.length;
				pResServerDown->chmpxid				= pServerDown->chmpxid;

				*ppResComPkt = pResComPkt;
			}
		}
	}
	return true;
}

bool ChmEventSock::PxCltReceiveJoinNotify(PCOMHEAD pComHead, PPXCLT_ALL pComAll)
{
	if(!pComHead || !pComAll){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*			pImData		= pChmCntrl->GetImDataObj();
//	PPXCLT_JOIN_NOTIFY	pJoinNotify	= CVT_CLTPTR_JOIN_NOTIFY(pComAll);

	if(!pImData->IsChmpxProcess()){
		ERR_CHMPRN("CHMPX_CLT_JOIN_NOTIFY must be received on Chmpx process.");
		return false;
	}

	// check processes
	//
	// [MEMO]
	// We should check pJoinNotify->pid in pidlist, but we do not it.
	// After receiving this command, we only check client count and change status.
	// 
	if(!pImData->IsClientPids()){
		ERR_CHMPRN("There is no client process.");
		return false;
	}

	// Check status
	chmpxsts_t	status = pImData->GetSelfStatus();
	if(IS_CHMPXSTS_NOSUP(status)){
		MSG_CHMPRN("Already self status(0x%016" PRIx64 ":%s) is NO SUSPEND.", status, STR_CHMPXSTS_FULL(status).c_str());
		return true;
	}

	// new status
	bool	do_merging_now = false;
	if(IS_CHMPXSTS_SRVIN(status) && IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_NOACT(status) && IS_CHMPXSTS_NOTHING(status)){
		// [NOTICE]
		// CHMPXSTS_SRVIN_UP_MERGING is "doing" status, so we need to start merging.
		//
		status			= CHMPXSTS_SRVIN_UP_MERGING;
		do_merging_now	= true;
	}
	CHANGE_CHMPXSTS_TO_NOSUP(status);

	// set status(set suspend in this method)
	if(!pImData->SetSelfStatus(status)){
		ERR_CHMPRN("Failed to change server status(0x%016" PRIx64 ").", status);
		return false;
	}

	// Update pending hash.
	if(!pImData->UpdatePendingHash()){
		ERR_CHMPRN("Failed to update pending hash for all servers, so stop update status.");
		return false;
	}

	// send status update
	CHMPXSVR	chmpxsvr;
	if(!pImData->GetSelfChmpxSvr(&chmpxsvr)){
		ERR_CHMPRN("Could not get self chmpx information.");
		return false;
	}
	chmpxid_t	nextchmpxid = GetNextRingChmpxId();
	if(CHM_INVALID_CHMPXID == nextchmpxid){
		WAN_CHMPRN("Could not get next chmpxid, probably there is no server without self chmpx on RING.");
		if(!PxComSendSlavesStatusChange(&chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change to slaves.");
			return false;
		}
	}else{
		if(!PxComSendStatusChange(nextchmpxid, &chmpxsvr)){
			ERR_CHMPRN("Failed to send self status change.");
			return false;
		}
	}

	// If do not merge(almost random deliver mode), do merging, completing it.
	if(is_auto_merge || do_merging_now){
		// If server does not have been added servicein yet, do it.
		if(IS_CHMPXSTS_SRVOUT(status) && IS_CHMPXSTS_UP(status) && IS_CHMPXSTS_NOACT(status)){
			if(!RequestServiceIn()){
				ERR_CHMPRN("Failed to request \"servce in\" status before auto merging.");
				return false;
			}
		}
		// start merge automatically.
		if(CHM_INVALID_CHMPXID == nextchmpxid){
			if(!MergeStart()){
				ERR_CHMPRN("Failed to merge or complete merge for \"service in\".");
				return false;
			}
		}else{
			if(!RequestMergeStart()){
				ERR_CHMPRN("Failed to merge or complete merge for \"service in\".");
				return false;
			}
		}
	}
	return true;
}

bool ChmEventSock::PxComSendReqUpdateData(chmpxid_t chmpxid, const PPXCOMMON_MERGE_PARAM pmerge_param)
{
	if(CHM_INVALID_CHMPXID == chmpxid || !pmerge_param){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	while(!fullock::flck_trylock_noshared_mutex(&mergeidmap_lockval));	// LOCK

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_REQ_UPDATEDATA, (sizeof(PXCOM_REQ_IDMAP) * mergeidmap.size())))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);		// UNLOCK
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_REQ_UPDATEDATA	pReqUpdateData	= CVT_COMPTR_REQ_UPDATEDATA(pComAll);

	SET_PXCOMPKT(pComPkt, CHMPX_COM_REQ_UPDATEDATA, pImData->GetSelfChmpxId(), chmpxid, true, (sizeof(PXCOM_REQ_IDMAP) * mergeidmap.size()));

	pReqUpdateData->head.type			= CHMPX_COM_REQ_UPDATEDATA;
	pReqUpdateData->head.result			= CHMPX_COM_RES_SUCCESS;
	pReqUpdateData->head.length			= sizeof(PXCOM_REQ_UPDATEDATA) + (sizeof(PXCOM_REQ_IDMAP) * mergeidmap.size());
	pReqUpdateData->count				= static_cast<long>(mergeidmap.size());
	pReqUpdateData->plist_offset		= sizeof(PXCOM_REQ_UPDATEDATA);
	COPY_COMMON_MERGE_PARAM(&(pReqUpdateData->merge_param), pmerge_param);

	// extra area
	PPXCOM_REQ_IDMAP	pIdMap	= CHM_OFFSET(pReqUpdateData, sizeof(PXCOM_REQ_UPDATEDATA), PPXCOM_REQ_IDMAP);
	int					idcnt	= 0;
	for(mergeidmap_t::const_iterator iter = mergeidmap.begin(); iter != mergeidmap.end(); ++iter, ++idcnt){
		pIdMap[idcnt].chmpxid	= iter->first;
		pIdMap[idcnt].req_status= iter->second;
	}

	fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);			// UNLOCK

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_REQ_UPDATEDATA to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveReqUpdateData(PCOMHEAD pComHead, PPXCOM_ALL pComAll, PCOMPKT* ppResComPkt)
{
	if(!pComHead || !pComAll || !ppResComPkt){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*				pImData			= pChmCntrl->GetImDataObj();
	PPXCOM_REQ_UPDATEDATA	pReqUpdateData	= CVT_COMPTR_REQ_UPDATEDATA(pComAll);
	chmpxid_t				selfchmpxid		= pImData->GetSelfChmpxId();
	*ppResComPkt							= NULL;

	if(pComHead->dept_ids.chmpxid == selfchmpxid){
		// around -> next step: update pending hash value
		//
		if(CHMPX_COM_RES_SUCCESS == pReqUpdateData->head.result){
			// Succeed request update data
			while(!fullock::flck_trylock_noshared_mutex(&mergeidmap_lockval));	// LOCK

			// copy all update data resuest status
			PPXCOM_REQ_IDMAP	pReqIdMap	= CHM_OFFSET(pReqUpdateData, sizeof(PXCOM_REQ_UPDATEDATA), PPXCOM_REQ_IDMAP);
			for(long cnt = 0; cnt < pReqUpdateData->count; ++cnt){
				mergeidmap_t::iterator	iter = mergeidmap.find(pReqIdMap[cnt].chmpxid);

				if(mergeidmap.end() == iter){
					// why? there is not chmpxid, but set data
					WAN_CHMPRN("There is not chmpxid(0x%016" PRIx64 ") in map.", pReqIdMap[cnt].chmpxid);
					mergeidmap[pReqIdMap[cnt].chmpxid] = pReqIdMap[cnt].req_status;
				}else{
					if(IS_PXCOM_REQ_UPDATE_RESULT(mergeidmap[pReqIdMap[cnt].chmpxid])){
						WAN_CHMPRN("Already result(%s) for chmpxid(0x%016" PRIx64 ") in map, so not set result(%s)",
							STRPXCOM_REQ_UPDATEDATA(mergeidmap[pReqIdMap[cnt].chmpxid]), pReqIdMap[cnt].chmpxid, STRPXCOM_REQ_UPDATEDATA(pReqIdMap[cnt].req_status));
					}else{
						mergeidmap[pReqIdMap[cnt].chmpxid] = pReqIdMap[cnt].req_status;
					}
				}
			}
			fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);			// UNLOCK
			return true;

		}else{
			// Something error occured on RING, but nothing to do for recovering.
			ERR_CHMPRN("PXCOM_REQ_UPDATEDATA is failed, could not recover...");
			return false;
		}
	}else{
		// get next chmpxid on RING
		chmpxid_t	nextchmpxid	= GetNextRingChmpxId();

		if(CHM_INVALID_CHMPXID != nextchmpxid){
			if(!IsSafeDeptAndNextChmpxId(pComHead->dept_ids.chmpxid, nextchmpxid)){
				// Deperture chmpx maybe DOWN!
				//
				// [NOTICE]
				// This case is very small case, deperture server sends this packet but that server could not
				// be connected from in RING server.(ex. down after sending, or FQDN is wrong, etc)
				// So we do not transfer this packet, because it could not be stopped in RING.
				//
				ERR_CHMPRN("Deperture chmpxid(0x%016" PRIx64 ") maybe down, so stop transferring this packet.", pComHead->dept_ids.chmpxid);

			}else{
				// Make packet
				PCOMPKT	pResComPkt;
				if(NULL == (pResComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_REQ_UPDATEDATA, (sizeof(PXCOM_REQ_IDMAP) * pReqUpdateData->count)))){
					ERR_CHMPRN("Could not allocate memory for COMPKT.");
					return false;
				}

				// compkt(copy)
				PPXCOM_ALL				pComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pResComPkt);
				PPXCOM_REQ_UPDATEDATA	pResUpdateData	= CVT_COMPTR_REQ_UPDATEDATA(pComAll);
				SET_PXCOMPKT(pResComPkt, CHMPX_COM_REQ_UPDATEDATA, pComHead->dept_ids.chmpxid, nextchmpxid, false, (sizeof(PXCOM_REQ_IDMAP) * pReqUpdateData->count));	// dept chmpxid is not changed.
				COPY_TIMESPEC(&(pResComPkt->head.reqtime), &(pComHead->reqtime));	// not change

				pResUpdateData->head.type			= CHMPX_COM_REQ_UPDATEDATA;
				pResUpdateData->head.result			= CHMPX_COM_RES_SUCCESS;
				pResUpdateData->head.length			= sizeof(PXCOM_REQ_UPDATEDATA) + (sizeof(PXCOM_REQ_IDMAP) * pReqUpdateData->count);
				pResUpdateData->count				= pReqUpdateData->count;
				pResUpdateData->plist_offset		= sizeof(PXCOM_REQ_UPDATEDATA);
				COPY_COMMON_MERGE_PARAM(&(pResUpdateData->merge_param), &(pReqUpdateData->merge_param));

				// extra area(copy)
				PPXCOM_REQ_IDMAP	pOwnMap		= NULL;
				PPXCOM_REQ_IDMAP	pReqIdMap	= CHM_OFFSET(pReqUpdateData, sizeof(PXCOM_REQ_UPDATEDATA), PPXCOM_REQ_IDMAP);
				PPXCOM_REQ_IDMAP	pResIdMap	= CHM_OFFSET(pResUpdateData, sizeof(PXCOM_REQ_UPDATEDATA), PPXCOM_REQ_IDMAP);
				for(long cnt = 0; cnt < pReqUpdateData->count; ++cnt){
					pResIdMap[cnt].chmpxid		= pReqIdMap[cnt].chmpxid;
					pResIdMap[cnt].req_status	= pReqIdMap[cnt].req_status;

					if(selfchmpxid == pResIdMap[cnt].chmpxid){
						pOwnMap = &pResIdMap[cnt];
					}
				}

				// own chmpxid in target chmpxid list?
				if(pOwnMap){
					// get own status
					chmpxsts_t	status = pImData->GetSelfStatus();

					// check status
					//
					// [NOTE]
					// Transfer the request to client, if this server is UP/DOWN & NOSUSPEND status.
					// Because we merge data from minimum server which has a valid data.
					//
					if((IS_CHMPXSTS_NOACT(status) || IS_CHMPXSTS_DELETE(status)) && IS_CHMPXSTS_NOSUP(status)){
						// this server needs to update data, so transfer request to client.

						// Make parameter
						PXCOMMON_MERGE_PARAM	MqMergeParam;
						COPY_COMMON_MERGE_PARAM(&MqMergeParam, &(pResUpdateData->merge_param));

						// transfer command to MQ
						if(!pChmCntrl->MergeRequestUpdateData(&MqMergeParam)){
							ERR_CHMPRN("Something error occurred during the request is sent to MQ.");
							pOwnMap->req_status = CHMPX_COM_REQ_UPDATE_FAIL;
						}else{
							pOwnMap->req_status = CHMPX_COM_REQ_UPDATE_DO;
						}
					}else{
						// this server does not need to update, so result is success.
						pOwnMap->req_status = CHMPX_COM_REQ_UPDATE_NOACT;
					}
				}else{
					// this chmpx is not target chmpx.
				}
				// set response data.
				*ppResComPkt = pResComPkt;
			}
		}else{
			// there is no next chnpxid.
		}
	}
	return true;
}

bool ChmEventSock::PxComSendResUpdateData(chmpxid_t chmpxid, size_t length, const unsigned char* pdata, const struct timespec* pts)
{
	if(CHM_INVALID_CHMPXID == chmpxid || 0 == length || !pdata || !pts){
		ERR_CHMPRN("Parameters are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_RES_UPDATEDATA, length))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL				pComAll			= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_RES_UPDATEDATA	pResUpdateData	= CVT_COMPTR_RES_UPDATEDATA(pComAll);

	SET_PXCOMPKT(pComPkt, CHMPX_COM_RES_UPDATEDATA, pImData->GetSelfChmpxId(), chmpxid, true, length);

	pResUpdateData->head.type		= CHMPX_COM_RES_UPDATEDATA;
	pResUpdateData->head.result		= CHMPX_COM_RES_SUCCESS;
	pResUpdateData->head.length		= sizeof(PXCOM_RES_UPDATEDATA) + length;
	pResUpdateData->ts.tv_sec		= pts->tv_sec;
	pResUpdateData->ts.tv_nsec		= pts->tv_nsec;
	pResUpdateData->length			= length;
	pResUpdateData->pdata_offset	= sizeof(PXCOM_RES_UPDATEDATA);

	unsigned char*	psetbase		= CHM_OFFSET(pResUpdateData, sizeof(PXCOM_RES_UPDATEDATA), unsigned char*);
	memcpy(psetbase, pdata, length);

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_RES_UPDATEDATA to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveResUpdateData(PCOMHEAD pComHead, PPXCOM_ALL pComAll)
{
	if(!pComHead || !pComAll){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}

	PPXCOM_RES_UPDATEDATA	pResUpdateData	= CVT_COMPTR_RES_UPDATEDATA(pComAll);

	// check data
	if(0 == pResUpdateData->length || 0 == pResUpdateData->pdata_offset){
		ERR_CHMPRN("Received CHMPX_COM_RES_UPDATEDATA command is somthing wrong.");
		return false;
	}
	unsigned char*	pdata = CHM_OFFSET(pResUpdateData, pResUpdateData->pdata_offset, unsigned char*);

	// transfer client
	return pChmCntrl->MergeSetUpdateData(pResUpdateData->length, pdata, &(pResUpdateData->ts));
}

bool ChmEventSock::PxComSendResultUpdateData(chmpxid_t chmpxid, reqidmapflag_t result_updatedata)
{
	if(CHM_INVALID_CHMPXID == chmpxid){
		ERR_CHMPRN("Parameter is wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	ChmIMData*	pImData = pChmCntrl->GetImDataObj();

	// Make packet
	PCOMPKT	pComPkt;
	if(NULL == (pComPkt = ChmEventSock::AllocatePxComPacket(CHMPX_COM_RESULT_UPDATEDATA))){
		ERR_CHMPRN("Could not allocate memory for COMPKT.");
		return false;
	}

	// compkt
	PPXCOM_ALL					pComAll				= CVT_COM_ALL_PTR_PXCOMPKT(pComPkt);
	PPXCOM_RESULT_UPDATEDATA	pResultUpdateData	= CVT_COMPTR_RESULT_UPDATEDATA(pComAll);
	SET_PXCOMPKT(pComPkt, CHMPX_COM_RESULT_UPDATEDATA, pImData->GetSelfChmpxId(), chmpxid, true, 0);

	pResultUpdateData->head.type	= CHMPX_COM_RESULT_UPDATEDATA;
	pResultUpdateData->head.result	= CHMPX_COM_RES_SUCCESS;
	pResultUpdateData->chmpxid		= pImData->GetSelfChmpxId();
	pResultUpdateData->result		= result_updatedata;

	// Send request
	if(!Send(pComPkt, NULL, 0L)){
		ERR_CHMPRN("Failed to send CHMPX_COM_RESULT_UPDATEDATA to chmpxid(0x%016" PRIx64 ").", chmpxid);
		CHM_Free(pComPkt);
		return false;
	}
	CHM_Free(pComPkt);

	return true;
}

bool ChmEventSock::PxComReceiveResultUpdateData(PCOMHEAD pComHead, PPXCOM_ALL pComAll)
{
	if(!pComHead || !pComAll){
		ERR_CHMPRN("Parameter are wrong.");
		return false;
	}
	if(IsEmpty()){
		ERR_CHMPRN("Object is not initialized.");
		return false;
	}
	PPXCOM_RESULT_UPDATEDATA	pResultUpdateData = CVT_COMPTR_RESULT_UPDATEDATA(pComAll);

	// check data
	if(CHM_INVALID_CHMPXID == pResultUpdateData->chmpxid || !IS_PXCOM_REQ_UPDATE_RESULT(pResultUpdateData->result)){
		ERR_CHMPRN("Received CHMPX_COM_RESULT_UPDATEDATA command is somthing wrong.");
		return false;
	}

	while(!fullock::flck_trylock_noshared_mutex(&mergeidmap_lockval));	// LOCK

	// search map
	mergeidmap_t::iterator	iter = mergeidmap.find(pResultUpdateData->chmpxid);
	if(mergeidmap.end() == iter){
		ERR_CHMPRN("The chmpxid(0x%016" PRIx64 ") in received CHMPX_COM_RESULT_UPDATEDATA does not find in merge id map.", pResultUpdateData->chmpxid);
		fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);		// UNLOCK
		return false;
	}
	if(!IS_PXCOM_REQ_UPDATE_RESULT(pResultUpdateData->result)){
		ERR_CHMPRN("received CHMPX_COM_RESULT_UPDATEDATA result(%s) for chmpxid(0x%016" PRIx64 ") is wrong.", STRPXCOM_REQ_UPDATEDATA(pResultUpdateData->result), pResultUpdateData->chmpxid);
		return false;
	}
	// set result
	mergeidmap[pResultUpdateData->chmpxid] = pResultUpdateData->result;

	fullock::flck_unlock_noshared_mutex(&mergeidmap_lockval);			// UNLOCK
	return true;
}

/*
 * VIM modelines
 *
 * vim:set ts=4 fenc=utf-8:
 */
