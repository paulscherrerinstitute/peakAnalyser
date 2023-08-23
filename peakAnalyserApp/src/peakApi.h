#ifndef PEAK_API_H
#define PEAK_API_H

#include "JsonRPCClientI.h"
#include "json.hpp"

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
    std::vector<double> integrate();
};

/*!
 * Generic access API of PEAK servers via JSON-RPC.
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

    PeakAPI (const std::string& uri);
    virtual ~PeakAPI() {};

    //! server uri
    std::string uri() {return m_uri;}

    //! attachment mode used in relavent methods
    void setAttachmentMode(AttachmentMode mode) {mAttachmentMode = mode;}
    AttachmentMode attachmentMode() {return mAttachmentMode;}

    //! make a JSON-RPC call
    template<typename... Args>
    nlohmann::json call(const std::string& method, const Args... args) {
        nlohmann::json params = nlohmann::json::array({args...});
        return m_rpcClient->call(method, params);
    }

    //! query a property of the PEAK server
    nlohmann::json query (const std::string& property, const nlohmann::json& filter = nlohmann::json::object());

    //! subscribe to a notification of the PEAK server
    std::string subscribe(const std::string& notification, JsonRPCClientI::Callback callback, AttachmentMode attachmentMode=Inline);
    //! subscribe to state changes of the PEAK server
    std::string subscribeToState(JsonRPCClientI::Callback callback);
    //! unsubscribe previousely subscribed notification
    void unsubscribe(const std::string& guid);
    //! unsubscribe all subscribed notifications
    void unsubscribeAll();

    //! reset the PEAK server
    void reset();

    //! current state of the PEAK server, e.g. Idle, Active, Error ...
    std::string currentState();
    //! current configuration of the PEAK server
    std::string currentConfiguration();
    //! available configurations of the PEAK server
    std::vector<std::string> configurations();

protected:
    //! convert enum AttachmentMode to string
    std::string attachmentModeToString(AttachmentMode attachmentMode);

    std::string m_uri;
    AttachmentMode mAttachmentMode;
    std::unique_ptr<JsonRPCClientI> m_rpcClient;
    std::map<std::string, std::string> m_subscribers;
};

/*! Client to access PEAK manager server
 */
class PeakManagerClient : public PeakAPI
{
public:
    PeakManagerClient (const std::string& uri)
        : PeakAPI(uri) {}
    virtual ~PeakManagerClient () {}

    //! current running servers' names and addresses
    nlohmann::json runningServers();
};

/*! Client to access PEAK Analyser server
 */
class PeakAnalyserClient : public PeakAPI
{
public:
    PeakAnalyserClient (const std::string& uri)
        : PeakAPI(uri) {}
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
    //! validate spectrum axes range info
    nlohmann::json adjustSpectrumDefinition(const nlohmann::json& spectrumDefinition);
    //! calculate spectrum axes range info
    nlohmann::json calculateSpectrumInfo(const nlohmann::json& spectrumDefinition);

    //! start an acquisition
    void start (const std::string& spectrumId);
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
    PeakElectronicsClient (const std::string& uri)
        : PeakAPI(uri) {}
    virtual ~PeakElectronicsClient () {}

    //! zero power supplies
    void zeroAllSupplies();
};


/*! Client to access PEAK Camera server
 */
class PeakCameraClient : public PeakAPI
{
public:
    PeakCameraClient (const std::string& uri)
        : PeakAPI(uri) {}
    virtual ~PeakCameraClient () {}

    //! detector calibrated geometry
    nlohmann::json geometry();
};

#endif
