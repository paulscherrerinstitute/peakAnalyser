#include "JsonRPCClientHTTP.h"

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>

#include <epicsThread.h>

#include "HTTPCommon.h"

using namespace nlohmann;

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
    : JsonRPCClientI(uri), HTTPClient(uri), mMsgId(0)
{
}

JsonRPCClientHTTP::~JsonRPCClientHTTP ()
{
}

void JsonRPCClientHTTP::connectionCallback()
{
    if (!_notificationServer) {
        _notificationServer.reset( new NotificationServer(mLocalHost, 52320) );
    }
}

json JsonRPCClientHTTP::call(const std::string& method, const json& params)
{
    std::map<std::string, std::string> headers =  {
        {"Attachment-Mode", "Http"},
        {"Content-Type", "application/json"}
    };

    std::string content =  json::object({
        {"jsonrpc", "2.0"},
        {"id", std::to_string(mMsgId.fetch_add(1))},
        {"method", method},
        {"params", params}
    }).dump();

    std::string value;
    if(!post(mPath, headers, content, value))
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
