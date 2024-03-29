#include "peakApi.h"

#include "JsonRPCClientHTTP.h"
#include "JsonRPCClientWS.h"

#include <fstream>
#include <iostream>

#include <epicsEvent.h>

using json = nlohmann::json;

void base64_decode(const std::string&, unsigned char *, size_t);

/*
 * PeakSpectrum
 *
 */
void PeakSpectrum::fromSpectrumChannel(const nlohmann::json& channel, std::string serverUri)
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
        } else if (attachment["Mode"] == "Http") {
            std::string uri = attachment["Address"].get<std::string>();
            size_t pos = uri.find("/attachments");
            if (pos != std::string::npos) {
                std::string path = uri.substr(pos);
                /* The spectrum URL is normally of form "http://127.0.0.1:59806/attachments/86ed5f36-34da-4d52-b5c1-442e65bb5e19.bin"
                   When connecting via manager server's reverse proxy, the host and port will be replaced with
                   manager server's host and port.*/
                if (serverUri.empty())
                    serverUri = uri.substr(0, pos);
                /* Get the data via HTTP request */
                std::string value;
                if (HTTPClient(serverUri).get(path, {}, value))
                    memcpy( (void*) data.data(), value.data(), value.size());
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

std::vector<double> PeakSpectrum::integrate()
{
    size_t width = dims[0];
    size_t height = dims[1];
    std::vector<double> spectrum(width);

    /* Address of the image data, in case of 3D dataset, the last frame */
    size_t frame = (rank==3 ? dims[2]-1 : 0);
    float *image = data.data() + width*height*frame;

    for (size_t j=0; j<height; j++)
        for (size_t i=0; i<width; i++)
            spectrum[i] += image[j*width + i];

    return spectrum;
}

/*
 * PeakAPI
 *
 */
PeakAPI::PeakAPI(const std::string& uri)
{
    m_uri = uri;

    // Remove trailing '/'
    std::size_t pos = m_uri.find_last_not_of('/');
    if (pos != std::string::npos)
        m_uri.erase(pos+1);

    // Append "/api" path
    if (m_uri.rfind("/api") != m_uri.size() - 4)
        m_uri += "/api";

    if (m_uri.find("http://") == 0) {
        m_rpcClient.reset(new JsonRPCClientHTTP(m_uri));
    } else {
        m_rpcClient.reset(new JsonRPCClientWS(m_uri));
    }
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

json PeakAPI::query (const std::string& property, const json& filter)
{
    json param = {
        {property, filter}
    };
    return call("Query", param)[property];
}

std::string PeakAPI::subscribeToObservable(const std::string& observable, JsonRPCClientI::Callback callback, AttachmentMode attachmentMode)
{
    static size_t id = 0;

    json attachmentSetting = {
        { "AttachmentMode", attachmentModeToString(attachmentMode) },
        { "AttachmentCompressionMode", "None"} // Options: None, Deflate, GZip, Brotli
    };

    std::string method = observable + std::to_string(id++);
    std::string guid;
    if (m_rpcClient->scheme() == "http://")
        guid = call("SubscribeToObservable",
                    observable,
                    method,
                    attachmentSetting,
                    m_rpcClient->notificationServer()).get<std::string>();
    else
        guid = call("SubscribeToObservable",
                    observable,
                    method,
                    attachmentSetting).get<std::string>();

    m_rpcClient->subscribe(method, callback);

    // append to the subscribers list
    m_subscribers.emplace(guid, method);

    return guid;
}

std::string PeakAPI::subscribeToState(JsonRPCClientI::Callback callback)
{
    static size_t id = 0;

    std::string method = "State" + std::to_string(id++);
    std::string guid;
    if (m_rpcClient->scheme() == "http://")
        call("SubscribeToState", method, m_rpcClient->notificationServer())["Id"].get_to(guid);
    else
        call("SubscribeToState", method)["Id"].get_to(guid);

    m_rpcClient->subscribe(method, callback);

    // append to the subscribers list
    m_subscribers.emplace(guid, method);

    return guid;
}

void PeakAPI::unsubscribe(const std::string& guid)
{
    for(auto it = m_subscribers.begin(); it != m_subscribers.end(); it++) {
        if (it->first == guid) {
            call("Unsubscribe", guid);
            m_rpcClient->unsubscribe(it->second);
            m_subscribers.erase(it);
            break;
        }
    }
}

void PeakAPI::unsubscribeAll()
{
    for(auto it = m_subscribers.begin(); it != m_subscribers.end(); it++) {
        call("Unsubscribe", it->first);
        m_rpcClient->unsubscribe(it->second);
        m_subscribers.erase(it);
    }

    if (m_rpcClient->scheme() == "http://")
        call("UnsubscribeAllForObserver", m_rpcClient->notificationServer());
}

void PeakAPI::reset()
{
    epicsEventId completedEventId = epicsEventCreate(epicsEventEmpty);

    std::string guid = subscribeToState(
        [&](const json&) {
            epicsEventSignal(completedEventId);
        }
    );

    call("ResetAsync");

    epicsEventWait(completedEventId);

    unsubscribe(guid);

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
    for (auto& it: value) {
        std::string address = it["Address"].get<std::string>();
        address.replace(0, 7, m_rpcClient->scheme());
        it["Address"] = address;
        servers.push_back(it);
    }

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

json PeakAnalyserClient::adjustSpectrumDefinition (const nlohmann::json& spectrumDefinition)
{
    return call("AdjustSpectrumDefinition", spectrumDefinition);
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
    call("StartMeasurement");
}

void PeakAnalyserClient::finishMeasurement ()
{
    call("FinishMeasurement");
}

void PeakAnalyserClient::start (const std::string& spectrumId)
{
    call("AcquisitionAsync", spectrumId);
}

void PeakAnalyserClient::abort ()
{
    call("CancelAcquisition");
}

void PeakAnalyserClient::acquire (const std::string& spectrumId)
{
    epicsEventId completedEventId = epicsEventCreate(epicsEventEmpty);

    std::string guid = subscribeToState(
            [&](const json& params) {
                std::string state = params["NewState"].get<std::string>();
                if (state == "Measuring" ||
                    state == "Error")
                    epicsEventSignal(completedEventId);
            }
    );

    start(spectrumId);

    epicsEventWait(completedEventId);

    unsubscribe(guid);

    epicsEventDestroy(completedEventId);
}

PeakSpectrum PeakAnalyserClient::getMeasuredSpectrum (const std::string& spectrumId)
{
    auto spectrumData = PeakSpectrum();
    PeakAnalyserClient *analyser = this;

    /* WebSocket client cannot get measured data directly, so we user an HTTP client. */
    if (m_rpcClient->scheme() == "ws://")
        analyser = new PeakAnalyserClient("http" + m_uri.substr(2));

    json spectrum = analyser->call("GetMeasuredSpectrum", spectrumId);
    spectrumData.fromSpectrumChannel(spectrum["SpectrumChannels"].front());

    if (analyser != this) delete analyser;

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
