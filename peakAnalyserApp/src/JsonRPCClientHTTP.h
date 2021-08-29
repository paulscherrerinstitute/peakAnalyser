#ifndef JSON_RPC_CLIENT_HTTP_H
#define JSON_RPC_CLIENT_HTTP_H

#include <string>
#include <atomic>
#include <thread>

#include <osiSock.h>    // EPICS os independent socket

#include "JsonRPCClientI.h"

// Forward declarations
typedef struct request  request_t;
typedef struct response response_t;
typedef struct socket   socket_t;
class NotificationServer;

class JsonRPCClientHTTP : public JsonRPCClientI
{
public:
    JsonRPCClientHTTP (const std::string& uri);
    virtual ~JsonRPCClientHTTP();

    std::string scheme() { return "http://"; }
    nlohmann::json call(const std::string& method, const nlohmann::json& params);
    void subscribe(const std::string& notification, JsonRPCClientI::Callback callback);
    void unsubscribe(const std::string& guid);
    std::string notificationServer();

protected:
    //! issue HTTP GET requests
    bool get (const std::string& path, std::string& value);
    //! issue HTTP POST requests
    bool post (const std::string& path, const std::string& content, std::string& value);
    //! issue an HTTP request and get server's response
    bool doRequest (const request_t& request, response_t& response);
    //! create the socket connection
    bool connect (socket_t *s);
    //! close the socket connection
    void close (socket_t *s);
    //! enable/disable socket nonblock option
    bool setNonBlock (socket_t *s, bool nonBlock);

private:
    static std::unique_ptr<NotificationServer> _notificationServer;

    std::string mHostname;
    int mPort;
    std::string mPath;
    struct sockaddr_in mServerAddress;
    std::string mLocalHost;
    size_t mNumSockets;
    socket_t *mSockets;
    std::atomic<uint32_t> mMsgId;
    int mTimeout;
};

#endif
