
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <iostream>

#ifdef _WIN32
 // Required for EPICS base 3.14.
 // In EPICS base >= 3.15, WIN32/osdSock.h includes this header.
#include <ws2tcpip.h>
#endif

#include "HTTPCommon.h"

#include "HTTPClient.h"

HTTPClient::HTTPClient(const std::string& uri)
    : mNumSockets(MAX_SOCKETS), mSockets(new socket_t[MAX_SOCKETS]),
    mTimeout(DEFAULT_TIMEOUT_REPLY)
{
    URL url(uri);
    mHostname = url.host;
    mPort = url.port;
    mPath = url.path;

    memset(&mServerAddress, 0, sizeof(mServerAddress));

    if(hostToIPAddr(mHostname.c_str(), &mServerAddress.sin_addr)) {
        std::cerr <<  "invalid hostname: "  << mHostname << std::endl;
        return;
    }
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

HTTPClient::~HTTPClient()
{
    for(size_t i = 0; i < mNumSockets; ++i)
    {
        if (!mSockets[i].closed && mSockets[i].fd != INVALID_SOCKET) {
            epicsSocketDestroy(mSockets[i].fd);
        }
    }
    delete[] mSockets;
}

void HTTPClient::close (socket_t *s)
{
    if (s->fd != INVALID_SOCKET)
        epicsSocketDestroy(s->fd);
    s->closed = true;
}

bool HTTPClient::connect (socket_t *s)
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

    connectionCallback();

    setNonBlock(s, false);
    s->closed = false;
    return true;
}

bool HTTPClient::setNonBlock (socket_t *s, bool nonBlock)
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

bool HTTPClient::doRequest (const request_t& request, response_t& response)
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

bool HTTPClient::get (const std::string& path, std::map<std::string, std::string>headers, std::string & value)
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

bool HTTPClient::post (const std::string& path, std::map<std::string, std::string>headers, const std::string& content, std::string& value)
{
    const char *functionName = "post";

    request_t request = {};
    request.method = "POST";
    request.path = path;
    request.headers = headers;
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
