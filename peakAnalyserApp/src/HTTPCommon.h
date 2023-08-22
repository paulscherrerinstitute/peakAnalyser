#ifndef HTTP_COMMON_H
#define HTTP_COMMON_H

#include <map>
#include <mutex>

#include <osiSock.h>

#define EOL                     "\r\n"      // End of Line

#define MAX_SOCKETS             5
#define MAX_HTTP_RETRIES        1
#define MAX_BUF_SIZE            256

#define DEFAULT_TIMEOUT_CONNECT 3
#define DEFAULT_TIMEOUT_REPLY 20

#define ERR_PREFIX  "HTTPClient"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
    functionName, __VA_ARGS__)

/*
 * Structure definitions
 *
 */

typedef struct socket
{
    SOCKET fd;
    std::mutex mutex;
    bool closed;
} socket_t;

typedef struct request
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string content;
    // for server
    size_t contentLength;
    bool close;
} request_t;

typedef struct response
{
    std::map<std::string, std::string> headers;
    bool reconnect;
    bool chunked;
    std::string content;
    size_t contentLength;
    int code;
    char version[40];
} response_t;

/*!
 * Simple parser of url of form <scheme>://<host>[:<port>][/path]
 */
struct URL
{
    URL(const std::string& url);
    std::string scheme;
    std::string host;
    int port;
    std::string path;
};

std::string getHeaderValue(
        std::map<std::string, std::string>headers,
        std::string header,
        std::string defaultValue="");
bool parseHeaderLine (std::string header, std::map<std::string, std::string>& headers);
bool readLine (SOCKET s, std::string &line);
bool write(SOCKET s, const std::string& data);
bool readIndefiniteContent(SOCKET s, std::string &content);
bool readFixedContent(SOCKET s, std::string &content, size_t length);
bool readChunkedContent(SOCKET s, std::string &content);


#endif // HTTP_COMMON_H