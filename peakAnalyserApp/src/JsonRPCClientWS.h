#ifndef JSON_RPC_CLIENT_WS_H
#define JSON_RPC_CLIENT_WS_H

#include <string>
#include <atomic>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <epicsEvent.h>

#include "JsonRPCClientI.h"

typedef websocketpp::client<websocketpp::config::asio_client> client;

class JsonRPCClientWS : public JsonRPCClientI
{
public:
    JsonRPCClientWS(const std::string& uri);
    virtual ~JsonRPCClientWS();

    std::string scheme() { return "ws://"; }
    nlohmann::json call(const std::string& method, const nlohmann::json& params);
    void subscribe(const std::string& notification, Callback callback);
    void unsubscribe(const std::string& notification);
 
protected:
    void connect();
    void close();

    void on_open(websocketpp::connection_hdl hdl);
    void on_fail(websocketpp::connection_hdl hdl);
    void on_close(websocketpp::connection_hdl hdl);
    void on_message(websocketpp::connection_hdl, client::message_ptr msg);

private:
    client m_client;
    websocketpp::connection_hdl m_hdl;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;

    bool m_connected;
    nlohmann::json m_response;
    epicsEventId m_callbackEventId;
    std::atomic<uint32_t> m_msgId;
    std::map<std::string, Callback> m_callbacks;
};

#endif // JSON_RPC_CLIENT_WS_H
