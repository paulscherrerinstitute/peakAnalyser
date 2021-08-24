#ifndef PEAK_API_H
#define PEAK_API_H

#include <string>
#include <atomic>
#include <thread>

#include <json.hpp>     // JSON parser
#include <osiSock.h>    // EPICS os independent socket


// Forward declarations
typedef struct request  request_t;
typedef struct response response_t;
typedef struct socket   socket_t;

using Callback = std::function<void(const nlohmann::json&)>;

void base64_decode(const std::string&, unsigned char *, size_t);

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
 * Spectrum data
 */
struct PeakSpectrum
{
    size_t rank;
    size_t dims[3];
    std::vector<double> xaxis;
    std::vector<double> yaxis;
    std::vector<double> zaxis;
    std::vector<float> data;

    void fromSpectrumChannel(const nlohmann::json& channel);
    void createAxis(const nlohmann::json& axis, std::vector<double>& axisData);
};

/*!
 * HTTP server to receive notifications from PEAK servers.
 */
class PeakNotificationServer
{
public:
    PeakNotificationServer(const std::string& host="127.0.0.1", int port=0);
    ~PeakNotificationServer();


    void subscribe (const std::string& topic, Callback callback);
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
    std::map<std::string, Callback> _subscribers;
    bool _stop;
};
/*!
 * Generic access API of PEAK servers via JSON-RPC over HTTP.
 */
class PeakAPI
{
public:
    enum AttachmentMode {
        None = 0,
        Inline,
        Http,
        File
    };

    PeakAPI (const std::string& hostname, int port, size_t numSockets=5);
    virtual ~PeakAPI();

    //! reply timeout in second
    void setTimeout(int timeout) {mTimeout = timeout;}
    int timeout() {return mTimeout;}

    //! attachment mode used in relavent methods
    void setAttachmentMode(AttachmentMode mode) {mAttachmentMode = mode;}
    AttachmentMode attachmentMode() {return mAttachmentMode;}

    //! server host name
    std::string host() {return mHostname;}

    //! server port number
    int port() {return mPort;}

    //! client host name, filled after the first connection
    std::string localHost();

    //! make a JSON-RPC call
    template<typename... Args>
    nlohmann::json call(const std::string& method, const Args... args);

    //! query a property of the PEAK server
    nlohmann::json query (const std::string& property, const nlohmann::json& filter = nlohmann::json::object());

    //! subscribe to a notification of the PEAK server
    std::string subscribe(const std::string& notification, Callback callback, AttachmentMode attachmentMode=Inline);
    //! unsubscribe previousely subscribed notification
    void unsubscribe(const std::string& guid);

    //! reset the PEAK server
    void reset();

    //! current state of the PEAK server, e.g. Idle, Active, Error ...
    std::string currentState();
    //! current configuration of the PEAK server
    std::string currentConfiguration();
    //! available configurations of the PEAK server
    std::vector<std::string> configurations();

    static std::unique_ptr<PeakNotificationServer> notificationServer;

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

    //! convert enum AttachmentMode to string
    std::string attachmentModeToString(AttachmentMode attachmentMode);

private:
    std::string mHostname;
    int mPort;
    struct sockaddr_in mServerAddress;
    std::string mLocalHost;
    size_t mNumSockets;
    socket_t *mSockets;
    std::atomic<uint32_t> mMsgId;
    int mTimeout;
    AttachmentMode mAttachmentMode;
};

/*! Client to access PEAK manager server
 */
class PeakManagerClient : public PeakAPI
{
public:
    PeakManagerClient (std::string const & hostname, int port=8080, size_t numSockets=5)
        : PeakAPI(hostname, port, numSockets) {}
    virtual ~PeakManagerClient () {}

    //! current running servers' names and addresses
    nlohmann::json runningServers();
};

/*! Client to access PEAK Analyser server
 */
class PeakAnalyserClient : public PeakAPI
{
public:
    PeakAnalyserClient (std::string const & hostname, int port, size_t numSockets=5)
        : PeakAPI(hostname, port, numSockets) {}
    virtual ~PeakAnalyserClient () {}

    //! current analyser settings
    nlohmann::json currentSettings();

    //! check if a measurement can be performed
    bool canMeasure();
    //! enter measurement state
    void startMeasurement();
    //! leave measurement state
    void finishMeasurement();

    //! create a spectrum with given settings
    std::string defineSpectrum (const nlohmann::json& spectrumDefinition);
    //! setup a spectrum for acquisition
    void setupSpectrum (const std::string& spectrumId);
    //! update a spectrum's settings
    void updateSpectrum (const std::string& spectrumId, const nlohmann::json& spectrumDefinition);
    //! release a spectrum's resources
    void finishSpectrum (const std::string& spectrumId);
    //! zero a spectrum's array data
    void clearSpectrum (const std::string& spectrumId);
    //! calculate spectrum axes range info
    nlohmann::json calculateSpectrumInfo(const nlohmann::json& spectrumDefinition);

    //! start an acquisition
    void start (const std::string& spectrumId);
    /*! stop the active acquisition
     *
     *  If the acquisition still has not completed,
     *  this will block, or timeout (depending the reply timeout setting)
     */
    void stop ();
    //! abort the active acquisition
    void abort ();
    //! start an acquisition and then wait for its finish
    void acquire (const std::string& spectrumId);

    //! get a spectrum's measured data, i.e. array data and axes range info
    PeakSpectrum getMeasuredSpectrum(const std::string& spectrumId);
};

/*! Client to access PEAK Electronics server
 */
class PeakElectronicsClient : public PeakAPI
{
public:
    PeakElectronicsClient (const std::string& hostname, int port, size_t numSockets=5)
        : PeakAPI(hostname, port, numSockets) {}
    virtual ~PeakElectronicsClient () {}

    //! zero power supplies
    void zeroAllSupplies();
};


/*! Client to access PEAK Camera server
 */
class PeakCameraClient : public PeakAPI
{
public:
    PeakCameraClient (const std::string& hostname, int port, size_t numSockets=5)
        : PeakAPI(hostname, port, numSockets) {}
    virtual ~PeakCameraClient () {}

    //! detector calibrated geometry
    nlohmann::json geometry();
};

#endif
