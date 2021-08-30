#ifndef JSON_RPC_CLIENT_I_H
#define JSON_RPC_CLIENT_I_H

#include <string>

#include "json.hpp"

class JsonRPCException : public std::runtime_error
{
public:
    enum ErrorCode {
        BAD_RESPONSE = 0,
        JSONRPC_ERROR,
        NETWORK_EXCEPTION,
    };

    JsonRPCException(ErrorCode code, const std::string& message):
        std::runtime_error(message),m_code(code) {};

    ErrorCode code() {return m_code;}

protected:
    ErrorCode m_code;
};

class JsonRPCClientI
{
public:
    using Callback = std::function<void(const nlohmann::json&)>;

    JsonRPCClientI(const std::string& uri) { m_uri = uri; };
    virtual ~JsonRPCClientI () {};

    virtual std::string scheme() = 0;
    virtual nlohmann::json call(const std::string& method, const nlohmann::json& params) = 0;
    virtual void subscribe(const std::string& notification, Callback callback)  = 0;
    virtual void unsubscribe(const std::string& notification) = 0;
    virtual std::string notificationServer() { return ""; };

protected:
    std::string m_uri;
};

#endif // JSON_RPC_CLIENT_I_H
