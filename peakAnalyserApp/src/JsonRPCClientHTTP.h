#ifndef JSON_RPC_CLIENT_HTTP_H
#define JSON_RPC_CLIENT_HTTP_H

#include <string>
#include <atomic>

#include "JsonRPCClientI.h"
#include "HTTPClient.h"

class NotificationServer;

class JsonRPCClientHTTP : public JsonRPCClientI, public HTTPClient
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
    virtual void connectionCallback();

private:
    static std::unique_ptr<NotificationServer> _notificationServer;

    std::atomic<uint32_t> mMsgId;
};

#endif
