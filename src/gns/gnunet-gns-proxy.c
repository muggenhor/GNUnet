/*
     This file is part of GNUnet.
     (C) 2012-2013 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/
/**
 * @author Martin Schanzenbach
 * @author Christian Grothoff
 * @file src/gns/gnunet-gns-proxy.c
 * @brief HTTP(S) proxy that rewrites URIs and fakes certificats to make GNS work
 *        with legacy browsers
 *
 * TODO:
 * - make DNS lookup asynchronous
 * - simplify POST/PUT processing
 * - double-check queueing logic
 * - figure out what to do with the 'authority' issue
 * - document better
 */
#include "platform.h"
#include <microhttpd.h>
#include <curl/curl.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <regex.h>
#include "gnunet_util_lib.h"
#include "gnunet_gns_service.h"
#include "gnunet_identity_service.h"
#include "gns.h"


/**
 * Default Socks5 listen port.
 */ 
#define GNUNET_GNS_PROXY_PORT 7777

/**
 * Maximum supported length for a URI.
 * Should die. @deprecated
 */
#define MAX_HTTP_URI_LENGTH 2048

/**
 * Some buffer size. @deprecated
 */
#define POSTBUFFERSIZE 4096


/**
 * Size of the read/write buffers for Socks.   Uses
 * 256 bytes for the hostname (at most), plus a few
 * bytes overhead for the messages.
 */
#define SOCKS_BUFFERSIZE (256 + 32)

/**
 * Port for plaintext HTTP.
 */
#define HTTP_PORT 80

/**
 * Port for HTTPS.
 */
#define HTTPS_PORT 443

/**
 * Largest allowed size for a PEM certificate.
 */
#define MAX_PEM_SIZE (10 * 1024)

/**
 * After how long do we clean up unused MHD SSL/TLS instances?
 */
#define MHD_CACHE_TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_MINUTES, 5)

/**
 * After how long do we clean up Socks5 handles that failed to show any activity
 * with their respective MHD instance?
 */
#define HTTP_HANDSHAKE_TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 15)


/**
 * Log curl error.
 *
 * @param level log level
 * @param fun name of curl_easy-function that gave the error
 * @param rc return code from curl
 */
#define LOG_CURL_EASY(level,fun,rc) GNUNET_log(level, _("%s failed at %s:%d: `%s'\n"), fun, __FILE__, __LINE__, curl_easy_strerror (rc))


/* *************** Socks protocol definitions (move to TUN?) ****************** */

/**
 * Which SOCKS version do we speak?
 */
#define SOCKS_VERSION_5 0x05

/**
 * Flag to set for 'no authentication'.
 */
#define SOCKS_AUTH_NONE 0


/**
 * Commands in Socks5.
 */ 
enum Socks5Commands
{
  /**
   * Establish TCP/IP stream.
   */
  SOCKS5_CMD_TCP_STREAM = 1,

  /**
   * Establish TCP port binding.
   */
  SOCKS5_CMD_TCP_PORT = 2,

  /**
   * Establish UDP port binding.
   */
  SOCKS5_CMD_UDP_PORT = 3
};


/**
 * Address types in Socks5.
 */ 
enum Socks5AddressType
{
  /**
   * IPv4 address.
   */
  SOCKS5_AT_IPV4 = 1,

  /**
   * IPv4 address.
   */
  SOCKS5_AT_DOMAINNAME = 3,

  /**
   * IPv6 address.
   */
  SOCKS5_AT_IPV6 = 4

};


/**
 * Status codes in Socks5 response.
 */
enum Socks5StatusCode
{
  SOCKS5_STATUS_REQUEST_GRANTED = 0,
  SOCKS5_STATUS_GENERAL_FAILURE = 1,
  SOCKS5_STATUS_CONNECTION_NOT_ALLOWED_BY_RULE = 2,
  SOCKS5_STATUS_NETWORK_UNREACHABLE = 3,
  SOCKS5_STATUS_HOST_UNREACHABLE = 4,
  SOCKS5_STATUS_CONNECTION_REFUSED_BY_HOST = 5,
  SOCKS5_STATUS_TTL_EXPIRED = 6,
  SOCKS5_STATUS_COMMAND_NOT_SUPPORTED = 7,
  SOCKS5_STATUS_ADDRESS_TYPE_NOT_SUPPORTED = 8
};


/**
 * Client hello in Socks5 protocol.
 */
struct Socks5ClientHelloMessage
{
  /**
   * Should be #SOCKS_VERSION_5.
   */
  uint8_t version;

  /**
   * How many authentication methods does the client support.
   */
  uint8_t num_auth_methods;

  /* followed by supported authentication methods, 1 byte per method */

};


/**
 * Server hello in Socks5 protocol.
 */
struct Socks5ServerHelloMessage
{
  /**
   * Should be #SOCKS_VERSION_5.
   */
  uint8_t version;

  /**
   * Chosen authentication method, for us always #SOCKS_AUTH_NONE,
   * which skips the authentication step.
   */
  uint8_t auth_method;
};


/**
 * Client socks request in Socks5 protocol.
 */
struct Socks5ClientRequestMessage
{
  /**
   * Should be #SOCKS_VERSION_5.
   */
  uint8_t version;

  /**
   * Command code, we only uspport #SOCKS5_CMD_TCP_STREAM.
   */
  uint8_t command;

  /**
   * Reserved, always zero.
   */
  uint8_t resvd;

  /**
   * Address type, an `enum Socks5AddressType`.
   */
  uint8_t addr_type;

  /* 
   * Followed by either an ip4/ipv6 address or a domain name with a
   * length field (uint8_t) in front (depending on @e addr_type).
   * followed by port number in network byte order (uint16_t).
   */
};


/**
 * Server response to client requests in Socks5 protocol.
 */
struct Socks5ServerResponseMessage
{
  /**
   * Should be #SOCKS_VERSION_5.
   */
  uint8_t version;

  /**
   * Status code, an `enum Socks5StatusCode`
   */
  uint8_t reply;

  /**
   * Always zero.
   */
  uint8_t reserved;

  /**
   * Address type, an `enum Socks5AddressType`.
   */
  uint8_t addr_type;

  /* 
   * Followed by either an ip4/ipv6 address or a domain name with a
   * length field (uint8_t) in front (depending on @e addr_type).
   * followed by port number in network byte order (uint16_t).
   */

};


/* ***************** Datastructures for Socks handling **************** */


/**
 * The socks phases.  
 */
enum SocksPhase
{
  /**
   * We're waiting to get the client hello.
   */
  SOCKS5_INIT,

  /**
   * We're waiting to get the initial request.
   */
  SOCKS5_REQUEST,

  /**
   * We are currently resolving the destination.
   */
  SOCKS5_RESOLVING,

  /**
   * We're in transfer mode.
   */
  SOCKS5_DATA_TRANSFER,

  /**
   * Finish writing the write buffer, then clean up.
   */
  SOCKS5_WRITE_THEN_CLEANUP,

  /**
   * Socket has been passed to MHD, do not close it anymore.
   */
  SOCKS5_SOCKET_WITH_MHD
};



/**
 * A structure for socks requests
 */
struct Socks5Request
{

  /**
   * DLL.
   */
  struct Socks5Request *next;

  /**
   * DLL.
   */
  struct Socks5Request *prev;

  /**
   * The client socket 
   */
  struct GNUNET_NETWORK_Handle *sock;

  /**
   * Handle to GNS lookup, during #SOCKS5_RESOLVING phase.
   */
  struct GNUNET_GNS_LookupRequest *gns_lookup;

  /**
   * Client socket read task 
   */
  GNUNET_SCHEDULER_TaskIdentifier rtask;

  /**
   * Client socket write task 
   */
  GNUNET_SCHEDULER_TaskIdentifier wtask;

  /**
   * Timeout task 
   */
  GNUNET_SCHEDULER_TaskIdentifier timeout_task;

  /**
   * Read buffer 
   */
  char rbuf[SOCKS_BUFFERSIZE];

  /**
   * Write buffer 
   */
  char wbuf[SOCKS_BUFFERSIZE];

  /**
   * the domain name to server (only important for SSL) 
   */
  char *domain;

  /**
   * DNS Legacy Host Name as given by GNS, NULL if not given.
   */
  char *leho;

  /**
   * The URL to fetch 
   */
  char *url;

  /**
   * Number of bytes already in read buffer 
   */
  size_t rbuf_len;

  /**
   * Number of bytes already in write buffer 
   */
  size_t wbuf_len;
  
  /**
   * Once known, what's the target address for the connection?
   */
  struct sockaddr_storage destination_address;

  /**
   * The socks state 
   */
  enum SocksPhase state;

  /**
   * Desired destination port.
   */
  uint16_t port;

};


/* *********************** Datastructures for HTTP handling ****************** */

/**
 * A structure for CA cert/key
 */
struct ProxyCA
{
  /**
   * The certificate 
   */
  gnutls_x509_crt_t cert;

  /**
   * The private key 
   */
  gnutls_x509_privkey_t key;
};


/**
 * Structure for GNS certificates
 */
struct ProxyGNSCertificate
{
  /**
   * The certificate as PEM 
   */
  char cert[MAX_PEM_SIZE];

  /**
   * The private key as PEM 
   */
  char key[MAX_PEM_SIZE];
};



/**
 * A structure for all running Httpds
 */
struct MhdHttpList
{
  /**
   * DLL for httpds 
   */
  struct MhdHttpList *prev;

  /**
   * DLL for httpds 
   */
  struct MhdHttpList *next;

  /**
   * the domain name to server (only important for SSL) 
   */
  char *domain;

  /**
   * The daemon handle 
   */
  struct MHD_Daemon *daemon;

  /**
   * Optional proxy certificate used
   */
  struct ProxyGNSCertificate *proxy_cert;

  /**
   * The task ID 
   */
  GNUNET_SCHEDULER_TaskIdentifier httpd_task;

  /**
   * is this an ssl daemon? 
   */
  int is_ssl;

};


/* ***************** possibly deprecated data structures ****************** */


/**
 * State machine for the IO buffer.
 */
enum BufferStatus
  {
    BUF_WAIT_FOR_CURL,
    BUF_WAIT_FOR_MHD
  };


/**
 * A structure for MHD<->cURL streams
 */
struct ProxyCurlTask
{
  /**
   * DLL for tasks 
   */
  struct ProxyCurlTask *prev;

  /**
   * DLL for tasks 
   */
  struct ProxyCurlTask *next;

  /**
   * Handle to cURL 
   */
  CURL *curl;

  /**
   * Optional header replacements for curl (LEHO) 
   */
  struct curl_slist *headers;

  /**
   * Optional resolver replacements for curl (LEHO) 
   */
  struct curl_slist *resolver;

  /**
   * curl response code 
   */
  long curl_response_code;

  /**
   * The cURL write buffer / MHD read buffer 
   */
  char buffer[CURL_MAX_WRITE_SIZE];

  /**
   * Should die. @deprecated
   */
  char url[MAX_HTTP_URI_LENGTH];

  /**
   * Read pos of the data in the buffer 
   */
  char *buffer_read_ptr;

  /**
   * Write pos in the buffer 
   */
  char *buffer_write_ptr;

  /**
   * connection 
   */
  struct MHD_Connection *connection;

  /**
   * put
   */
  size_t put_read_offset;
  size_t put_read_size;

  /**
   *post
   */
  struct MHD_PostProcessor *post_handler;

  /* post data */
  struct ProxyUploadData *upload_data_head;
  struct ProxyUploadData *upload_data_tail;

  /**
   * the type of POST encoding 
   */
  char* post_type;

  struct curl_httppost *httppost;

  struct curl_httppost *httppost_last;

  /**
   * Number of bytes in buffer 
   */
  unsigned int bytes_in_buffer;

  /* PP task */
  GNUNET_SCHEDULER_TaskIdentifier pp_task;

  /* The associated daemon list entry */
  struct MhdHttpList *mhd;

  /* The associated response */
  struct MHD_Response *response;

  /* Cookies to set */
  struct ProxySetCookieHeader *set_cookies_head;

  /* Cookies to set */
  struct ProxySetCookieHeader *set_cookies_tail;

  /**
   * The authority of the corresponding host (site of origin) 
   */
  char authority[256];

  /**
   * The hostname (Host header field) 
   */
  char host[256];

  /**
   * The LEgacy HOstname (can be empty) 
   */
  char leho[256];

  /**
   * The port 
   */
  uint16_t port;

  /**
   * The buffer status (BUF_WAIT_FOR_CURL or BUF_WAIT_FOR_MHD) 
   */
  enum BufferStatus buf_status;

  /**
   * connection status 
   */
  int ready_to_queue;

  /**
   * is curl running? 
   */
  int curl_running;
  
  /**
   * are we done 
   */
  int fin;

  /**
   * Already accepted 
   */
  int accepted;

  /**
   * Indicates wheather the download is in progress 
   */
  int download_in_progress;

  /**
   * Indicates wheather the download was successful 
   */
  int download_is_finished;

  /**
   * Indicates wheather the download failed 
   */
  int download_error;

  int post_done;

  int is_httppost;
  
};


/**
 * Struct for set-cookies
 */
struct ProxySetCookieHeader
{
  /**
   * DLL 
   */
  struct ProxySetCookieHeader *next;

  /**
   * DLL 
   */
  struct ProxySetCookieHeader *prev;

  /**
   * the cookie 
   */
  char *cookie;
};


/**
 * Post data structure
 */
struct ProxyUploadData
{
  /**
   * DLL 
   */
  struct ProxyUploadData *next;

  /**
   * DLL 
   */
  struct ProxyUploadData *prev;

  char *key;

  char *filename;

  char *content_type;

  size_t content_length;
  
  /**
   * value 
   */
  char *value;

  /**
   * to copy 
   */
  size_t bytes_left;

  /**
   * size 
   */
  size_t total_bytes;
};


/* *********************** Globals **************************** */


/**
 * The port the proxy is running on (default 7777) 
 */
static unsigned long port = GNUNET_GNS_PROXY_PORT;

/**
 * The CA file (pem) to use for the proxy CA 
 */
static char *cafile_opt;

/**
 * The listen socket of the proxy 
 */
static struct GNUNET_NETWORK_Handle *lsock;

/**
 * The listen task ID 
 */
static GNUNET_SCHEDULER_TaskIdentifier ltask;

/**
 * The cURL download task (curl multi API).
 */
static GNUNET_SCHEDULER_TaskIdentifier curl_download_task;

/**
 * The cURL multi handle 
 */
static CURLM *curl_multi;

/**
 * Handle to the GNS service 
 */
static struct GNUNET_GNS_Handle *gns_handle;

/**
 * DLL for ProxyCurlTasks 
 */
static struct ProxyCurlTask *ctasks_head;

/**
 * DLL for ProxyCurlTasks 
 */
static struct ProxyCurlTask *ctasks_tail;

/**
 * DLL for http/https daemons 
 */
static struct MhdHttpList *mhd_httpd_head;

/**
 * DLL for http/https daemons 
 */
static struct MhdHttpList *mhd_httpd_tail;

/**
 * Daemon for HTTP (we have one per SSL certificate, and then one for
 * all HTTP connections; this is the one for HTTP, not HTTPS).
 */
static struct MhdHttpList *httpd;

/**
 * DLL of active socks requests.
 */
static struct Socks5Request *s5r_head;

/**
 * DLL of active socks requests.
 */
static struct Socks5Request *s5r_tail;

/**
 * The users local GNS master zone 
 */
static struct GNUNET_CRYPTO_EccPublicSignKey local_gns_zone;

/**
 * The users local shorten zone 
 */
static struct GNUNET_CRYPTO_EccPrivateKey local_shorten_zone;

/**
 * Is shortening enabled?
 */
static int do_shorten;

/**
 * The CA for SSL certificate generation 
 */
static struct ProxyCA proxy_ca;

/**
 * Response we return on cURL failures.
 */
static struct MHD_Response *curl_failure_response;

/**
 * Connection to identity service.
 */
static struct GNUNET_IDENTITY_Handle *identity;

/**
 * Request for our ego.
 */
static struct GNUNET_IDENTITY_Operation *id_op;

/**
 * Our configuration.
 */
static const struct GNUNET_CONFIGURATION_Handle *cfg;


/* ************************* Global helpers ********************* */


/**
 * Clean up s5r handles.
 *
 * @param s5r the handle to destroy
 */
static void
cleanup_s5r (struct Socks5Request *s5r)
{
  if (GNUNET_SCHEDULER_NO_TASK != s5r->rtask)
    GNUNET_SCHEDULER_cancel (s5r->rtask);
  if (GNUNET_SCHEDULER_NO_TASK != s5r->timeout_task)
    GNUNET_SCHEDULER_cancel (s5r->timeout_task);
  if (GNUNET_SCHEDULER_NO_TASK != s5r->wtask)
    GNUNET_SCHEDULER_cancel (s5r->wtask);
  if (NULL != s5r->gns_lookup)
    GNUNET_GNS_lookup_cancel (s5r->gns_lookup);
  if (NULL != s5r->sock) 
  {
    if (SOCKS5_SOCKET_WITH_MHD == s5r->state)
      GNUNET_NETWORK_socket_free_memory_only_ (s5r->sock);
    else
      GNUNET_NETWORK_socket_close (s5r->sock);
  }
  GNUNET_CONTAINER_DLL_remove (s5r_head,
			       s5r_tail,
			       s5r);
  GNUNET_free_non_null (s5r->domain);
  GNUNET_free_non_null (s5r->leho);
  GNUNET_free_non_null (s5r->url);
  GNUNET_free (s5r);
}


/**
 * Run MHD now, we have extra data ready for the callback.
 *
 * @param hd the daemon to run now.
 */
static void
run_mhd_now (struct MhdHttpList *hd);


/* *************************** netcat mode *********************** */

#if 0




/**
 * Given a TCP stream and a destination address, forward the stream
 * in both directions.
 *
 * @param cls FIXME
 * @param tc  FIXME
 */
static void
forward_socket_like_ncat (void *cls,
			  const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct hostent *phost;
  uint32_t remote_ip;
  struct sockaddr_in remote_addr;
  struct in_addr *r_sin_addr;

  s5r->remote_sock = GNUNET_NETWORK_socket_create (AF_INET,
						   SOCK_STREAM,
						   0);
  r_sin_addr = (struct in_addr*)(phost->h_addr);
  remote_ip = r_sin_addr->s_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  remote_addr.sin_family = AF_INET;
#if HAVE_SOCKADDR_IN_SIN_LEN
  remote_addr.sin_len = sizeof (remote_addr);
#endif
  remote_addr.sin_addr.s_addr = remote_ip;
  remote_addr.sin_port = *port;
  
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "target server: %s:%u\n", 
	      inet_ntoa(remote_addr.sin_addr),
	      ntohs(*port));
  
  if ((GNUNET_OK !=
       GNUNET_NETWORK_socket_connect ( s5r->remote_sock,
				       (const struct sockaddr*)&remote_addr,
				       sizeof (remote_addr)))
      && (errno != EINPROGRESS))
    {
      GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "connect");
      signal_socks_failure (s5r,
			    SOCKS5_STATUS_NETWORK_UNREACHABLE);
      return;
    }
}
#endif


/* ************************* HTTP handling with cURL *********************** */

static int
con_post_data_iter (void *cls,
                  enum MHD_ValueKind kind,
                  const char *key,
                  const char *filename,
                  const char *content_type,
                  const char *transfer_encoding,
                  const char *data,
                  uint64_t off,
                  size_t size)
{
  struct ProxyCurlTask* ctask = cls;
  struct ProxyUploadData* pdata;
  char* enc;
  char* new_value;
  
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Got POST data (file: %s, content type: %s): '%s=%.*s' at offset %llu size %llu\n",
	      filename, content_type,
              key, (int) size, data, 
	      (unsigned long long) off, 
	      (unsigned long long) size);
  GNUNET_assert (NULL != ctask->post_type);

  if (0 == strcasecmp (MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA,
                       ctask->post_type))
  {
    ctask->is_httppost = GNUNET_YES;
    /* new part */
    if (0 == off)
    {
      pdata = GNUNET_new (struct ProxyUploadData);
      pdata->key = GNUNET_strdup (key);

      if (NULL != filename)
        pdata->filename = GNUNET_strdup (filename);
      if (NULL != content_type)
        pdata->content_type = GNUNET_strdup (content_type);
      pdata->value = GNUNET_malloc (size);
      pdata->total_bytes = size;
      memcpy (pdata->value, data, size);
      GNUNET_CONTAINER_DLL_insert_tail (ctask->upload_data_head,
                                        ctask->upload_data_tail,
                                        pdata);

      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Copied %llu bytes of POST Data\n", 
		  (unsigned long long) size);
      return MHD_YES;
    }
    
    pdata = ctask->upload_data_tail;
    new_value = GNUNET_malloc (size + pdata->total_bytes);
    memcpy (new_value, pdata->value, pdata->total_bytes);
    memcpy (new_value+off, data, size);
    GNUNET_free (pdata->value);
    pdata->value = new_value;
    pdata->total_bytes += size;

    return MHD_YES;
  }

  if (0 != strcasecmp (MHD_HTTP_POST_ENCODING_FORM_URLENCODED,
                       ctask->post_type))
  {
    return MHD_NO;
  }

  ctask->is_httppost = GNUNET_NO;
  
  if (NULL != ctask->curl)
    curl_easy_pause (ctask->curl, CURLPAUSE_CONT);

  if (0 == off)
  {
    enc = curl_easy_escape (ctask->curl, key, 0);
    if (NULL == enc)
      {
	GNUNET_break (0);
	return MHD_NO;
      }
    /* a key */
    pdata = GNUNET_new (struct ProxyUploadData);
    pdata->value = GNUNET_malloc (strlen (enc) + 3);
    if (NULL != ctask->upload_data_head)
    {
      pdata->value[0] = '&';
      memcpy (pdata->value+1, enc, strlen (enc));
    }
    else
      memcpy (pdata->value, enc, strlen (enc));
    pdata->value[strlen (pdata->value)] = '=';
    pdata->bytes_left = strlen (pdata->value);
    pdata->total_bytes = pdata->bytes_left;
    curl_free (enc);

    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Escaped POST key: '%s'\n",
                pdata->value);

    GNUNET_CONTAINER_DLL_insert_tail (ctask->upload_data_head,
                                      ctask->upload_data_tail,
                                      pdata);
  }

  /* a value */
  enc = curl_easy_escape (ctask->curl, data, 0);
  if (NULL == enc)
    {
      GNUNET_break (0);
      return MHD_NO;
    }
  pdata = GNUNET_new (struct ProxyUploadData);
  pdata->value = GNUNET_malloc (strlen (enc) + 1);
  memcpy (pdata->value, enc, strlen (enc));
  pdata->bytes_left = strlen (pdata->value);
  pdata->total_bytes = pdata->bytes_left;
  curl_free (enc);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Escaped POST value: '%s'\n",
              pdata->value);

  GNUNET_CONTAINER_DLL_insert_tail (ctask->upload_data_head,
                                    ctask->upload_data_tail,
                                    pdata);
  return MHD_YES;
}


/**
 * Read HTTP request header field 'Host'
 *
 * @param cls buffer to write to
 * @param kind value kind
 * @param key field key
 * @param value field value
 * @return #MHD_NO when Host found
 */
static int
con_val_iter (void *cls,
              enum MHD_ValueKind kind,
              const char *key,
              const char *value)
{
  struct ProxyCurlTask *ctask = cls;
  char* buf = ctask->host;
  char* port;
  char* cstr;
  const char* hdr_val;
  unsigned int uport;

  if (0 == strcmp ("Host", key))
  {
    port = strchr (value, ':');
    if (NULL != port)
    {
      strncpy (buf, value, port-value);
      port++;
      if ((1 != sscanf (port, "%u", &uport)) ||
           (uport > UINT16_MAX) ||
           (0 == uport))
        GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                    "Unable to parse port!\n");
      else
        ctask->port = (uint16_t) uport;
    }
    else
      strcpy (buf, value);
    return MHD_YES;
  }

  if (0 == strcmp (MHD_HTTP_HEADER_ACCEPT_ENCODING, key))
    hdr_val = "";
  else
    hdr_val = value;

  if (0 == strcasecmp (MHD_HTTP_HEADER_CONTENT_TYPE,
                   key))
  {
    if (0 == strncasecmp (value,
                     MHD_HTTP_POST_ENCODING_FORM_URLENCODED,
                     strlen (MHD_HTTP_POST_ENCODING_FORM_URLENCODED)))
      ctask->post_type = MHD_HTTP_POST_ENCODING_FORM_URLENCODED;
    else if (0 == strncasecmp (value,
                          MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA,
                          strlen (MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA)))
      ctask->post_type = MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA;
    else
      ctask->post_type = NULL;

  }

  cstr = GNUNET_malloc (strlen (key) + strlen (hdr_val) + 3);
  GNUNET_snprintf (cstr, strlen (key) + strlen (hdr_val) + 3,
                   "%s: %s", key, hdr_val);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Client Header: %s\n", cstr);

  ctask->headers = curl_slist_append (ctask->headers, cstr);
  GNUNET_free (cstr);

  return MHD_YES;
}


/**
 * Callback for MHD response
 *
 * @param cls closure
 * @param pos in buffer
 * @param buf buffer
 * @param max space in buffer
 * @return number of bytes written
 */
static ssize_t
mhd_content_cb (void *cls,
                uint64_t pos,
                char* buf,
                size_t max);


/**
 * Check HTTP response header for mime
 *
 * @param buffer curl buffer
 * @param size curl blocksize
 * @param nmemb curl blocknumber
 * @param cls handle
 * @return size of read bytes
 */
static size_t
curl_check_hdr (void *buffer, size_t size, size_t nmemb, void *cls)
{
  size_t bytes = size * nmemb;
  struct ProxyCurlTask *ctask = cls;
  int cookie_hdr_len = strlen (MHD_HTTP_HEADER_SET_COOKIE);
  char hdr_generic[bytes+1];
  char new_cookie_hdr[bytes+strlen (ctask->leho)+1];
  char new_location[MAX_HTTP_URI_LENGTH+500];
  char real_host[264];
  char leho_host[264];
  char* ndup;
  char* tok;
  char* cookie_domain;
  char* hdr_type;
  char* hdr_val;
  int delta_cdomain;
  size_t offset = 0;
  char cors_hdr[strlen (ctask->leho) + strlen ("https://")];
  
  if (NULL == ctask->response)
  {
    /* FIXME: get total size from curl (if available) */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Creating response for %s\n", ctask->url);
    ctask->response = MHD_create_response_from_callback (MHD_SIZE_UNKNOWN,
							 sizeof (ctask->buffer),
							 &mhd_content_cb,
							 ctask,
							 NULL);

    /* if we have a leho add a CORS header */
    if (0 != strcmp ("", ctask->leho))
    {
      /* We could also allow ssl and http here */
      if (ctask->mhd->is_ssl)
        sprintf (cors_hdr, "https://%s", ctask->leho);
      else
        sprintf (cors_hdr, "http://%s", ctask->leho);

      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "MHD: Adding CORS header field %s\n",
                  cors_hdr);

      if (GNUNET_NO == MHD_add_response_header (ctask->response,
                                              "Access-Control-Allow-Origin",
                                              cors_hdr))
      {
        GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "MHD: Error adding CORS header field %s\n",
                  cors_hdr);
      }
    }
    ctask->ready_to_queue = GNUNET_YES;
  }
  if (cookie_hdr_len > bytes)
    return bytes;

  memcpy (hdr_generic, buffer, bytes);
  hdr_generic[bytes] = '\0';
  /* remove crlf */
  if ('\n' == hdr_generic[bytes-1])
    hdr_generic[bytes-1] = '\0';

  if (hdr_generic[bytes-2] == '\r')
    hdr_generic[bytes-2] = '\0';
  
  if (0 == memcmp (hdr_generic,
                   MHD_HTTP_HEADER_SET_COOKIE,
                   cookie_hdr_len))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Looking for cookie in: `%s'\n", hdr_generic);    
    ndup = GNUNET_strdup (hdr_generic+cookie_hdr_len+1);
    memset (new_cookie_hdr, 0, sizeof (new_cookie_hdr));
    for (tok = strtok (ndup, ";"); tok != NULL; tok = strtok (NULL, ";"))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  "Got Cookie token: %s\n", tok);
      //memcpy (new_cookie_hdr+offset, tok, strlen (tok));
      if (0 == memcmp (tok, " domain", strlen (" domain")))
      {
        cookie_domain = tok + strlen (" domain") + 1;

        GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                    "Got Set-Cookie Domain: %s\n", cookie_domain);

        if (strlen (cookie_domain) < strlen (ctask->leho))
        {
          delta_cdomain = strlen (ctask->leho) - strlen (cookie_domain);
          if (0 == strcmp (cookie_domain, ctask->leho + (delta_cdomain)))
          {
            GNUNET_snprintf (new_cookie_hdr+offset,
                             sizeof (new_cookie_hdr),
                             " domain=%s", ctask->authority);
            offset += strlen (" domain=") + strlen (ctask->authority);
            new_cookie_hdr[offset] = ';';
            offset++;
            continue;
          }
        }
        else if (strlen (cookie_domain) == strlen (ctask->leho))
        {
          if (0 == strcmp (cookie_domain, ctask->leho))
          {
            GNUNET_snprintf (new_cookie_hdr+offset,
                             sizeof (new_cookie_hdr),
                             " domain=%s", ctask->host);
            offset += strlen (" domain=") + strlen (ctask->host);
            new_cookie_hdr[offset] = ';';
            offset++;
            continue;
          }
        }
        GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                    "Cookie domain invalid\n");

        
      }
      memcpy (new_cookie_hdr+offset, tok, strlen (tok));
      offset += strlen (tok);
      new_cookie_hdr[offset] = ';';
      offset++;
    }
    
    GNUNET_free (ndup);

    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Got Set-Cookie HTTP header %s\n", new_cookie_hdr);

    if (GNUNET_NO == MHD_add_response_header (ctask->response,
                                              MHD_HTTP_HEADER_SET_COOKIE,
                                              new_cookie_hdr))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "MHD: Error adding set-cookie header field %s\n",
                  hdr_generic+cookie_hdr_len+1);
    }
    return bytes;
  }

  ndup = GNUNET_strdup (hdr_generic);
  hdr_type = strtok (ndup, ":");

  if (NULL == hdr_type)
  {
    GNUNET_free (ndup);
    return bytes;
  }

  hdr_val = strtok (NULL, "");

  if (NULL == hdr_val)
  {
    GNUNET_free (ndup);
    return bytes;
  }

  hdr_val++;

  if (0 == strcasecmp (MHD_HTTP_HEADER_LOCATION, hdr_type))
  {
    if (ctask->mhd->is_ssl)
    {
      sprintf (leho_host, "https://%s", ctask->leho);
      sprintf (real_host, "https://%s", ctask->host);
    }
    else
    {
      sprintf (leho_host, "http://%s", ctask->leho);
      sprintf (real_host, "http://%s", ctask->host);
    }

    if (0 == memcmp (leho_host, hdr_val, strlen (leho_host)))
    {
      sprintf (new_location, "%s%s", real_host, hdr_val+strlen (leho_host));
      hdr_val = new_location;
    }
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Trying to set %s: %s\n",
              hdr_type,
              hdr_val);
  if (GNUNET_NO == MHD_add_response_header (ctask->response,
                                            hdr_type,
                                            hdr_val))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "MHD: Error adding %s header field %s\n",
                hdr_type,
                hdr_val);
  }
  GNUNET_free (ndup);
  return bytes;
}



/**
 * Ask cURL for the select sets and schedule download
 */
static void
curl_download_prepare (void);


/**
 * Callback to free content
 *
 * @param cls content to free
 * @param tc task context
 */
static void
mhd_content_free (void *cls,
                  const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ProxyCurlTask *ctask = cls;
  struct ProxyUploadData *pdata;

  if (NULL != ctask->headers)
    curl_slist_free_all (ctask->headers);

  if (NULL != ctask->headers)
    curl_slist_free_all (ctask->resolver);

  if (NULL != ctask->response)
    MHD_destroy_response (ctask->response);

  if (NULL != ctask->post_handler)
    MHD_destroy_post_processor (ctask->post_handler);

  if (GNUNET_SCHEDULER_NO_TASK != ctask->pp_task)
    GNUNET_SCHEDULER_cancel (ctask->pp_task);

  for (pdata = ctask->upload_data_head; NULL != pdata; pdata = ctask->upload_data_head)
  {
    GNUNET_CONTAINER_DLL_remove (ctask->upload_data_head,
                                 ctask->upload_data_tail,
                                 pdata);
    GNUNET_free_non_null (pdata->filename);
    GNUNET_free_non_null (pdata->content_type);
    GNUNET_free_non_null (pdata->key);
    GNUNET_free_non_null (pdata->value);
    GNUNET_free (pdata);
  }
  GNUNET_free (ctask);
}


/**
 * Callback for MHD response
 *
 * @param cls closure
 * @param pos in buffer
 * @param buf buffer
 * @param max space in buffer
 * @return number of bytes written
 */
static ssize_t
mhd_content_cb (void *cls,
                uint64_t pos,
                char* buf,
                size_t max)
{
  struct ProxyCurlTask *ctask = cls;
  ssize_t copied = 0;
  size_t bytes_to_copy = ctask->buffer_write_ptr - ctask->buffer_read_ptr;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "MHD: content cb for %s. To copy: %u\n",
              ctask->url, (unsigned int) bytes_to_copy);
  if ((GNUNET_YES == ctask->download_is_finished) &&
      (GNUNET_NO == ctask->download_error) &&
      (0 == bytes_to_copy))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "MHD: sending response for %s\n", ctask->url);
    ctask->download_in_progress = GNUNET_NO;
    run_mhd_now (ctask->mhd);
    GNUNET_SCHEDULER_add_now (&mhd_content_free, ctask);
    return MHD_CONTENT_READER_END_OF_STREAM;
  }
  
  if ((GNUNET_YES == ctask->download_error) &&
      (GNUNET_YES == ctask->download_is_finished) &&
      (0 == bytes_to_copy))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "MHD: sending error response\n");
    ctask->download_in_progress = GNUNET_NO;
    run_mhd_now (ctask->mhd);
    GNUNET_SCHEDULER_add_now (&mhd_content_free, ctask);
    return MHD_CONTENT_READER_END_WITH_ERROR;
  }

  if ( ctask->buf_status == BUF_WAIT_FOR_CURL )
    return 0;
  
  copied = 0;
  bytes_to_copy = ctask->buffer_write_ptr - ctask->buffer_read_ptr;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "MHD: copied: %d left: %u, space left in buf: %d\n",
              copied,
              (unsigned int) bytes_to_copy, (int) (max - copied));
  
  if (GNUNET_NO == ctask->download_is_finished)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "MHD: Purging buffer\n");
    memmove (ctask->buffer, ctask->buffer_read_ptr, bytes_to_copy);
    ctask->buffer_read_ptr = ctask->buffer;
    ctask->buffer_write_ptr = ctask->buffer + bytes_to_copy;
    ctask->buffer[bytes_to_copy] = '\0';
  }
  
  if (bytes_to_copy + copied > max)
    bytes_to_copy = max - copied;
  memcpy (buf+copied, ctask->buffer_read_ptr, bytes_to_copy);
  ctask->buffer_read_ptr += bytes_to_copy;
  copied += bytes_to_copy;
  ctask->buf_status = BUF_WAIT_FOR_CURL;
  
  if (NULL != ctask->curl)
    curl_easy_pause (ctask->curl, CURLPAUSE_CONT);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "MHD: copied %d bytes\n", (int) copied);
  run_mhd_now (ctask->mhd);
  return copied;
}


/**
 * Handle data from cURL
 *
 * @param ptr pointer to the data
 * @param size number of blocks of data
 * @param nmemb blocksize
 * @param ctx the curlproxytask
 * @return number of bytes handled
 */
static size_t
curl_download_cb (void *ptr, size_t size, size_t nmemb, void* ctx)
{
  const char *cbuf = ptr;
  size_t total = size * nmemb;
  struct ProxyCurlTask *ctask = ctx;
  size_t buf_space = sizeof (ctask->buffer) - (ctask->buffer_write_ptr - ctask->buffer);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "CURL: Got %d. %d free in buffer\n",
              (int) total,
	      (int) buf_space);
  if (0 == buf_space)
  {
    ctask->buf_status = BUF_WAIT_FOR_MHD;
    run_mhd_now (ctask->mhd);
    return CURL_WRITEFUNC_PAUSE;
  }
  if (total > buf_space)
    total = buf_space;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "CURL: Copying %d bytes to buffer (%s)\n", 
	      total, ctask->url);
  memcpy (ctask->buffer_write_ptr, cbuf, total);
  ctask->bytes_in_buffer += total;
  ctask->buffer_write_ptr += total;
  if (ctask->bytes_in_buffer > 0)
  {
    ctask->buf_status = BUF_WAIT_FOR_MHD;
    run_mhd_now (ctask->mhd);
  }
  return total;
}


/**
 * cURL callback for put data
 */
static size_t
put_read_callback (void *buf, size_t size, size_t nmemb, void *cls)
{
  struct ProxyCurlTask *ctask = cls;
  struct ProxyUploadData *pdata = ctask->upload_data_head;
  size_t len = size * nmemb;
  size_t to_copy;
  char* pos;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "CURL: put read callback\n");

  if (NULL == pdata)
    return CURL_READFUNC_PAUSE;
  
  //fin
  if (NULL == pdata->value)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "CURL: Terminating PUT\n");

    GNUNET_CONTAINER_DLL_remove (ctask->upload_data_head,
                                 ctask->upload_data_tail,
                                 pdata);
    GNUNET_free (pdata);
    return 0;
  }
 
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "CURL: read callback value %s\n", pdata->value); 
  
  to_copy = pdata->bytes_left;
  if (to_copy > len)
    to_copy = len;
  
  pos = pdata->value + (pdata->total_bytes - pdata->bytes_left);
  memcpy (buf, pos, to_copy);
  pdata->bytes_left -= to_copy;
  if (pdata->bytes_left <= 0)
  {
    GNUNET_free (pdata->value);
    GNUNET_CONTAINER_DLL_remove (ctask->upload_data_head,
                                 ctask->upload_data_tail,
                                 pdata);
    GNUNET_free (pdata);
  }
  return to_copy;
}


/**
 * cURL callback for post data
 */
static size_t
post_read_callback (void *buf, size_t size, size_t nmemb, void *cls)
{
  struct ProxyCurlTask *ctask = cls;
  struct ProxyUploadData *pdata = ctask->upload_data_head;
  size_t len = size * nmemb;
  size_t to_copy;
  char* pos;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "CURL: read callback\n");

  if (NULL == pdata)
    return CURL_READFUNC_PAUSE;
  
  //fin
  if (NULL == pdata->value)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "CURL: Terminating POST data\n");

    GNUNET_CONTAINER_DLL_remove (ctask->upload_data_head,
                                 ctask->upload_data_tail,
                                 pdata);
    GNUNET_free (pdata);
    return 0;
  }
 
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "CURL: read callback value %s\n", pdata->value); 
  
  to_copy = pdata->bytes_left;
  if (to_copy > len)
    to_copy = len;
  
  pos = pdata->value + (pdata->total_bytes - pdata->bytes_left);
  memcpy (buf, pos, to_copy);
  pdata->bytes_left -= to_copy;
  if (pdata->bytes_left <= 0)
  {
    GNUNET_free (pdata->value);
    GNUNET_CONTAINER_DLL_remove (ctask->upload_data_head,
                                 ctask->upload_data_tail,
                                 pdata);
    GNUNET_free (pdata);
  }
  return to_copy;
}


/**
 * Task that is run when we are ready to receive more data
 * from curl
 *
 * @param cls closure
 * @param tc task context
 */
static void
curl_task_download (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc);


/**
 * Ask cURL for the select sets and schedule download
 */
static void
curl_download_prepare ()
{
  CURLMcode mret;
  fd_set rs;
  fd_set ws;
  fd_set es;
  int max;
  struct GNUNET_NETWORK_FDSet *grs;
  struct GNUNET_NETWORK_FDSet *gws;
  long to;
  struct GNUNET_TIME_Relative rtime;

  max = -1;
  FD_ZERO (&rs);
  FD_ZERO (&ws);
  FD_ZERO (&es);
  if (CURLM_OK != (mret = curl_multi_fdset (curl_multi, &rs, &ws, &es, &max)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "%s failed at %s:%d: `%s'\n",
                "curl_multi_fdset", __FILE__, __LINE__,
                curl_multi_strerror (mret));
    //TODO cleanup here?
    return;
  }
  to = -1;
  GNUNET_break (CURLM_OK == curl_multi_timeout (curl_multi, &to));
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "cURL multi fds: max=%d timeout=%lld\n", max, (long long) to);
  if (-1 == to)
    rtime = GNUNET_TIME_UNIT_FOREVER_REL;
  else
    rtime = GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_MILLISECONDS, to);
  grs = GNUNET_NETWORK_fdset_create ();
  gws = GNUNET_NETWORK_fdset_create ();
  GNUNET_NETWORK_fdset_copy_native (grs, &rs, max + 1);
  GNUNET_NETWORK_fdset_copy_native (gws, &ws, max + 1);
  if (curl_download_task != GNUNET_SCHEDULER_NO_TASK)
    GNUNET_SCHEDULER_cancel (curl_download_task);  
  if (-1 != max)
  {
    curl_download_task =
      GNUNET_SCHEDULER_add_select (GNUNET_SCHEDULER_PRIORITY_DEFAULT,
                                   rtime,
                                   grs, gws,
                                   &curl_task_download, curl_multi);
  }
  else if (NULL != ctasks_head)
  {
    /* as specified in curl docs */
    curl_download_task = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_MILLISECONDS,
                                                       &curl_task_download,
                                                       curl_multi);
  }
  GNUNET_NETWORK_fdset_destroy (gws);
  GNUNET_NETWORK_fdset_destroy (grs);
}


/**
 * Task that is run when we are ready to receive more data
 * from curl
 *
 * @param cls closure
 * @param tc task context
 */
static void
curl_task_download (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  int running;
  int msgnum;
  struct CURLMsg *msg;
  CURLMcode mret;
  struct ProxyCurlTask *ctask;
  int num_ctasks;
  long resp_code;
  struct ProxyCurlTask *clean_head = NULL;
  struct ProxyCurlTask *clean_tail = NULL;

  curl_download_task = GNUNET_SCHEDULER_NO_TASK;

  if (0 != (tc->reason & GNUNET_SCHEDULER_REASON_SHUTDOWN))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Shutdown requested while trying to download\n");
    //TODO cleanup
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Ready to dl\n");

  do
  {
    running = 0;
    num_ctasks = 0;
    
    mret = curl_multi_perform (curl_multi, &running);

    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Running curl tasks: %d\n", running);
    for (ctask = ctasks_head; NULL != ctask; ctask = ctask->next)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "CTask: %s\n", ctask->url);
      num_ctasks++;
    }

    do
    {
      
      msg = curl_multi_info_read (curl_multi, &msgnum);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Messages left: %d\n", msgnum);
      
      if (msg == NULL)
        break;
      switch (msg->msg)
      {
       case CURLMSG_DONE:
         if ((msg->data.result != CURLE_OK) &&
             (msg->data.result != CURLE_GOT_NOTHING))
         {
           GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                       "Download curl failed");
            
           for (ctask = ctasks_head; NULL != ctask; ctask = ctask->next)
           {
             if (NULL == ctask->curl)
               continue;

             if (memcmp (msg->easy_handle, ctask->curl, sizeof (CURL)) != 0)
               continue;
             
             GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                         "CURL: Download failed for task %s: %s.\n",
                         ctask->url,
                         curl_easy_strerror (msg->data.result));
             ctask->download_is_finished = GNUNET_YES;
             ctask->download_error = GNUNET_YES;
             if (CURLE_OK == curl_easy_getinfo (ctask->curl,
                                                CURLINFO_RESPONSE_CODE,
                                                &resp_code))
               ctask->curl_response_code = resp_code;
             ctask->ready_to_queue = MHD_YES;
             ctask->buf_status = BUF_WAIT_FOR_MHD;
             run_mhd_now (ctask->mhd);
             
             GNUNET_CONTAINER_DLL_remove (ctasks_head, ctasks_tail,
                                          ctask);
             GNUNET_CONTAINER_DLL_insert (clean_head, clean_tail, ctask);
             break;
           }
           GNUNET_assert (ctask != NULL);
         }
         else
         {
           GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                       "CURL: download completed.\n");

           for (ctask = ctasks_head; NULL != ctask; ctask = ctask->next)
           {
             if (NULL == ctask->curl)
               continue;

             if (0 != memcmp (msg->easy_handle, ctask->curl, sizeof (CURL)))
               continue;
             
             GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                         "CURL: completed task %s found.\n", ctask->url);
             if (CURLE_OK == curl_easy_getinfo (ctask->curl,
                                                CURLINFO_RESPONSE_CODE,
                                                &resp_code))
               ctask->curl_response_code = resp_code;


             GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                         "CURL: Completed ctask!\n");
             if (GNUNET_SCHEDULER_NO_TASK == ctask->pp_task)
	     {
	       ctask->buf_status = BUF_WAIT_FOR_MHD;
	       run_mhd_now (ctask->mhd);
             }

             ctask->ready_to_queue = MHD_YES;
             ctask->download_is_finished = GNUNET_YES;

             /* We MUST not modify the multi handle else we loose messages */
             GNUNET_CONTAINER_DLL_remove (ctasks_head, ctasks_tail,
                                          ctask);
             GNUNET_CONTAINER_DLL_insert (clean_head, clean_tail, ctask);

             break;
           }
           GNUNET_assert (ctask != NULL);
         }
         GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                     "CURL: %s\n", curl_easy_strerror(msg->data.result));
         break;
       default:
         GNUNET_assert (0);
         break;
      }
    } while (msgnum > 0);

    for (ctask=clean_head; NULL != ctask; ctask = ctask->next)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "CURL: Removing task %s.\n", ctask->url);
      curl_multi_remove_handle (curl_multi, ctask->curl);
      curl_easy_cleanup (ctask->curl);
      ctask->curl = NULL;
    }
    
    num_ctasks=0;
    for (ctask=ctasks_head; NULL != ctask; ctask = ctask->next)    
      num_ctasks++; 
    GNUNET_assert (num_ctasks == running);

  } while (mret == CURLM_CALL_MULTI_PERFORM);
  
  if (mret != CURLM_OK)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "CURL: %s failed at %s:%d: `%s'\n",
                "curl_multi_perform", __FILE__, __LINE__,
                curl_multi_strerror (mret));
  }
  curl_download_prepare();
}


/**
 * Process LEHO lookup
 *
 * @param cls the ctask
 * @param rd_count number of records returned
 * @param rd record data
 */
static void
process_leho_lookup (void *cls,
                     uint32_t rd_count,
                     const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct ProxyCurlTask *ctask = cls;
  char hosthdr[262]; //256 + "Host: "
  int i;
  CURLcode ret;
  CURLMcode mret;
  struct hostent *phost;
  char *ssl_ip;
  char resolvename[512];
  char curlurl[512];

  
  strcpy (ctask->leho, "");

  if (rd_count == 0)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "No LEHO present!\n");

  for (i=0; i<rd_count; i++)
  {
    if (rd[i].record_type != GNUNET_NAMESTORE_TYPE_LEHO)
      continue;

    memcpy (ctask->leho, rd[i].data, rd[i].data_size);

    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Found LEHO %s for %s\n", ctask->leho, ctask->url);
  }

  if (0 != strcmp (ctask->leho, ""))
  {
    sprintf (hosthdr, "%s%s:%d", "Host: ", ctask->leho, ctask->port);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "New HTTP header value: %s\n", hosthdr);
    ctask->headers = curl_slist_append (ctask->headers, hosthdr);
    GNUNET_assert (NULL != ctask->headers);
    if (CURLE_OK != (ret = curl_easy_setopt (ctask->curl, CURLOPT_HTTPHEADER, ctask->headers)))
      LOG_CURL_EASY(GNUNET_ERROR_TYPE_WARNING,"curl_easy_setopt",ret);
  }

  if (ctask->mhd->is_ssl)
  {
    phost = (struct hostent*)gethostbyname (ctask->host);

    if (phost!=NULL)
    {
      ssl_ip = inet_ntoa(*((struct in_addr*)(phost->h_addr)));
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "SSL target server: %s\n", ssl_ip);
      sprintf (resolvename, "%s:%d:%s", ctask->leho, HTTPS_PORT, ssl_ip);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Curl resolve: %s\n", resolvename);
      ctask->resolver = curl_slist_append ( ctask->resolver, resolvename);
      if (CURLE_OK != (ret = curl_easy_setopt (ctask->curl, CURLOPT_RESOLVE, ctask->resolver)))
	LOG_CURL_EASY(GNUNET_ERROR_TYPE_WARNING,"curl_easy_setopt",ret);
      sprintf (curlurl, "https://%s:%d%s", ctask->leho, ctask->port, ctask->url);
      if (CURLE_OK != (ret = curl_easy_setopt (ctask->curl, CURLOPT_URL, curlurl)))
	LOG_CURL_EASY(GNUNET_ERROR_TYPE_WARNING,"curl_easy_setopt",ret);
    }
    else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "gethostbyname failed for %s!\n",
		  ctask->host);
      ctask->download_is_finished = GNUNET_YES;
      ctask->download_error = GNUNET_YES;
      return;
    }
  }

  if (CURLM_OK != (mret=curl_multi_add_handle (curl_multi, ctask->curl)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "%s failed at %s:%d: `%s'\n",
                "curl_multi_add_handle", __FILE__, __LINE__,
                curl_multi_strerror (mret));
    ctask->download_is_finished = GNUNET_YES;
    ctask->download_error = GNUNET_YES;
    return;
  }
  GNUNET_CONTAINER_DLL_insert (ctasks_head, ctasks_tail, ctask);

  curl_download_prepare ();
}




/**
 * Main MHD callback for handling requests.
 *
 * @param cls unused
 * @param con MHD connection handle
 * @param url the url in the request
 * @param meth the HTTP method used ("GET", "PUT", etc.)
 * @param ver the HTTP version string (i.e. "HTTP/1.1")
 * @param upload_data the data being uploaded (excluding HEADERS,
 *        for a POST that fits into memory and that is encoded
 *        with a supported encoding, the POST data will NOT be
 *        given in upload_data and is instead available as
 *        part of MHD_get_connection_values; very large POST
 *        data *will* be made available incrementally in
 *        upload_data)
 * @param upload_data_size set initially to the size of the
 *        @a upload_data provided; the method must update this
 *        value to the number of bytes NOT processed;
 * @param con_cls pointer to location where we store the 'struct Request'
 * @return #MHD_YES if the connection was handled successfully,
 *         #MHD_NO if the socket must be closed due to a serious
 *         error while handling the request
 */
static int
create_response (void *cls,
                 struct MHD_Connection *con,
                 const char *url,
                 const char *meth,
                 const char *ver,
                 const char *upload_data,
                 size_t *upload_data_size,
                 void **con_cls)
{
  struct MhdHttpList* hd = cls;  
  char curlurl[MAX_HTTP_URI_LENGTH]; // buffer overflow!
  int ret = MHD_YES;
  int i;
  struct ProxyCurlTask *ctask = *con_cls;
  struct ProxyUploadData *fin_post;
  struct curl_forms forms[5];
  struct ProxyUploadData *upload_data_iter;
  
  //FIXME handle
  if ((0 != strcasecmp (meth, MHD_HTTP_METHOD_GET)) &&
      (0 != strcasecmp (meth, MHD_HTTP_METHOD_PUT)) &&
      (0 != strcasecmp (meth, MHD_HTTP_METHOD_POST)) &&
      (0 != strcasecmp (meth, MHD_HTTP_METHOD_HEAD)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "MHD: %s NOT IMPLEMENTED!\n", meth);
    return MHD_NO;
  }


  if (GNUNET_NO == ctask->accepted)
  {

    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Got %s request for %s\n", meth, url);
    ctask->mhd = hd;
    ctask->curl = curl_easy_init();
    ctask->curl_running = GNUNET_NO;
    if (NULL == ctask->curl)
    {
      ret = MHD_queue_response (con,
                                MHD_HTTP_OK,
                                curl_failure_response);
      GNUNET_free (ctask);
      return ret;
    }
    
    if (ctask->mhd->is_ssl)
      ctask->port = HTTPS_PORT;
    else
      ctask->port = HTTP_PORT;

    MHD_get_connection_values (con,
                               MHD_HEADER_KIND,
                               &con_val_iter, ctask);
    
    curl_easy_setopt (ctask->curl, CURLOPT_HEADERFUNCTION, &curl_check_hdr);
    curl_easy_setopt (ctask->curl, CURLOPT_HEADERDATA, ctask);
    curl_easy_setopt (ctask->curl, CURLOPT_WRITEFUNCTION, &curl_download_cb);
    curl_easy_setopt (ctask->curl, CURLOPT_WRITEDATA, ctask);
    curl_easy_setopt (ctask->curl, CURLOPT_FOLLOWLOCATION, 0);
    curl_easy_setopt (ctask->curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    if (GNUNET_NO == ctask->mhd->is_ssl)
    {
      sprintf (curlurl, "http://%s:%d%s", ctask->host, ctask->port, ctask->url);
      curl_easy_setopt (ctask->curl, CURLOPT_URL, curlurl);
    }
    

    curl_easy_setopt (ctask->curl, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt (ctask->curl, CURLOPT_CONNECTTIMEOUT, 600L);
    curl_easy_setopt (ctask->curl, CURLOPT_TIMEOUT, 600L);
    
    /* Add GNS header */
    ctask->headers = curl_slist_append (ctask->headers,
                                          "GNS: YES");
    ctask->accepted = GNUNET_YES;
    ctask->download_in_progress = GNUNET_YES;
    ctask->buf_status = BUF_WAIT_FOR_CURL;
    ctask->connection = con;
    ctask->curl_response_code = MHD_HTTP_OK;
    ctask->buffer_read_ptr = ctask->buffer;
    ctask->buffer_write_ptr = ctask->buffer;
    ctask->pp_task = GNUNET_SCHEDULER_NO_TASK;
    

    if (0 == strcasecmp (meth, MHD_HTTP_METHOD_PUT))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Setting up PUT\n");
      
      curl_easy_setopt (ctask->curl, CURLOPT_UPLOAD, 1);
      curl_easy_setopt (ctask->curl, CURLOPT_READDATA, ctask);
      curl_easy_setopt (ctask->curl, CURLOPT_READFUNCTION, &put_read_callback);
      ctask->headers = curl_slist_append (ctask->headers,
                                          "Transfer-Encoding: chunked");
    }

    if (0 == strcasecmp (meth, MHD_HTTP_METHOD_POST))
    {
      //FIXME handle multipart
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Setting up POST processor\n");
      ctask->post_handler = MHD_create_post_processor (con,
						       POSTBUFFERSIZE,
						       &con_post_data_iter,
						       ctask);
      ctask->headers = curl_slist_append (ctask->headers,
                                         "Transfer-Encoding: chunked");
      return MHD_YES;
    }

    if (0 == strcasecmp (meth, MHD_HTTP_METHOD_HEAD))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Setting NOBODY\n");
      curl_easy_setopt (ctask->curl, CURLOPT_NOBODY, 1);
    }

    
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "MHD: Adding new curl task for %s\n", ctask->host);
#if 0
    GNUNET_GNS_get_authority (gns_handle,
                              ctask->host,
                              &process_get_authority,
                              ctask);
#endif
    ctask->ready_to_queue = GNUNET_NO;
    ctask->fin = GNUNET_NO;
    ctask->curl_running = GNUNET_YES;
    return MHD_YES;
  }

  ctask = (struct ProxyCurlTask *) *con_cls;
  if (0 == strcasecmp (meth, MHD_HTTP_METHOD_POST))
  {
    if (0 != *upload_data_size)
    {
      
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  "Invoking POST processor\n");
      MHD_post_process (ctask->post_handler,
                        upload_data, *upload_data_size);
      *upload_data_size = 0;
      if ((GNUNET_NO == ctask->is_httppost) &&
          (GNUNET_NO == ctask->curl_running))
      {
        curl_easy_setopt (ctask->curl, CURLOPT_POST, 1);
        curl_easy_setopt (ctask->curl, CURLOPT_READFUNCTION,
                          &post_read_callback);
        curl_easy_setopt (ctask->curl, CURLOPT_READDATA, ctask);
        
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "MHD: Adding new curl task for %s\n", ctask->host);
#if 0
        GNUNET_GNS_get_authority (gns_handle,
                                  ctask->host,
                                  &process_get_authority,
                                  ctask);
#endif
        ctask->ready_to_queue = GNUNET_NO;
        ctask->fin = GNUNET_NO;
        ctask->curl_running = GNUNET_YES;
      }
      return MHD_YES;
    }
    else if (GNUNET_NO == ctask->post_done)
    {
      if (GNUNET_YES == ctask->is_httppost)
      {
        for (upload_data_iter = ctask->upload_data_head;
             NULL != upload_data_iter;
             upload_data_iter = upload_data_iter->next)
        {
          i = 0;
          if (NULL != upload_data_iter->filename)
          {
            forms[i].option = CURLFORM_FILENAME;
            forms[i].value = upload_data_iter->filename;
            GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                        "Adding filename %s\n",
                        forms[i].value);
            i++;
          }
          if (NULL != upload_data_iter->content_type)
          {
            forms[i].option = CURLFORM_CONTENTTYPE;
            forms[i].value = upload_data_iter->content_type;
            GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                        "Adding content type %s\n",
                        forms[i].value);
            i++;
          }
          forms[i].option = CURLFORM_PTRCONTENTS;
          forms[i].value = upload_data_iter->value;
          forms[i+1].option = CURLFORM_END;

          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Adding formdata for %s (len=%lld)\n",
                      upload_data_iter->key,
                      upload_data_iter->total_bytes);

          curl_formadd(&ctask->httppost, &ctask->httppost_last,
                       CURLFORM_COPYNAME, upload_data_iter->key,
                       CURLFORM_CONTENTSLENGTH, upload_data_iter->total_bytes,
                       CURLFORM_ARRAY, forms,
                       CURLFORM_END);
        }
        curl_easy_setopt (ctask->curl, CURLOPT_HTTPPOST,
                          ctask->httppost);

        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "MHD: Adding new curl task for %s\n", ctask->host);
#if 0
        GNUNET_GNS_get_authority (gns_handle,
                                  ctask->host,
                                  &process_get_authority,
                                  ctask);
#endif
        ctask->ready_to_queue = GNUNET_YES;
        ctask->fin = GNUNET_NO;
        ctask->curl_running = GNUNET_YES;
        ctask->post_done = GNUNET_YES;
        return MHD_YES;
      }

      fin_post = GNUNET_new (struct ProxyUploadData);
      GNUNET_CONTAINER_DLL_insert_tail (ctask->upload_data_head,
                                        ctask->upload_data_tail,
                                        fin_post);
      ctask->post_done = GNUNET_YES;
      return MHD_YES;
    }
  }
  
  if (GNUNET_YES != ctask->ready_to_queue)
    return MHD_YES; /* wait longer */
  
  if (GNUNET_YES == ctask->fin)
    return MHD_YES;

  ctask->fin = GNUNET_YES;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "MHD: Queueing response for %s\n", ctask->url);
  ret = MHD_queue_response (con, ctask->curl_response_code, ctask->response);
  run_mhd_now (ctask->mhd);
  return ret;
}


/* ******************** MHD HTTP setup and event loop ******************** */


/**
 * Function called when MHD decides that we are done with a connection.
 *
 * @param cls NULL
 * @param connection connection handle
 * @param con_cls value as set by the last call to
 *        the #MHD_AccessHandlerCallback, should be our `struct Socks5Request`
 * @param toe reason for request termination (ignored)
 */
static void
mhd_completed_cb (void *cls,
		  struct MHD_Connection *connection,
		  void **con_cls,
		  enum MHD_RequestTerminationCode toe)
{
  struct Socks5Request *s5r = *con_cls;

  if (NULL == s5r)
    return;
  cleanup_s5r (s5r);
  *con_cls = NULL;  
}


/**
 * Function called when MHD first processes an incoming connection.
 * Gives us the respective URI information.
 *
 * We use this to associate the `struct MHD_Connection` with our 
 * internal `struct Socks5Request` data structure (by checking
 * for matching sockets).
 *
 * @param cls the HTTP server handle (a `struct MhdHttpList`)
 * @param url the URL that is being requested
 * @param connection MHD connection object for the request
 * @return the `struct Socks5Request` that this @a connection is for
 */
static void *
mhd_log_callback (void *cls, 
		  const char *url,
		  struct MHD_Connection *connection)
{
  struct Socks5Request *s5r;
  const union MHD_ConnectionInfo *ci;
  int sock;

  ci = MHD_get_connection_info (connection,
				MHD_CONNECTION_INFO_CONNECTION_FD);
  if (NULL == ci) 
  {
    GNUNET_break (0);
    return NULL;
  }
  sock = ci->connect_fd;
  for (s5r = s5r_head; NULL != s5r; s5r = s5r->next)
  {
    if (GNUNET_NETWORK_get_fd (s5r->sock) == sock)
    {
      if (NULL != s5r->url)
      {
	GNUNET_break (0);
	return NULL;
      }
      s5r->url = GNUNET_strdup (url);
      return s5r;
    }
  }
  return NULL;
}


/**
 * Kill the given MHD daemon.
 *
 * @param hd daemon to stop
 */
static void
kill_httpd (struct MhdHttpList *hd)
{
  GNUNET_CONTAINER_DLL_remove (mhd_httpd_head,
			       mhd_httpd_tail,
			       hd);
  GNUNET_free_non_null (hd->domain);
  MHD_stop_daemon (hd->daemon);
  if (GNUNET_SCHEDULER_NO_TASK != hd->httpd_task)
    GNUNET_SCHEDULER_cancel (hd->httpd_task);
  GNUNET_free_non_null (hd->proxy_cert);
  if (hd == httpd)
    httpd = NULL;
  GNUNET_free (hd);
}


/**
 * Task run whenever HTTP server is idle for too long. Kill it.
 *
 * @param cls the `struct MhdHttpList *`
 * @param tc sched context
 */
static void
kill_httpd_task (void *cls,
		 const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct MhdHttpList *hd = cls;
  
  kill_httpd (hd);
}


/**
 * Task run whenever HTTP server operations are pending.
 *
 * @param cls the `struct MhdHttpList *` of the daemon that is being run
 * @param tc sched context
 */
static void
do_httpd (void *cls,
          const struct GNUNET_SCHEDULER_TaskContext *tc);


/**
 * Schedule MHD.  This function should be called initially when an
 * MHD is first getting its client socket, and will then automatically
 * always be called later whenever there is work to be done.
 *
 * @param hd the daemon to schedule
 */
static void
schedule_httpd (struct MhdHttpList *hd)
{
  fd_set rs;
  fd_set ws;
  fd_set es;
  struct GNUNET_NETWORK_FDSet *wrs;
  struct GNUNET_NETWORK_FDSet *wws;
  int max;
  int haveto;
  MHD_UNSIGNED_LONG_LONG timeout;
  struct GNUNET_TIME_Relative tv;

  FD_ZERO (&rs);
  FD_ZERO (&ws);
  FD_ZERO (&es);
  max = -1;
  if (MHD_YES != MHD_get_fdset (hd->daemon, &rs, &ws, &es, &max))
  {
    kill_httpd (hd);
    return;
  }
  haveto = MHD_get_timeout (hd->daemon, &timeout);
  if (MHD_YES == haveto)
    tv.rel_value_us = (uint64_t) timeout * 1000LL;
  else
    tv = GNUNET_TIME_UNIT_FOREVER_REL;
  if (-1 != max)
  {
    wrs = GNUNET_NETWORK_fdset_create ();
    wws = GNUNET_NETWORK_fdset_create ();
    GNUNET_NETWORK_fdset_copy_native (wrs, &rs, max + 1);
    GNUNET_NETWORK_fdset_copy_native (wws, &ws, max + 1);
  }
  else
  {
    wrs = NULL;
    wws = NULL;
  }
  if (GNUNET_SCHEDULER_NO_TASK != hd->httpd_task)
    GNUNET_SCHEDULER_cancel (hd->httpd_task);
  if ( (MHD_YES != haveto) &&
       (-1 == max) &&
       (hd != httpd) )
  {
    /* daemon is idle, kill after timeout */
    hd->httpd_task = GNUNET_SCHEDULER_add_delayed (MHD_CACHE_TIMEOUT,
						   &kill_httpd_task,
						   hd);
  }
  else
  {
    hd->httpd_task =
      GNUNET_SCHEDULER_add_select (GNUNET_SCHEDULER_PRIORITY_DEFAULT,
				   tv, wrs, wws,
				   &do_httpd, hd);
  }
  if (NULL != wrs)
    GNUNET_NETWORK_fdset_destroy (wrs);
  if (NULL != wws)
    GNUNET_NETWORK_fdset_destroy (wws);
}


/**
 * Task run whenever HTTP server operations are pending.
 *
 * @param cls the `struct MhdHttpList` of the daemon that is being run
 * @param tc scheduler context
 */
static void
do_httpd (void *cls,
          const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct MhdHttpList *hd = cls;
  
  hd->httpd_task = GNUNET_SCHEDULER_NO_TASK; 
  MHD_run (hd->daemon);
  schedule_httpd (hd);
}


/**
 * Run MHD now, we have extra data ready for the callback.
 *
 * @param hd the daemon to run now.
 */
static void
run_mhd_now (struct MhdHttpList *hd)
{
  if (GNUNET_SCHEDULER_NO_TASK != 
      hd->httpd_task)
    GNUNET_SCHEDULER_cancel (hd->httpd_task);
  hd->httpd_task = GNUNET_SCHEDULER_add_now (&do_httpd, 
					     hd);
}


/**
 * Read file in filename
 *
 * @param filename file to read
 * @param size pointer where filesize is stored
 * @return NULL on error
 */
static void*
load_file (const char* filename, 
	   unsigned int* size)
{
  void *buffer;
  uint64_t fsize;

  if (GNUNET_OK !=
      GNUNET_DISK_file_size (filename, &fsize,
			     GNUNET_YES, GNUNET_YES))
    return NULL;
  if (fsize > MAX_PEM_SIZE)
    return NULL;
  *size = (unsigned int) fsize;
  buffer = GNUNET_malloc (*size);
  if (fsize != GNUNET_DISK_fn_read (filename, buffer, (size_t) fsize))
  {
    GNUNET_free (buffer);
    return NULL;
  }
  return buffer;
}


/**
 * Load PEM key from file
 *
 * @param key where to store the data
 * @param keyfile path to the PEM file
 * @return #GNUNET_OK on success
 */
static int
load_key_from_file (gnutls_x509_privkey_t key, 
		    const char* keyfile)
{
  gnutls_datum_t key_data;
  int ret;

  key_data.data = load_file (keyfile, &key_data.size);
  ret = gnutls_x509_privkey_import (key, &key_data,
                                    GNUTLS_X509_FMT_PEM);
  if (GNUTLS_E_SUCCESS != ret)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                _("Unable to import private key from file `%s'\n"),
		keyfile);
    GNUNET_break (0);
  }
  GNUNET_free (key_data.data);
  return (GNUTLS_E_SUCCESS != ret) ? GNUNET_SYSERR : GNUNET_OK;
}


/**
 * Load cert from file
 *
 * @param crt struct to store data in
 * @param certfile path to pem file
 * @return #GNUNET_OK on success
 */
static int
load_cert_from_file (gnutls_x509_crt_t crt, 
		     const char* certfile)
{
  gnutls_datum_t cert_data;
  int ret;

  cert_data.data = load_file (certfile, &cert_data.size);
  ret = gnutls_x509_crt_import (crt, &cert_data,
                                GNUTLS_X509_FMT_PEM);
  if (GNUTLS_E_SUCCESS != ret)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
               _("Unable to import certificate %s\n"), certfile);
    GNUNET_break (0);
  }
  GNUNET_free (cert_data.data);
  return (GNUTLS_E_SUCCESS != ret) ? GNUNET_SYSERR : GNUNET_OK;
}


/**
 * Generate new certificate for specific name
 *
 * @param name the subject name to generate a cert for
 * @return a struct holding the PEM data, NULL on error
 */
static struct ProxyGNSCertificate *
generate_gns_certificate (const char *name)
{
  unsigned int serial;
  size_t key_buf_size;
  size_t cert_buf_size;
  gnutls_x509_crt_t request;
  time_t etime;
  struct tm *tm_data;
  struct ProxyGNSCertificate *pgc;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Generating TLS/SSL certificate for `%s'\n", 
	      name);
  GNUNET_break (GNUTLS_E_SUCCESS == gnutls_x509_crt_init (&request));
  GNUNET_break (GNUTLS_E_SUCCESS == gnutls_x509_crt_set_key (request, proxy_ca.key));
  pgc = GNUNET_new (struct ProxyGNSCertificate);
  gnutls_x509_crt_set_dn_by_oid (request, GNUTLS_OID_X520_COUNTRY_NAME,
                                 0, "TNR", 2);
  gnutls_x509_crt_set_dn_by_oid (request, GNUTLS_OID_X520_ORGANIZATION_NAME,
                                 0, "GNU Name System", 4);
  gnutls_x509_crt_set_dn_by_oid (request, GNUTLS_OID_X520_COMMON_NAME,
                                 0, name, strlen (name));
  GNUNET_break (GNUTLS_E_SUCCESS == gnutls_x509_crt_set_version (request, 3));
  gnutls_rnd (GNUTLS_RND_NONCE, &serial, sizeof (serial));
  gnutls_x509_crt_set_serial (request,
			      &serial,
			      sizeof (serial));
  etime = time (NULL);
  tm_data = localtime (&etime);  
  gnutls_x509_crt_set_activation_time (request,
				       etime);
  tm_data->tm_year++;
  etime = mktime (tm_data);
  gnutls_x509_crt_set_expiration_time (request,
				       etime);
  gnutls_x509_crt_sign (request, 
			proxy_ca.cert, 
			proxy_ca.key);
  key_buf_size = sizeof (pgc->key);
  cert_buf_size = sizeof (pgc->cert);
  gnutls_x509_crt_export (request, GNUTLS_X509_FMT_PEM,
                          pgc->cert, &cert_buf_size);
  gnutls_x509_privkey_export (proxy_ca.key, GNUTLS_X509_FMT_PEM,
			      pgc->key, &key_buf_size);
  gnutls_x509_crt_deinit (request);
  return pgc;
}


/**
 * Lookup (or create) an SSL MHD instance for a particular domain.
 *
 * @param domain the domain the SSL daemon has to serve
 * @return NULL on errro
 */
static struct MhdHttpList *
lookup_ssl_httpd (const char* domain)
{
  struct MhdHttpList *hd;
  struct ProxyGNSCertificate *pgc;

  for (hd = mhd_httpd_head; NULL != hd; hd = hd->next)
    if (0 == strcmp (hd->domain, domain))
      return hd;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Starting fresh MHD HTTPS instance for domain `%s'\n",
	      domain);
  pgc = generate_gns_certificate (domain);   
  hd = GNUNET_new (struct MhdHttpList);
  hd->is_ssl = GNUNET_YES;
  hd->domain = GNUNET_strdup (domain); 
  hd->proxy_cert = pgc;
  hd->daemon = MHD_start_daemon (MHD_USE_DEBUG | MHD_USE_SSL | MHD_USE_NO_LISTEN_SOCKET,
				 0,
				 NULL, NULL,
				 &create_response, hd,
				 MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 16,
				 MHD_OPTION_NOTIFY_COMPLETED, &mhd_completed_cb, NULL,
				 MHD_OPTION_URI_LOG_CALLBACK, &mhd_log_callback, NULL,
				 MHD_OPTION_HTTPS_MEM_KEY, pgc->key,
				 MHD_OPTION_HTTPS_MEM_CERT, pgc->cert,
				 MHD_OPTION_END);
  if (NULL == hd->daemon)
  {
    GNUNET_free (pgc);
    GNUNET_free (hd);
    return NULL;
  }
  GNUNET_CONTAINER_DLL_insert (mhd_httpd_head, 
			       mhd_httpd_tail, 
			       hd);
  return hd;
}


/**
 * Task run when a Socks5Request somehow fails to be associated with
 * an MHD connection (i.e. because the client never speaks HTTP after
 * the SOCKS5 handshake).  Clean up.
 *
 * @param cls the `struct Socks5Request *`
 * @param tc sched context
 */
static void
timeout_s5r_handshake (void *cls,
		       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Socks5Request *s5r = cls;

  s5r->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  cleanup_s5r (s5r);
}


/**
 * Checks if name is in given @a tld.
 *
 * @param name the name to check 
 * @param tld the TLD to check for (must NOT begin with ".")
 * @return #GNUNET_YES or #GNUNET_NO
 */
static int
is_tld (const char* name, const char* tld)
{
  size_t name_len = strlen (name);
  size_t tld_len = strlen (tld);

  GNUNET_break ('.' != tld[0]);
  return ( (tld_len < name_len) &&
	   ( ('.' == name[name_len - tld_len - 1]) || (name_len == tld_len) ) &&
	   (0 == memcmp (tld,
			 name + (name_len - tld_len),
			 tld_len)) );
}


/**
 * We're done with the Socks5 protocol, now we need to pass the
 * connection data through to the final destination, either 
 * direct (if the protocol might not be HTTP), or via MHD
 * (if the port looks like it should be HTTP).
 *
 * @param s5r socks request that has reached the final stage
 */
static void
setup_data_transfer (struct Socks5Request *s5r)
{
  struct MhdHttpList *hd;
  int fd;
  const struct sockaddr *addr;
  socklen_t len;

  if (is_tld (s5r->domain, GNUNET_GNS_TLD) ||
      is_tld (s5r->domain, GNUNET_GNS_TLD_ZKEY))
  {
    /* GNS TLD, setup MHD server for destination */
    switch (s5r->port)
    {
    case HTTPS_PORT:
      hd = lookup_ssl_httpd (s5r->domain);
      if (NULL == hd)
      {
	GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		    _("Failed to start HTTPS server for `%s'\n"),
		    s5r->domain);
	cleanup_s5r (s5r);
	return;
      }
      break;
    case HTTP_PORT:
      GNUNET_assert (NULL == httpd);
      hd = httpd;
      break;
    default:
      hd = NULL; /* netcat */
      break;
    }
  }
  else
  {
    hd = NULL; /* netcat */
  }
  if (NULL != hd)
  {
    fd = GNUNET_NETWORK_get_fd (s5r->sock);
    addr = GNUNET_NETWORK_get_addr (s5r->sock);
    len = GNUNET_NETWORK_get_addrlen (s5r->sock);
    s5r->state = SOCKS5_SOCKET_WITH_MHD;
    if (MHD_YES != MHD_add_connection (hd->daemon, fd, addr, len))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
		  _("Failed to pass client to MHD\n"));
      cleanup_s5r (s5r);
      return;
    }
    schedule_httpd (hd);
    s5r->timeout_task = GNUNET_SCHEDULER_add_delayed (HTTP_HANDSHAKE_TIMEOUT,
						      &timeout_s5r_handshake,
						      s5r);
  }
  else
  {
    // FIXME: not implemented
    GNUNET_break (0);
    /* start netcat mode here! */
  }
}


/* ********************* SOCKS handling ************************* */


/**
 * Write data from buffer to socks5 client, then continue with state machine.
 *
 * @param cls the closure with the `struct Socks5Request`
 * @param tc scheduler context
 */
static void
do_write (void *cls,
	  const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Socks5Request *s5r = cls;
  ssize_t len;

  s5r->wtask = GNUNET_SCHEDULER_NO_TASK;
  len = GNUNET_NETWORK_socket_send (s5r->sock,
				    s5r->wbuf,
				    s5r->wbuf_len);
  if (len <= 0)
  {
    /* write error: connection closed, shutdown, etc.; just clean up */
    cleanup_s5r (s5r); 
    return;
  }
  memmove (s5r->wbuf,
	   &s5r->wbuf[len],
	   s5r->wbuf_len - len);
  s5r->wbuf_len -= len;
  if (s5r->wbuf_len > 0)
  {
    /* not done writing */
    s5r->wtask =
      GNUNET_SCHEDULER_add_write_net (GNUNET_TIME_UNIT_FOREVER_REL,
				      s5r->sock,
				      &do_write, s5r);
    return;
  }

  /* we're done writing, continue with state machine! */

  switch (s5r->state)
  {
  case SOCKS5_INIT:    
    GNUNET_assert (0);
    break;
  case SOCKS5_REQUEST:    
    GNUNET_assert (GNUNET_SCHEDULER_NO_TASK != s5r->rtask);
    break;
  case SOCKS5_DATA_TRANSFER:
    setup_data_transfer (s5r);
    return;
  case SOCKS5_WRITE_THEN_CLEANUP:
    cleanup_s5r (s5r);
    return;
  default:
    GNUNET_break (0);
    break;
  }
}


/**
 * Return a server response message indicating a failure to the client.
 *
 * @param s5r request to return failure code for
 * @param sc status code to return
 */
static void
signal_socks_failure (struct Socks5Request *s5r,
		      enum Socks5StatusCode sc)
{
  struct Socks5ServerResponseMessage *s_resp;

  s_resp = (struct Socks5ServerResponseMessage *) &s5r->wbuf[s5r->wbuf_len];
  memset (s_resp, 0, sizeof (struct Socks5ServerResponseMessage));
  s_resp->version = SOCKS_VERSION_5;
  s_resp->reply = sc;
  s5r->state = SOCKS5_WRITE_THEN_CLEANUP;
  if (GNUNET_SCHEDULER_NO_TASK != s5r->wtask)
    s5r->wtask = 
      GNUNET_SCHEDULER_add_write_net (GNUNET_TIME_UNIT_FOREVER_REL,
				      s5r->sock,
				      &do_write, s5r);
}


/**
 * Return a server response message indicating success.
 *
 * @param s5r request to return success status message for
 */
static void
signal_socks_success (struct Socks5Request *s5r)
{
  struct Socks5ServerResponseMessage *s_resp;

  s_resp = (struct Socks5ServerResponseMessage *) &s5r->wbuf[s5r->wbuf_len];
  s_resp->version = SOCKS_VERSION_5;
  s_resp->reply = SOCKS5_STATUS_REQUEST_GRANTED;
  s_resp->reserved = 0;
  s_resp->addr_type = SOCKS5_AT_IPV4;
  /* zero out IPv4 address and port */
  memset (&s_resp[1], 
	  0, 
	  sizeof (struct in_addr) + sizeof (uint16_t));
  s5r->wbuf_len += sizeof (struct Socks5ServerResponseMessage) +
    sizeof (struct in_addr) + sizeof (uint16_t);  
  if (GNUNET_SCHEDULER_NO_TASK == s5r->wtask)      
    s5r->wtask =
      GNUNET_SCHEDULER_add_write_net (GNUNET_TIME_UNIT_FOREVER_REL,
				      s5r->sock,
				      &do_write, s5r); 
}


/**
 * Process GNS results for target domain.
 *
 * @param cls the ctask
 * @param rd_count number of records returned
 * @param rd record data
 */
static void
handle_gns_result (void *cls,
		   uint32_t rd_count,
		   const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct Socks5Request *s5r = cls;
  uint32_t i;
  const struct GNUNET_NAMESTORE_RecordData *r;
  int got_ip;

  s5r->gns_lookup = NULL;
  got_ip = GNUNET_NO;
  for (i=0;i<rd_count;i++)
  {
    r = &rd[i];
    switch (r->record_type)
    {
    case GNUNET_DNSPARSER_TYPE_A:
      {
	struct sockaddr_in *in;

	if (sizeof (struct in_addr) != r->data_size)
	{
	  GNUNET_break_op (0);
	  break;
	}
	if (GNUNET_YES == got_ip)
	  break;
	if (GNUNET_OK != 
	    GNUNET_NETWORK_test_pf (PF_INET))
	  break;
	got_ip = GNUNET_YES;
      	in = (struct sockaddr_in *) &s5r->destination_address;
	in->sin_family = AF_INET;
	memcpy (&in->sin_addr,
		r->data,
		r->data_size);
	in->sin_port = htons (s5r->port);
#if HAVE_SOCKADDR_IN_SIN_LEN
	in->sin_len = sizeof (*in);
#endif
      }
      break;
    case GNUNET_DNSPARSER_TYPE_AAAA: 
      {
	struct sockaddr_in6 *in;

	if (sizeof (struct in6_addr) != r->data_size)
	{
	  GNUNET_break_op (0);
	  break;
	}
	if (GNUNET_YES == got_ip)
	  break; 
	if (GNUNET_OK != 
	    GNUNET_NETWORK_test_pf (PF_INET))
	  break;
	/* FIXME: allow user to disable IPv6 per configuration option... */
	got_ip = GNUNET_YES;
      	in = (struct sockaddr_in6 *) &s5r->destination_address;
	in->sin6_family = AF_INET6;
	memcpy (&in->sin6_addr,
		r->data,
		r->data_size);
	in->sin6_port = htons (s5r->port);
#if HAVE_SOCKADDR_IN_SIN_LEN
	in->sin6_len = sizeof (*in);
#endif
      }
      break;      
    case GNUNET_NAMESTORE_TYPE_VPN:
      GNUNET_break (0); /* should have been translated within GNS */
      break;
    case GNUNET_NAMESTORE_TYPE_LEHO:
      GNUNET_free_non_null (s5r->leho);
      s5r->leho = GNUNET_strndup (r->data,
				  r->data_size);
      break;
    default:
      /* don't care */
      break;
    }
  }
  if (GNUNET_YES != got_ip)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Name resolution failed to yield useful IP address.\n");
    signal_socks_failure (s5r,
			  SOCKS5_STATUS_GENERAL_FAILURE);
    return;
  }
  s5r->state = SOCKS5_DATA_TRANSFER;
  signal_socks_success (s5r);  
}


/**
 * Remove the first @a len bytes from the beginning of the read buffer.
 *
 * @param s5r the handle clear the read buffer for
 * @param len number of bytes in read buffer to advance
 */
static void
clear_from_s5r_rbuf (struct Socks5Request *s5r,
		     size_t len)
{
  GNUNET_assert (len <= s5r->rbuf_len);
  memmove (s5r->rbuf,
	   &s5r->rbuf[len],
	   s5r->rbuf_len - len);
  s5r->rbuf_len -= len;
}


/**
 * Read data from incoming Socks5 connection
 *
 * @param cls the closure with the `struct Socks5Request`
 * @param tc the scheduler context
 */
static void
do_s5r_read (void *cls,
	     const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Socks5Request *s5r = cls;
  const struct Socks5ClientHelloMessage *c_hello;
  struct Socks5ServerHelloMessage *s_hello;
  const struct Socks5ClientRequestMessage *c_req;
  ssize_t rlen;
  size_t alen;

  s5r->rtask = GNUNET_SCHEDULER_NO_TASK;
  if ( (NULL != tc->read_ready) &&
       (GNUNET_NETWORK_fdset_isset (tc->read_ready, s5r->sock)) )
  {
    rlen = GNUNET_NETWORK_socket_recv (s5r->sock, 
				       &s5r->rbuf[s5r->rbuf_len],
				       sizeof (s5r->rbuf) - s5r->rbuf_len);
    if (rlen <= 0)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		  "socks5 client disconnected.\n");
      cleanup_s5r (s5r);
      return;
    }
    s5r->rbuf_len += rlen;
  }
  s5r->rtask = GNUNET_SCHEDULER_add_read_net (GNUNET_TIME_UNIT_FOREVER_REL,
					      s5r->sock,
					      &do_s5r_read, s5r);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Processing %u bytes of socks data in state %d\n",
	      s5r->rbuf_len,
	      s5r->state);
  switch (s5r->state)
  {
  case SOCKS5_INIT:
    c_hello = (const struct Socks5ClientHelloMessage*) &s5r->rbuf;
    if ( (s5r->rbuf_len < sizeof (struct Socks5ClientHelloMessage)) ||
	 (s5r->rbuf_len < sizeof (struct Socks5ClientHelloMessage) + c_hello->num_auth_methods) )
      return; /* need more data */
    if (SOCKS_VERSION_5 != c_hello->version)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Unsupported socks version %d\n"),
		  (int) c_hello->version);
      cleanup_s5r (s5r);
      return;
    }
    clear_from_s5r_rbuf (s5r,
			 sizeof (struct Socks5ClientHelloMessage) + c_hello->num_auth_methods);
    GNUNET_assert (0 == s5r->wbuf_len);
    s_hello = (struct Socks5ServerHelloMessage *) &s5r->wbuf;
    s5r->wbuf_len = sizeof (struct Socks5ServerHelloMessage);
    s_hello->version = SOCKS_VERSION_5;
    s_hello->auth_method = SOCKS_AUTH_NONE;
    GNUNET_assert (GNUNET_SCHEDULER_NO_TASK == s5r->wtask);
    s5r->wtask = GNUNET_SCHEDULER_add_write_net (GNUNET_TIME_UNIT_FOREVER_REL,
						 s5r->sock,
						 &do_write, s5r);
    s5r->state = SOCKS5_REQUEST;
    return;
  case SOCKS5_REQUEST:
    c_req = (const struct Socks5ClientRequestMessage *) &s5r->rbuf;
    if (s5r->rbuf_len < sizeof (struct Socks5ClientRequestMessage))
      return;
    switch (c_req->command)
    {
    case SOCKS5_CMD_TCP_STREAM:
      /* handled below */
      break;
    default:
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Unsupported socks command %d\n"),
		  (int) c_req->command);
      signal_socks_failure (s5r,
			    SOCKS5_STATUS_COMMAND_NOT_SUPPORTED);
      return;
    }
    switch (c_req->addr_type)
    {
    case SOCKS5_AT_IPV4:
      {
	const struct in_addr *v4 = (const struct in_addr *) &c_req[1];
	const uint16_t *port = (const uint16_t *) &v4[1];
	struct sockaddr_in *in;

	s5r->port = ntohs (*port);
	alen = sizeof (struct in_addr);
	if (s5r->rbuf_len < sizeof (struct Socks5ClientRequestMessage) +
	    alen + sizeof (uint16_t))
	  return; /* need more data */
	in = (struct sockaddr_in *) &s5r->destination_address;
	in->sin_family = AF_INET;
	in->sin_addr = *v4;
	in->sin_port = *port;
#if HAVE_SOCKADDR_IN_SIN_LEN
	in->sin_len = sizeof (*in);
#endif
	s5r->state = SOCKS5_DATA_TRANSFER;
      }
      break;
    case SOCKS5_AT_IPV6:
      {
	const struct in6_addr *v6 = (const struct in6_addr *) &c_req[1];
	const uint16_t *port = (const uint16_t *) &v6[1];
	struct sockaddr_in6 *in;

	s5r->port = ntohs (*port);
	alen = sizeof (struct in6_addr);
	if (s5r->rbuf_len < sizeof (struct Socks5ClientRequestMessage) +
	    alen + sizeof (uint16_t))
	  return; /* need more data */
	in = (struct sockaddr_in6 *) &s5r->destination_address;
	in->sin6_family = AF_INET6;
	in->sin6_addr = *v6;
	in->sin6_port = *port;
#if HAVE_SOCKADDR_IN_SIN_LEN
	in->sin6_len = sizeof (*in);
#endif
	s5r->state = SOCKS5_DATA_TRANSFER;
      }
      break;
    case SOCKS5_AT_DOMAINNAME:
      {
	const uint8_t *dom_len;
	const char *dom_name;
	const uint16_t *port;	
	
	dom_len = (const uint8_t *) &c_req[1];
	alen = *dom_len + 1;
	if (s5r->rbuf_len < sizeof (struct Socks5ClientRequestMessage) +
	    alen + sizeof (uint16_t))
	  return; /* need more data */
	dom_name = (const char *) &dom_len[1];
	port = (const uint16_t*) &dom_name[*dom_len];
	s5r->domain = GNUNET_strndup (dom_name, *dom_len);
	GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		    "Requested connection is to %s:%d\n",
		    s5r->domain,
		    ntohs (*port));
	s5r->state = SOCKS5_RESOLVING;
	s5r->port = ntohs (*port);
	s5r->gns_lookup = GNUNET_GNS_lookup (gns_handle,
					     s5r->domain,
					     &local_gns_zone,
					     GNUNET_DNSPARSER_TYPE_A,
					     GNUNET_NO /* only cached */,
					     (GNUNET_YES == do_shorten) ? &local_shorten_zone : NULL,
					     &handle_gns_result,
					     s5r);					     
	break;
      }
    default:
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Unsupported socks address type %d\n"),
		  (int) c_req->addr_type);
      signal_socks_failure (s5r,
			    SOCKS5_STATUS_ADDRESS_TYPE_NOT_SUPPORTED);
      return;
    }
    clear_from_s5r_rbuf (s5r,
			 sizeof (struct Socks5ClientRequestMessage) +
			 alen + sizeof (uint16_t));
    if (0 != s5r->rbuf_len)
    {
      /* read more bytes than healthy, why did the client send more!? */
      GNUNET_break_op (0);
      signal_socks_failure (s5r,
			    SOCKS5_STATUS_GENERAL_FAILURE);
      return;	    
    }
    if (SOCKS5_DATA_TRANSFER == s5r->state)
    {
      /* if we are not waiting for GNS resolution, signal success */
      signal_socks_success (s5r);
    }
    /* We are done reading right now */
    GNUNET_SCHEDULER_cancel (s5r->rtask);
    s5r->rtask = GNUNET_SCHEDULER_NO_TASK;    
    return;
  case SOCKS5_RESOLVING:
    GNUNET_assert (0);
    return;
  case SOCKS5_DATA_TRANSFER:
    GNUNET_assert (0);
    return;
  default:
    GNUNET_assert (0);
    return;
  }
}


/**
 * Accept new incoming connections
 *
 * @param cls the closure
 * @param tc the scheduler context
 */
static void
do_accept (void *cls, 
	   const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_NETWORK_Handle *s;
  struct Socks5Request *s5r;

  ltask = GNUNET_SCHEDULER_add_read_net (GNUNET_TIME_UNIT_FOREVER_REL,
                                         lsock,
                                         &do_accept, NULL);
  s = GNUNET_NETWORK_socket_accept (lsock, NULL, NULL);
  if (NULL == s)
  {
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR, "accept");
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Got an inbound connection, waiting for data\n");
  s5r = GNUNET_new (struct Socks5Request);
  GNUNET_CONTAINER_DLL_insert (s5r_head,
			       s5r_tail,
			       s5r);
  s5r->sock = s;
  s5r->state = SOCKS5_INIT;
  s5r->rtask = GNUNET_SCHEDULER_add_read_net (GNUNET_TIME_UNIT_FOREVER_REL,
                                              s5r->sock,
                                              &do_s5r_read, s5r);
}


/* ******************* General / main code ********************* */


/**
 * Task run on shutdown
 *
 * @param cls closure
 * @param tc task context
 */
static void
do_shutdown (void *cls,
             const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ProxyCurlTask *ctask;
  struct ProxyCurlTask *ctask_tmp;
  struct ProxyUploadData *pdata;
  
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Shutting down...\n");
  while (NULL != mhd_httpd_head)
    kill_httpd (mhd_httpd_head);
  for (ctask=ctasks_head; NULL != ctask; ctask=ctask_tmp)
  {
    ctask_tmp = ctask->next;
    if (NULL != ctask->curl)
    {
      curl_easy_cleanup (ctask->curl);
      ctask->curl = NULL;
    }
    if (NULL != ctask->headers)
    {
      curl_slist_free_all (ctask->headers);
      ctask->headers = NULL;
    }
    if (NULL != ctask->resolver)
    {
      curl_slist_free_all (ctask->resolver);
      ctask->resolver = NULL;
    }
    if (NULL != ctask->response)
    {
      MHD_destroy_response (ctask->response);
      ctask->response = NULL;
    }    
    for (pdata = ctask->upload_data_head; NULL != pdata; pdata = ctask->upload_data_head)
    {
      GNUNET_CONTAINER_DLL_remove (ctask->upload_data_head,
                                   ctask->upload_data_tail,
                                   pdata);
      GNUNET_free_non_null (pdata->filename);
      GNUNET_free_non_null (pdata->content_type);
      GNUNET_free_non_null (pdata->key);
      GNUNET_free_non_null (pdata->value);
      GNUNET_free (pdata);
    }
    GNUNET_free (ctask);
  }
  if (NULL != lsock)
  {
    GNUNET_NETWORK_socket_close (lsock);
    lsock = NULL;
  }
  if (NULL != id_op)
  {
    GNUNET_IDENTITY_cancel (id_op);
    id_op = NULL;
  }
  if (NULL != identity)
  {
    GNUNET_IDENTITY_disconnect (identity);
    identity = NULL;
  }
  if (NULL != curl_multi)
  {
    curl_multi_cleanup (curl_multi);
    curl_multi = NULL;
  }
  if (NULL != gns_handle)
  {
    GNUNET_GNS_disconnect (gns_handle);
    gns_handle = NULL;
  }
  if (GNUNET_SCHEDULER_NO_TASK != curl_download_task)
  {
    GNUNET_SCHEDULER_cancel (curl_download_task);
    curl_download_task = GNUNET_SCHEDULER_NO_TASK;
  }
  if (GNUNET_SCHEDULER_NO_TASK != ltask)
  {
    GNUNET_SCHEDULER_cancel (ltask);
    ltask = GNUNET_SCHEDULER_NO_TASK;
  }
  gnutls_global_deinit ();
}


/**
 * Continue initialization after we have our zone information.
 */
static void 
run_cont () 
{
  struct MhdHttpList *hd;
  struct sockaddr_in sa;

  /* Open listen socket for socks proxy */
  /* FIXME: support IPv6! */
  memset (&sa, 0, sizeof (sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons (port);
#if HAVE_SOCKADDR_IN_SIN_LEN
  sa.sin_len = sizeof (sa);
#endif
  lsock = GNUNET_NETWORK_socket_create (AF_INET,
					SOCK_STREAM,
					0);
  if (NULL == lsock) 
  {
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR, "socket");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  if (GNUNET_OK !=
      GNUNET_NETWORK_socket_bind (lsock, (const struct sockaddr *) &sa,
				  sizeof (sa), 0))
  {
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR, "bind");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  if (GNUNET_OK != GNUNET_NETWORK_socket_listen (lsock, 5))
  {
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR, "listen");
    return;
  }
  ltask = GNUNET_SCHEDULER_add_read_net (GNUNET_TIME_UNIT_FOREVER_REL,
                                         lsock, &do_accept, NULL);

  if (0 != curl_global_init (CURL_GLOBAL_WIN32))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "cURL global init failed!\n");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Proxy listens on port %u\n",
              port);

  /* start MHD daemon for HTTP */
  hd = GNUNET_new (struct MhdHttpList);
  hd->daemon = MHD_start_daemon (MHD_USE_DEBUG | MHD_USE_NO_LISTEN_SOCKET,
				 0,
				 NULL, NULL,
				 &create_response, hd,
				 MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 16,
				 MHD_OPTION_NOTIFY_COMPLETED, &mhd_completed_cb, NULL,
				 MHD_OPTION_URI_LOG_CALLBACK, &mhd_log_callback, NULL,
				 MHD_OPTION_END);
  if (NULL == hd->daemon)
  {
    GNUNET_free (hd);
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  httpd = hd;
  GNUNET_CONTAINER_DLL_insert (mhd_httpd_head, mhd_httpd_tail, hd);
}


/** 
 * Method called to inform about the egos of the shorten zone of this peer.
 *
 * When used with #GNUNET_IDENTITY_create or #GNUNET_IDENTITY_get,
 * this function is only called ONCE, and 'NULL' being passed in
 * @a ego does indicate an error (i.e. name is taken or no default
 * value is known).  If @a ego is non-NULL and if '*ctx'
 * is set in those callbacks, the value WILL be passed to a subsequent
 * call to the identity callback of #GNUNET_IDENTITY_connect (if 
 * that one was not NULL).
 *
 * @param cls closure, NULL
 * @param ego ego handle
 * @param ctx context for application to store data for this ego
 *                 (during the lifetime of this process, initially NULL)
 * @param name name assigned by the user for this ego,
 *                   NULL if the user just deleted the ego and it
 *                   must thus no longer be used
 */
static void
identity_shorten_cb (void *cls,
		     struct GNUNET_IDENTITY_Ego *ego,
		     void **ctx,
		     const char *name)
{
  id_op = NULL;
  if (NULL == ego)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
		_("No ego configured for `shorten-zone`\n"));
  }
  else
  {
    local_shorten_zone = *GNUNET_IDENTITY_ego_get_private_key (ego);
    do_shorten = GNUNET_YES;
  }
  run_cont ();
}


/** 
 * Method called to inform about the egos of the master zone of this peer.
 *
 * When used with #GNUNET_IDENTITY_create or #GNUNET_IDENTITY_get,
 * this function is only called ONCE, and 'NULL' being passed in
 * @a ego does indicate an error (i.e. name is taken or no default
 * value is known).  If @a ego is non-NULL and if '*ctx'
 * is set in those callbacks, the value WILL be passed to a subsequent
 * call to the identity callback of #GNUNET_IDENTITY_connect (if 
 * that one was not NULL).
 *
 * @param cls closure, NULL
 * @param ego ego handle
 * @param ctx context for application to store data for this ego
 *                 (during the lifetime of this process, initially NULL)
 * @param name name assigned by the user for this ego,
 *                   NULL if the user just deleted the ego and it
 *                   must thus no longer be used
 */
static void
identity_master_cb (void *cls,
		    struct GNUNET_IDENTITY_Ego *ego,
		    void **ctx,
		    const char *name)
{
  id_op = NULL;
  if (NULL == ego)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		_("No ego configured for `master-zone`\n"));
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  GNUNET_IDENTITY_ego_get_public_key (ego,
				      &local_gns_zone);
  id_op = GNUNET_IDENTITY_get (identity,
			       "shorten-zone",
			       &identity_shorten_cb,
			       NULL);
}


/**
 * Main function that will be run
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param c configuration
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *c)
{
  char* cafile_cfg = NULL;
  char* cafile;

  cfg = c;
  if (NULL == (curl_multi = curl_multi_init ()))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Failed to create cURL multi handle!\n");
    return;
  } 
  cafile = cafile_opt;
  if (NULL == cafile)
  {
    if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_filename (cfg, "gns-proxy",
							      "PROXY_CACERT",
							      &cafile_cfg))
    {
      GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
				 "gns-proxy",
				 "PROXY_CACERT");
      return;
    }
    cafile = cafile_cfg;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Using %s as CA\n", cafile);
  
  gnutls_global_init ();
  gnutls_x509_crt_init (&proxy_ca.cert);
  gnutls_x509_privkey_init (&proxy_ca.key);
  
  if ( (GNUNET_OK != load_cert_from_file (proxy_ca.cert, cafile)) ||
       (GNUNET_OK != load_key_from_file (proxy_ca.key, cafile)) )
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		_("Failed to load SSL/TLS key and certificate from `%s'\n"),
		cafile);
    // FIXME: release resources...
    GNUNET_free_non_null (cafile_cfg);  
    return;
  }
  GNUNET_free_non_null (cafile_cfg);
  if (NULL == (gns_handle = GNUNET_GNS_connect (cfg)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Unable to connect to GNS!\n");
    return;
  }
  identity = GNUNET_IDENTITY_connect (cfg,
				      NULL, NULL);  
  id_op = GNUNET_IDENTITY_get (identity,
			       "master-zone",
			       &identity_master_cb,
			       NULL);  
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL,
                                &do_shutdown, NULL);
}


/**
 * The main function for gnunet-gns-proxy.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static const struct GNUNET_GETOPT_CommandLineOption options[] = {
    {'p', "port", NULL,
     gettext_noop ("listen on specified port (default: 7777)"), 1,
     &GNUNET_GETOPT_set_ulong, &port},
    {'a', "authority", NULL,
      gettext_noop ("pem file to use as CA"), 1,
      &GNUNET_GETOPT_set_string, &cafile_opt},
    GNUNET_GETOPT_OPTION_END
  };
  static const char* page = 
    "<html><head><title>gnunet-gns-proxy</title>"
    "</head><body>cURL fail</body></html>";
  int ret;

  if (GNUNET_OK != GNUNET_STRINGS_get_utf8_args (argc, argv, &argc, &argv))
    return 2;
  GNUNET_log_setup ("gnunet-gns-proxy", "WARNING", NULL);
  curl_failure_response = MHD_create_response_from_buffer (strlen (page),
							   (void*)page,
							   MHD_RESPMEM_PERSISTENT);

  ret =
      (GNUNET_OK ==
       GNUNET_PROGRAM_run (argc, argv, "gnunet-gns-proxy",
                           _("GNUnet GNS proxy"),
                           options,
                           &run, NULL)) ? 0 : 1;
  MHD_destroy_response (curl_failure_response);
  GNUNET_free_non_null ((char *) argv);
  return ret;
}

/* end of gnunet-gns-proxy.c */
