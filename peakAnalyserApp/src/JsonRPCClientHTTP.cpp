#include "JsonRPCClientHTTP.h"

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#ifdef _WIN32
 // Required for EPICS base 3.14.
 // In EPICS base >= 3.15, WIN32/osdSock.h includes this header.
#include <ws2tcpip.h>
#endif

#include <epicsThread.h>

using namespace nlohmann;

#define EOL                     "\r\n"      // End of Line

#define MAX_SOCKETS             5
#define MAX_HTTP_RETRIES        1
#define MAX_BUF_SIZE            256

#define DEFAULT_TIMEOUT_CONNECT 3
#define DEFAULT_TIMEOUT_REPLY 20

#define ERR_PREFIX  "JsonRPCClientHTTP"
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
 * Simple parser of url of form http://<host>[:<port>][/path]
 */
struct URL
{
    URL(const std::string& url);
    std::string host;
    int port;
    std::string path;
};

/*!
 * HTTP server to receive notifications from PEAK servers.
 */
class NotificationServer
{
public:
    NotificationServer(const std::string& host="127.0.0.1", int port=0);
    ~NotificationServer();


    void subscribe (const std::string& topic, JsonRPCClientI::Callback callback);
    void unsubscribe (const std::string& topic);

    void stop () {_stop = true;};

    std::string host() { return _host; }
    int port() { return _port; }

private:
    void createSocket();
    void connectionListener();
    void clientHandler(SOCKET client);

    std::unique_ptr<std::thread> _server;
    std::string _host;
    int _port;
    SOCKET _socket;
    std::map<std::string, JsonRPCClientI::Callback> _subscribers;
    bool _stop;
};

/*
 * Helper functions
 *
 */

/*! If header exists return its value,
 * otherwise return the default value.
 */
static std::string getHeaderValue(
        std::map<std::string, std::string>headers,
        std::string header,
        std::string defaultValue="")
{
    auto search = headers.find(header);
    if (search == headers.end())
        return defaultValue;
    return search->second;
}

/* Parse header text to a map */
static bool parseHeaderLine (std::string header, std::map<std::string, std::string>& headers)
{
    size_t n = header.find_first_of(':');
    if (n == std::string::npos)
        return false;
    std::string key = header.substr(0, n);

    // strip off leading/trailing white spaces
    size_t d1 = header.find_first_not_of(" \t\r\n", n+1);
    size_t d2 = header.find_last_not_of(" \t\r\n");
    std::string value = header.substr(d1, d2+1-d1);

    headers[key] = value;

    return true;
}

/* Read a single line from socket */
static bool readLine (SOCKET s, std::string &line)
{
    const char *functionName = "readLine";

    line.clear();
    while (true) {
        char b;
        int n = recv(s, &b, 1, 0);
        if (n < 0) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("recv error %d:%s\n", SOCKERRNO, error);
            return false;
        } else if (n == 0) {
            if (line.length() == 0)
                return false;
            else
                return true;
        } else {
            line.push_back(b);
            if (b == '\n')
                break;
        }
    }

    return true;
}

/*! Read HTTP response content until connection is closed */
static bool readIndefiniteContent(SOCKET s, std::string &content)
{
    char buf[4096];
    while (true) {
        int received = recv(s, buf, sizeof(buf), 0);
        if (received < 0) {
            return false;
        } else if (received == 0) {
            return true;
        } else {
            content.append(buf, received);
        }
    }
}

/*! Read HTTP response content with a specified length */
static bool readFixedContent(SOCKET s, std::string &content, size_t length)
{
    std::vector<char> buf(length);
    char *p = buf.data();
    size_t bufSize = length;
    while (bufSize) {
        int received = recv(s, p, (int)bufSize, 0);
        if (received <= 0) {
            return false;
        }
        p += received;
        bufSize -= received;
    }
    content.append(buf.data(), length);

    return true;
}

/*! Read HTTP response chunked content */
static bool readChunkedContent(SOCKET s, std::string &content)
{
    while (true) {
        // chunk size
        std::string lengthLine;
        if (!readLine(s, lengthLine))
            return false;
        size_t length = std::strtoul(lengthLine.c_str(), NULL, 16);

        // chunk data
        if (length) {
            std::string chunk;
            if (!readFixedContent(s, chunk, length))
                return false;
            content.append(chunk);
        }

        // end of chunk
        std::string line;
        if (!readLine(s, line) || line != "\r\n")
            return false;

        // end of content
        if (length == 0)
            return true;
    }
}

/* Send given data over socket */
static bool write(SOCKET s, const std::string& data)
{
    size_t remaining = data.size();
    const char *p = data.c_str();

    while (remaining) {
        int written = send(s, p, (int)remaining, 0);
        if (written < 0)
            return false;
        remaining -= written;
        p += written;
    }
    return true;
}

/*
 * URL
 *
 */

URL::URL(const std::string& url)
    : host("127.0.0.1"), port(80), path("/")
{
    const char scheme[] = "http://";

    if (url.find(scheme) != 0)
        return;
    size_t begin = strlen(scheme);
    size_t end = url.find('/', begin);
    std::string address;
    if (end == std::string::npos) {
        address = url.substr(begin);
    } else {
        address = url.substr(begin, end - begin);
        path = url.substr(end);
    }
    end = address.find(':');
    if (end == std::string::npos) {
        host = address;
    } else {
        host = address.substr(0, end);
        port = std::stoi(address.substr(end+1));
    }
}

/*
 * NotificationServer
 *
 */
NotificationServer::NotificationServer(const std::string& host, int port)
    : _host(host),_port(port), _stop(false)
{
    createSocket();
    if (_socket != INVALID_SOCKET)
        _server.reset(new std::thread(&NotificationServer::connectionListener, this));
}

NotificationServer::~NotificationServer()
{
    stop();
    _server->join();
}

void NotificationServer::subscribe(const std::string& topic, JsonRPCClientI::Callback callback)
{
    _subscribers[topic] = callback;
}

void NotificationServer::unsubscribe(const std::string& topic)
{
    auto it = _subscribers.find(topic);
    if (it != _subscribers.end())
        _subscribers.erase(it);
}

void NotificationServer::createSocket()
{
    const char *functionName = "createSocket";

    _socket = epicsSocketCreate(PF_INET, SOCK_STREAM, 0);
    if (_socket == INVALID_SOCKET) {
        char error[MAX_BUF_SIZE];
        epicsSocketConvertErrnoToString(error, sizeof(error));
        ERR_ARGS("Can't create socket: %d:%s", SOCKERRNO, error);
        return;
    }

    epicsSocketEnableAddressReuseDuringTimeWaitState(_socket);

    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(_port);
    if (_host == "*")
        serverAddr.sin_addr.s_addr = INADDR_ANY;
    else
        hostToIPAddr(_host.c_str(), &serverAddr.sin_addr);

    int status = bind(_socket, (struct sockaddr *) &serverAddr, sizeof (serverAddr));
    if (status < 0) {
        if (SOCKERRNO == SOCK_EADDRINUSE || SOCKERRNO == SOCK_EACCES) {
            /* choose random port if given port is not available */
            serverAddr.sin_port = htons(0);
            status = bind(_socket, (struct sockaddr *) &serverAddr, sizeof (serverAddr));
        }
        if (status < 0 ) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("Can't bind socket: %d:%s", SOCKERRNO, error);
            epicsSocketDestroy(_socket);
            _socket = INVALID_SOCKET;
           return;
        }
    }

    // retrieve port number
    osiSocklen_t addrSize = (osiSocklen_t) sizeof(serverAddr);
    getsockname(_socket, (struct sockaddr *) &serverAddr, &addrSize);
    _port = ntohs(serverAddr.sin_port);

    if (listen(_socket, 0) < 0) {
        char error[MAX_BUF_SIZE];
        epicsSocketConvertErrnoToString(error, sizeof(error));
        ERR_ARGS("Can't listen socket %d:%s\n", SOCKERRNO, error);
        epicsSocketDestroy(_socket);
        _socket = INVALID_SOCKET;
        return;
    }

    std::cout << "Notification server runs at " << _host << ":" << _port << std::endl;
}

void NotificationServer::connectionListener()
{
    const char* functionName = "connectionListener";

    while (!_stop) {
        struct sockaddr_in clientAddr;
        osiSocklen_t clientLen = sizeof (clientAddr);
        // accept with timeout
        fd_set rset, eset;
        struct timeval tv;
        int ret;

        FD_ZERO(&rset);FD_SET(_socket, &rset);
        FD_ZERO(&eset);FD_SET(_socket, &eset);
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        ret = select((int)_socket + 1, &rset, NULL, &eset, &tv);
        if (ret <= 0)
            continue;
        if (FD_ISSET(_socket, &eset)) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("Error calling select on %s:%d: %d:%s\n",
                 _host.c_str(), _port, SOCKERRNO, error);
            break;
        }
        SOCKET client = epicsSocketAccept((int)_socket, (struct sockaddr *) &clientAddr, &clientLen);
        if (SOCKERRNO == SOCK_EBADF || SOCKERRNO == SOCK_ENOTSOCK) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("Error calling accept on %s:%d: %d:%s\n",
                 _host.c_str(), _port, SOCKERRNO, error);
            break;
        }
        if (client == INVALID_SOCKET) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("Error calling accept on %s:%d: %d:%s\n",
                 _host.c_str(), _port, SOCKERRNO, error);
            continue;
        }
        std::thread(&NotificationServer::clientHandler, this, client).detach();
        continue;
    }
}

void NotificationServer::clientHandler(SOCKET client)
{
    const char* functionName = "clientHandler";

    while (!_stop) {
        {
            fd_set rset, eset;
            struct timeval tv;

            FD_ZERO(&rset);
            FD_SET(client, &rset);
            tv.tv_sec  = 1;
            tv.tv_usec = 0;

            int ret = select((int)client + 1, &rset, NULL, &eset, &tv);
            if (ret == 0)
                continue;
            if (ret < 0)
                break;
        }
        request_t request = {};

        // request line
        std::string requestLine;
        if (!readLine(client, requestLine)) {
            epicsSocketDestroy(client);
            return;
        }
        char method[10];
        sscanf(requestLine.c_str(), "%s", method);
        request.method = method;

        if (request.method !=  "POST") {
            ERR_ARGS("invalid request method %s", request.method.c_str());
            epicsSocketDestroy(client);
            return;
        }
        // headers
        while (true)
        {
            std::string line;
            if (!readLine(client, line)) {
                ERR("failed to read headers");
                epicsSocketDestroy(client);
                return;
            }

            // check EOL
            size_t s = line.length();
            if (s < 2 || line[s-2] != '\r' || line[s-1] != '\n')
                continue;
            // end of header
            if (s == 2)
                break;
            if (!parseHeaderLine(line, request.headers))
                continue;
        }

        // treat some specific headers
        request.contentLength = std::stoi(getHeaderValue(request.headers, "Content-Length", "0"));
        request.close = getHeaderValue(request.headers, "Connection", "") == "close";

        // read content
        if (request.contentLength)
            readFixedContent(client, request.content, request.contentLength);

        // parse content
        try {
            json notification = json::parse(request.content);
            std::string method = notification["method"].get<std::string>();
            auto params = notification["params"];

            _subscribers.at(method)(params);
        } catch (std::exception& e) {
            std::cerr << "Error in callback " << request.content << ": "<< e.what() << std::endl;
        } catch (...) {
            std::cerr << "Error in callback " << request.content << std::endl;
        }

        // response
        std::string response = "HTTP/1.1 204 OK" EOL "Content-Length: 0" EOL EOL;
        if (!write(client, response)) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("failed to send response: %d:%s", SOCKERRNO, error);
            epicsSocketDestroy(client);
            return;
        }

        // close connect if requested
        if (request.close)
            break;
    }
    epicsSocketDestroy(client);
}

/*
 * JsonRPCClientHTTP
 *
 */
std::unique_ptr<NotificationServer> JsonRPCClientHTTP::_notificationServer = nullptr;

JsonRPCClientHTTP::JsonRPCClientHTTP (const std::string& uri)
    : JsonRPCClientI(uri),
    mNumSockets(MAX_SOCKETS), mSockets(new socket_t[MAX_SOCKETS]),
    mMsgId(0), mTimeout(DEFAULT_TIMEOUT_REPLY)
{
    URL url(uri);
    mHostname = url.host;
    mPort = url.port;
    mPath = url.path;

    memset(&mServerAddress, 0, sizeof(mServerAddress));

    if(hostToIPAddr(mHostname.c_str(), &mServerAddress.sin_addr))
        throw JsonRPCException(
                JsonRPCException::NETWORK_EXCEPTION,
                "invalid hostname: " + mHostname);

    // convert hostname to dot decimals
    char serverHost[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &mServerAddress.sin_addr, serverHost, sizeof(serverHost));
    mHostname = serverHost;

    mServerAddress.sin_family = AF_INET;
    mServerAddress.sin_port = htons(mPort);

    for(size_t i = 0; i < mNumSockets; ++i)
    {
        mSockets[i].closed = true;
        mSockets[i].fd = INVALID_SOCKET;
    }
}

JsonRPCClientHTTP::~JsonRPCClientHTTP ()
{
    for(size_t i = 0; i < mNumSockets; ++i)
    {
        if (!mSockets[i].closed && mSockets[i].fd != INVALID_SOCKET) {
            epicsSocketDestroy(mSockets[i].fd);
        }
    }
    delete[] mSockets;
}

void JsonRPCClientHTTP::close (socket_t *s)
{
    if (s->fd != INVALID_SOCKET)
        epicsSocketDestroy(s->fd);
    s->closed = true;
}

bool JsonRPCClientHTTP::connect (socket_t *s)
{
    const char *functionName = "connect";

    if(!s->closed)
        return true;

    s->fd = epicsSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(s->fd == INVALID_SOCKET)
    {
        ERR("couldn't create socket");
        return false;
    }

    setNonBlock(s, true);

    if(::connect(s->fd, (struct sockaddr*)&mServerAddress, sizeof(mServerAddress)) < 0)
    {
        // Connection actually failed
#ifdef _WIN32
        if(SOCKERRNO != SOCK_EWOULDBLOCK)
#else
        if(SOCKERRNO != SOCK_EINPROGRESS)
#endif
        {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("failed to connect to %s:%d [%d:%s]", mHostname.c_str(),
                    mPort, SOCKERRNO, error);
            epicsSocketDestroy(s->fd);
            s->fd = INVALID_SOCKET;
            return false;
        }
        // Server didn't respond immediately, wait a little
        else
        {
            fd_set set;
            struct timeval tv;
            int ret;

            FD_ZERO(&set);
            FD_SET(s->fd, &set);
            tv.tv_sec  = DEFAULT_TIMEOUT_CONNECT;
            tv.tv_usec = 0;

            ret = select(int(s->fd) + 1, NULL, &set, NULL, &tv);
            if(ret <= 0)
            {
                const char *error = ret == 0 ? "TIMEOUT" : "select failed";
                ERR_ARGS("failed to connect to %s:%d [%d:%s]", mHostname.c_str(),
                        mPort, SOCKERRNO, error);
                epicsSocketDestroy(s->fd);
                s->fd = INVALID_SOCKET;
                return false;
            }
        }
    }

    // cache local host address
    if (mLocalHost.empty()) {
        struct sockaddr_in localAddress = {};
        char localHost[INET_ADDRSTRLEN];
        osiSocklen_t addrSize = (osiSocklen_t) sizeof(localAddress);
        getsockname(s->fd, (struct sockaddr *) &localAddress, &addrSize);
        inet_ntop(AF_INET, &localAddress.sin_addr, localHost, sizeof(localHost));
        mLocalHost = localHost;
    }
    if (!_notificationServer)
        _notificationServer.reset( new NotificationServer(mLocalHost, 52320) );

    setNonBlock(s, false);
    s->closed = false;
    return true;
}

bool JsonRPCClientHTTP::setNonBlock (socket_t *s, bool nonBlock)
{
#if defined(_WIN32)
    unsigned long int flags;
    flags = nonBlock;
    if (socket_ioctl(s->fd, FIONBIO, &flags) < 0)
        return false;
#else
    int flags = fcntl(s->fd, F_GETFL, 0);
    if(flags < 0)
        return false;

    flags = nonBlock ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    if (fcntl(s->fd, F_SETFL, flags) < 0)
        return false;
#endif
    return true;
}

bool JsonRPCClientHTTP::doRequest (const request_t& request, response_t& response)
{
    const char *functionName = "doRequest";

    socket_t *s = NULL;
    for (size_t i = 0; i < mNumSockets; ++i) {
        if(mSockets[i].mutex.try_lock()) {
            s = &mSockets[i];
            break;
        }
    }
    if (!s) {
        ERR("no available socket");
        return false;
    }
    std::lock_guard<std::mutex>(s->mutex, std::adopt_lock);

    std::string data;
    data += request.method + " " + request.path + " HTTP/1.1" + EOL;
    data += "Host: " + mHostname + EOL;
    for (const auto& el: request.headers) {
        data +=  el.first + ": " + el.second + EOL;
    }
    if (request.content.length()) {
        data += "Content-Length: " + std::to_string(request.content.length()) + EOL;
    }
    data += EOL;
    data += request.content;

    size_t retries = 0;
reconnect:
    if (!connect(s)) {
        ERR("failed to reconnect socket");
        return false;
    }

    if (!write(s->fd, data)) {
        this->close(s);
        if (retries++ < MAX_HTTP_RETRIES) {
            goto reconnect;
        }
        char error[MAX_BUF_SIZE];
        epicsSocketConvertErrnoToString(error, sizeof(error));
        ERR_ARGS("send failed %d:%s", SOCKERRNO, error);
        return false;
    }

    struct timeval recvTimeout;
    fd_set fds;
    if(mTimeout >= 0) {
        recvTimeout.tv_sec = mTimeout;
        recvTimeout.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(s->fd, &fds);
    }

    int ret = select(int(s->fd)+1, &fds, NULL, NULL, &recvTimeout);
    if (ret <= 0) {
        if (ret == 0)
            ERR_ARGS("timeout (%ds) in waiting for reply", mTimeout);
        else {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("select() failed ret=%d error=%d:%s", ret, SOCKERRNO, error);
        }
        this->close(s);
        return false;
    }

    // status line
    std::string status;
    if (!readLine(s->fd, status)) {
        this->close(s);
        if (retries++ < MAX_HTTP_RETRIES) {
            goto reconnect;
        }
        char error[MAX_BUF_SIZE];
        epicsSocketConvertErrnoToString(error, sizeof(error));
        ERR_ARGS("failed to read response line %s", error);
        return false;
    }
    sscanf(status.c_str(), "%s %d", response.version, &response.code);

    // headers
    while (true)
    {
        std::string line;
        if (!readLine(s->fd, line))
        {
            ERR("failed to read header line");
            this->close(s);
            return false;
        }
        // check EOL
        size_t len = line.length();
        if (len < 2 || line[len-2] != '\r' || line[len-1] != '\n')
            continue;
        // end of header
        if (len == 2)
            break;
        if (!parseHeaderLine(line, response.headers))
            continue;
    }
    // treat some specific headers
    response.contentLength = std::stoi(getHeaderValue(response.headers, "Content-Length", "0"));
    response.reconnect = getHeaderValue(response.headers, "Connection", "") == "close";
    response.chunked =  getHeaderValue(response.headers, "Transfer-Encoding", "") == "chunked";

    // content
    if (response.chunked) {
        if (!readChunkedContent(s->fd, response.content)) {
            this->close(s);
            return false;
        }
    } else if (response.headers.find("Content-Length") == response.headers.end()) {
        if (!readIndefiniteContent(s->fd, response.content)) {
            this->close(s);
            return false;
        }
    } else if (response.contentLength) {
        if (!readFixedContent(s->fd, response.content, response.contentLength)) {
            this->close(s);
            return false;
        }
    }

    // close socket if requested
    if(response.reconnect)
        this->close(s);

    return true;
}

bool JsonRPCClientHTTP::get (const std::string& path, std::string & value)
{
    const char *functionName = "get";

    request_t request = {};
    request.method = "GET";
    request.path = path;

    response_t response = {};

    if(!doRequest(request, response))
    {
        ERR_ARGS("[%s] request failed", path.c_str());
        return false;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[%s] server returned error code %d",
                path.c_str(), response.code);
        return false;
    }

    value = response.content;
    return true;
}

bool JsonRPCClientHTTP::post (const std::string& path, const std::string& content, std::string& value)
{
    const char *functionName = "post";

    request_t request = {};
    request.method = "POST";
    request.path = path;
    request.headers["Attachment-Mode"] = "Inline";
    request.headers["Content-Type"] = "application/json";
    request.content = content;

    response_t response = {};

    if(!doRequest(request, response))
    {
        ERR_ARGS("[%s] request failed", path.c_str());
        return false;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[%s] server returned error code %d",
                path.c_str(), response.code);
        return false;
    }

    value = response.content;
    return true;
}

json JsonRPCClientHTTP::call(const std::string& method, const json& params)
{
    std::string content =  json::object({
        {"jsonrpc", "2.0"},
        {"id", std::to_string(mMsgId.fetch_add(1))},
        {"method", method},
        {"params", params}
    }).dump();

    std::string value;
    if(!post(mPath, content, value))
        throw JsonRPCException(
                JsonRPCException::NETWORK_EXCEPTION,
                "JSON-RPC HTTP request failed: " + content);

    json reply;
    try {
        reply = json::parse(value);
    } catch(...) {
        throw JsonRPCException(
                JsonRPCException::BAD_RESPONSE,
                method + " invalid json: " + value);
    }

    if(reply.contains("error"))
        throw JsonRPCException(
                JsonRPCException::JSONRPC_ERROR,
                method + " error: " + reply["error"].dump());

    if(!reply.contains("result"))
        throw JsonRPCException(
                JsonRPCException::BAD_RESPONSE,
                method + " invalid response: " + value);

    return reply["result"];
}

void JsonRPCClientHTTP::subscribe(const std::string& notification, Callback callback)
{
    _notificationServer->subscribe(notification, callback);
}

void JsonRPCClientHTTP::unsubscribe(const std::string& notification)
{
    _notificationServer->unsubscribe(notification);
}

std::string JsonRPCClientHTTP::notificationServer()
{
    return "http://" +
        _notificationServer->host() + ":" +
        std::to_string(_notificationServer->port());
}
