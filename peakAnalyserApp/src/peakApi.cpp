#include "peakApi.h"

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

#define MAX_HTTP_RETRIES        1
#define MAX_BUF_SIZE            256

#define DEFAULT_TIMEOUT_CONNECT 3
#define DEFAULT_TIMEOUT_REPLY 20

#define ERR_PREFIX  "PeakApi"
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
 * PeakSpectrum
 *
 */
void PeakSpectrum::fromSpectrumChannel(const nlohmann::json& channel)
{
    rank = 0;
    const nlohmann::json& axes = channel["SpectrumChannelSettings"]["RequestedAxes"];
    if (axes.contains("X")) { createAxis(axes["X"], xaxis); dims[rank++] = xaxis.size(); }
    if (axes.contains("Y")) { createAxis(axes["Y"], yaxis); dims[rank++] = yaxis.size(); }
    if (axes.contains("Z")) { createAxis(axes["Z"], zaxis); dims[rank++] = zaxis.size(); }

    const nlohmann::json& attachment = channel["CountByteData"];
    if (attachment["HasValue"]) {
        size_t byteSize = attachment["Length"].get<size_t>();
        data.resize(byteSize / 4); // single precision float
        if (attachment["Mode"] == "Inline") {
            base64_decode(
                attachment["InlineValue"].get<std::string>(),
                (unsigned char *)data.data(),
                byteSize);
        } else if (attachment["Mode"] == "File") {
            std::string fname = attachment["Address"].get<std::string>();
            std::ifstream ifs(fname, std::ios::in | std::ios::binary);
            if (ifs.is_open()) {
                ifs.read((char *)data.data(), byteSize);
                ifs.close();
                remove(fname.c_str());
            } else {
                std::cerr << "Error in reading file " << fname << std::endl;
            }
        }
    }
}

void PeakSpectrum::createAxis(const nlohmann::json& axis, std::vector<double>& axisData)
{
    int count = axis["Count"].get<int>();
    double offset = axis["Offset"].get<double>();
    double delta = axis["Delta"].get<double>();
    double center = axis["Center"].get<double>();

    axisData.resize(count);
    if (count == 1)
        axisData[0] = center;
    else {
        for (size_t i=0; i<axisData.size(); i++) {
            axisData[i] = offset + delta * i;
        }
    }
}

/*
 * PeakNotificationServer
 *
 */
PeakNotificationServer::PeakNotificationServer(const std::string& host, int port)
    : _host(host),_port(port), _stop(false)
{
    createSocket();
    if (_socket != INVALID_SOCKET)
        _server.reset(new std::thread(&PeakNotificationServer::connectionListener, this));
}

PeakNotificationServer::~PeakNotificationServer()
{
    stop();
    _server->join();
}

void PeakNotificationServer::subscribe(const std::string& topic, Callback callback)
{
    _subscribers[topic] = callback;
}

void PeakNotificationServer::unsubscribe(const std::string& topic)
{
    _subscribers.erase(topic);
}

void PeakNotificationServer::createSocket()
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

void PeakNotificationServer::connectionListener()
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
        std::thread(&PeakNotificationServer::clientHandler, this, client).detach();
        continue;
    }
}

void PeakNotificationServer::clientHandler(SOCKET client)
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
            std::cerr << "Error in callback " << method << ": "<< e.what() << std::endl;
        } catch (...) {
            std::cerr << "Error in callback " << method << std::endl;
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
 * PeakAPI
 *
 */
std::unique_ptr<PeakNotificationServer> PeakAPI::notificationServer = nullptr;

PeakAPI::PeakAPI (const std::string& hostname, int port, size_t numSockets) :
    mHostname(hostname), mPort(port), mNumSockets(numSockets),
    mSockets(new socket_t[numSockets]), mMsgId(0), mTimeout(DEFAULT_TIMEOUT_REPLY),
    mAttachmentMode(Inline)
{
    memset(&mServerAddress, 0, sizeof(mServerAddress));

    if(hostToIPAddr(mHostname.c_str(), &mServerAddress.sin_addr))
        throw std::runtime_error("invalid hostname: " + mHostname);

    // convert hostname to dot decimals
    char serverHost[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &mServerAddress.sin_addr, serverHost, sizeof(serverHost));
    mHostname = serverHost;

    mServerAddress.sin_family = AF_INET;
    mServerAddress.sin_port = htons(port);

    for(size_t i = 0; i < mNumSockets; ++i)
    {
        mSockets[i].closed = true;
        mSockets[i].fd = INVALID_SOCKET;
    }
}

PeakAPI::~PeakAPI ()
{
    for(size_t i = 0; i < mNumSockets; ++i)
    {
        if (!mSockets[i].closed && mSockets[i].fd != INVALID_SOCKET) {
            epicsSocketDestroy(mSockets[i].fd);
        }
    }
    delete[] mSockets;
}

std::string PeakAPI::localHost()
{
    return mLocalHost;
}

void PeakAPI::close (socket_t *s)
{
    if (s->fd != INVALID_SOCKET)
        epicsSocketDestroy(s->fd);
    s->closed = true;
}

bool PeakAPI::connect (socket_t *s)
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

    setNonBlock(s, false);
    s->closed = false;
    return true;
}

bool PeakAPI::setNonBlock (socket_t *s, bool nonBlock)
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

std::string PeakAPI::attachmentModeToString(AttachmentMode attachmentMode)
{
    switch (attachmentMode) {
        case Inline:
            return "Inline";
        case Http:
            return "Http";
        case File:
            return "File";
        default:
            return "None";
    }
}

bool PeakAPI::doRequest (const request_t& request, response_t& response)
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
    } else if (response.contentLength) {
        if (!readFixedContent(s->fd, response.content, response.contentLength)) {
            this->close(s);
            return false;
        }
    } else {
        if (!readIndefiniteContent(s->fd, response.content)) {
            this->close(s);
            return false;
        }
    }

    // close socket if requested
    if(response.reconnect)
        this->close(s);

    return true;
}

bool PeakAPI::get (const std::string& path, std::string & value)
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

bool PeakAPI::post (const std::string& path, const std::string& content, std::string& value)
{
    const char *functionName = "post";

    request_t request = {};
    request.method = "POST";
    request.path = path;
    request.headers["Attachment-Mode"] = attachmentModeToString(mAttachmentMode);
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

template<typename... Args>
json PeakAPI::call(const std::string& method, const Args... args)
{
    json params = json::array({args...});

    std::string content =  json::object({
        {"jsonrpc", "2.0"},
        {"id", std::to_string(mMsgId.fetch_add(1))},
        {"method", method},
        {"params", params}
    }).dump();

    std::string value;
    if(!post("/api", content, value))
        throw std::runtime_error("rpc request failed: " + content);

    auto reply = json::parse(value);
    if(reply.contains("error"))
        throw std::runtime_error(method + " rpc error: " + reply["error"].dump());

    if(!reply.contains("result"))
        throw std::runtime_error(method + " rpc invalid response: " + value);

    return reply["result"];
}

json PeakAPI::query (const std::string& property, const json& filter)
{
    json param = {
        {property, filter}
    };
    return call("Query", param)[property];
}

std::string PeakAPI::subscribe(const std::string& notification, Callback callback, AttachmentMode attachmentMode)
{
    if (!notificationServer)
        throw std::runtime_error("No NotificationServer assigned");

    json attachmentSetting = {
        { "AttachmentMode", attachmentModeToString(attachmentMode) },
        { "AttachmentCompressionMode", "None"} // Options: None, Deflate, GZip, Brotli
    };

    json result = call("Subscribe",
                       notification,
                       notification,
                       attachmentSetting,
                       "http://" + notificationServer->host() + ":" + std::to_string(notificationServer->port()));

    notificationServer->subscribe(notification, callback);
    return result.get<std::string>();
}

void PeakAPI::unsubscribe(const std::string& guid)
{
    json result = call("Unsubscribe", guid);
}

void PeakAPI::reset()
{
    epicsEventId completedEventId = epicsEventCreate(epicsEventEmpty);

    std::string guid = subscribe("ResetCompleted",
            [&](const json&) {
                epicsEventSignal(completedEventId);
            }
    );

    call("BeginReset");

    epicsEventWait(completedEventId);

    unsubscribe(guid);

    call("EndReset");

    epicsEventDestroy(completedEventId);
}

std::string PeakAPI::currentState()
{
    return call("GetState").get<std::string>();
}

std::string PeakAPI::currentConfiguration()
{
    json filter = {
        {"Name", json::object()}
    };

    return query("Configuration", filter)["Name"].get<std::string>();
}

std::vector<std::string> PeakAPI::configurations()
{
    json filter = {
        { "{}", {
            {"Name", json::object()}
        }}
    };

    json value = query("AvailableConfigurations", filter);

    std::vector<std::string> configs;
    for (auto& it: value)
        configs.push_back(it["Name"].get<std::string>());

    return configs;
}

/*
 * PeakManagerClient
 *
 */
json PeakManagerClient::runningServers()
{
    // only retrieve server's name and address
    json filter = {
       { "{}", {
           {"Name", json::object()},
           {"Address", json::object()}
       }}
    };

    json value = query ("Servers", filter);

    json servers = json::array();
    for (auto& it: value)
        servers.push_back(it);

    return servers;
}

/*
 * PeakAnalyserClient
 *
 */
json PeakAnalyserClient::currentSettings ()
{
    return query("AnalyserSettings");
}

json PeakAnalyserClient::calculateSpectrumInfo (const nlohmann::json& spectrumDefinition)
{
    return call("CalculateSpectrumInfo", spectrumDefinition);
}

std::string PeakAnalyserClient::defineSpectrum (const nlohmann::json& spectrumDefinition)
{
    return call("DefineSpectrum", spectrumDefinition).get<std::string>();
}

void PeakAnalyserClient::setupSpectrum (const std::string& spectrumId)
{
    call("SetupSpectrum", spectrumId);
}

void PeakAnalyserClient::updateSpectrum (const std::string& spectrumId, const nlohmann::json& spectrumDefinition)
{
    call("UpdateSpectrum", spectrumId, spectrumDefinition);
}

void PeakAnalyserClient::clearSpectrum (const std::string& spectrumId)
{
    call("ClearSpectrum", spectrumId);
}

void PeakAnalyserClient::finishSpectrum (const std::string& spectrumId)
{
    call("FinishSpectrum", spectrumId);
}

bool PeakAnalyserClient::canMeasure ()
{
    return query("CanMeasure").get<bool>();
}

void PeakAnalyserClient::startMeasurement ()
{
    call("StartMeasurement", "Sequence", "Acquisition");
}

void PeakAnalyserClient::finishMeasurement ()
{
    call("FinishMeasurement");
}

void PeakAnalyserClient::start (const std::string& spectrumId)
{
    call("BeginAcquisition", spectrumId);
}

void PeakAnalyserClient::stop ()
{
    call("EndAcquisition");
}

void PeakAnalyserClient::abort ()
{
    call("CancelAcquisition");
}

void PeakAnalyserClient::acquire (const std::string& spectrumId)
{
    epicsEventId completedEventId = epicsEventCreate(epicsEventEmpty);

    std::string guid = subscribe("AcquisitionCompleted",
            [&](const json& params) {
                std::string notifiedSpectrumId = params[0].get<std::string>();
                if (notifiedSpectrumId == spectrumId) {
                    epicsEventSignal(completedEventId);
                }
            }
    );

    start(spectrumId);

    epicsEventWait(completedEventId);

    unsubscribe(guid);

    stop();

    epicsEventDestroy(completedEventId);
}

PeakSpectrum PeakAnalyserClient::getMeasuredSpectrum (const std::string& spectrumId)
{
   auto spectrumData = PeakSpectrum();
   const json& spectrum = call("GetMeasuredSpectrum", spectrumId);
   for (auto& chan: spectrum["SpectrumChannels"]) {
        spectrumData.fromSpectrumChannel(chan);
        break;
   }
   return spectrumData;
}

/*
 * PeakCameraClient
 *
 */
json PeakCameraClient::geometry()
{
    json filter = {
        {"CameraCalibration", json::object()},
    };

    return query("Configuration", filter)["CameraCalibration"]["SpatialCalibration"];
}

/*
 * PeakElectronicsClient
 *
 */
void PeakElectronicsClient::zeroAllSupplies()
{
    call("ZeroAllSupplies");
}
