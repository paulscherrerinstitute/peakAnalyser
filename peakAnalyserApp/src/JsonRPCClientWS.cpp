#include "JsonRPCClientWS.h"

using json = nlohmann::json;

JsonRPCClientWS::JsonRPCClientWS(const std::string& uri)
        : JsonRPCClientI(uri), m_connected(false), m_msgId(1)
{
    m_callbackEventId = epicsEventCreate(epicsEventEmpty);

    // Set core access logging
    m_client.clear_access_channels(websocketpp::log::alevel::all);
    m_client.set_access_channels(websocketpp::log::alevel::access_core);

    // Initialize the Asio transport policy
    m_client.init_asio();

    m_client.set_open_handler(websocketpp::lib::bind(
        &JsonRPCClientWS::on_open,
        this,
        websocketpp::lib::placeholders::_1
    ));
    m_client.set_fail_handler(websocketpp::lib::bind(
        &JsonRPCClientWS::on_fail,
        this,
        websocketpp::lib::placeholders::_1
    ));
    m_client.set_close_handler(websocketpp::lib::bind(
        &JsonRPCClientWS::on_close,
        this,
        websocketpp::lib::placeholders::_1
    ));
    m_client.set_message_handler(websocketpp::lib::bind(
        &JsonRPCClientWS::on_message,
        this,
        websocketpp::lib::placeholders::_1,
        websocketpp::lib::placeholders::_2
    ));

    // Marks the endpoint as perpetual, stopping it from exiting when empty.
    m_client.start_perpetual();

    // Create a thread to run the ASIO io_service event loop
    m_thread = websocketpp::lib::make_shared<websocketpp::lib::thread>(&client::run, &m_client);
}

JsonRPCClientWS::~JsonRPCClientWS()
{
    close();

    m_client.stop_perpetual();

    m_thread->join();

    epicsEventDestroy(m_callbackEventId);
}

json JsonRPCClientWS :: call(const std::string& method, const json& params)
{
    if (!m_connected) {
        connect();
    }
 
    std::string content =  json::object({
        {"jsonrpc", "2.0"},
        {"id", std::to_string(m_msgId.fetch_add(1))},
        {"method", method},
        {"params", params}
    }).dump();

    m_response = {};
    websocketpp::lib::error_code ec;
    m_client.send(m_hdl, content, websocketpp::frame::opcode::text, ec);
    if (ec)
        throw JsonRPCException(
                JsonRPCException::NETWORK_EXCEPTION,
                "JSON-RPC WebSocket send failed: " + ec.message());

    epicsEventWait(m_callbackEventId);

    if(m_response.contains("error"))
        throw JsonRPCException(
                JsonRPCException::JSONRPC_ERROR,
                method + " error: " + m_response["error"].dump());


    if(!m_response.contains("result"))
        throw JsonRPCException(
                JsonRPCException::BAD_RESPONSE,
                method + " invalid response: " + m_response.dump());

    return m_response["result"];
}

void JsonRPCClientWS::subscribe(const std::string& notification, Callback callback)
{
    m_callbacks[notification] = callback;
}

void JsonRPCClientWS::unsubscribe(const std::string& notification)
{
    auto it = m_callbacks.find(notification);
    if (it != m_callbacks.end())
        m_callbacks.erase(it);
}

void JsonRPCClientWS::connect()
{
    // Create a new connection to the given URI
    websocketpp::lib::error_code ec;
    auto con = m_client.get_connection(m_uri, ec);
    if (ec) {
        std::cout << "> Connect initialization for '" << m_uri  << "' error: " << ec.message() << std::endl;
        return;
    }
    
    // Grab a handle for this connection so we can talk to it in a thread
    // safe manor after the event loop starts.
    m_hdl = con->get_handle();

    // Queue the connection. No DNS queries or network connections will be
    // made until the io_service event loop is run.
    m_client.connect(con);

    // Wait the connection
    epicsEventWait(m_callbackEventId);    
}

void JsonRPCClientWS :: close()
{

    websocketpp::lib::error_code ec;
    m_client.close(m_hdl, websocketpp::close::status::going_away, "", ec);
    if (ec) {
        std::cout << "> Error closing connection: " << ec.message() << std::endl;
        return;
    }
}


void JsonRPCClientWS :: on_open(websocketpp::connection_hdl)
{
    m_connected = true;
    epicsEventSignal(m_callbackEventId);
}

void JsonRPCClientWS :: on_fail(websocketpp::connection_hdl)
{
    m_connected = false;
    epicsEventSignal(m_callbackEventId);
}

void JsonRPCClientWS :: on_close(websocketpp::connection_hdl)
{
    m_connected = false;
    epicsEventSignal(m_callbackEventId);
}

void JsonRPCClientWS :: on_message(websocketpp::connection_hdl, client::message_ptr msg)
{
    if (msg->get_opcode() != websocketpp::frame::opcode::text)
        return;
    
    json message;
    try {
        message = json::parse(msg->get_payload());
    } catch (std::exception& e) {
        std::cerr << "Invalid json message: " << e.what() << std::endl;
        return;
    }

    if (message.contains("id")) {
        m_response = message; 
        epicsEventSignal(m_callbackEventId);
    } else {
        std::string method = message["method"].get<std::string>();
        const json params = message["params"];
        try {
            m_callbacks[method](params);
        } catch (std::exception& e) {
            std::cerr << "Error in callback " << method << ": "<< e.what() << std::endl;
        }
    }
}
