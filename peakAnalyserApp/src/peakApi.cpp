#include "peakApi.h"

#include "JsonRPCClientHTTP.h"

#include <fstream>
#include <iostream>

#include <epicsEvent.h>

using json = nlohmann::json;

void base64_decode(const std::string&, unsigned char *, size_t);

/*
 * PeakSpectrum
 *
 */
void PeakSpectrum::fromSpectrumChannel(const nlohmann::json& channel)
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

/*
 * PeakAPI
 *
 */
PeakAPI::PeakAPI(const std::string& uri)
{
    std::string peak_uri = uri;
    if (uri.rfind("/api") != uri.size() - 4)
        peak_uri += "/api";
    m_rpcClient.reset(new JsonRPCClientHTTP(peak_uri));
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

std::string PeakAPI::subscribe(const std::string& notification, JsonRPCClientI::Callback callback, AttachmentMode attachmentMode)
{
    static size_t id = 0;

    json attachmentSetting = {
        { "AttachmentMode", attachmentModeToString(attachmentMode) },
        { "AttachmentCompressionMode", "None"} // Options: None, Deflate, GZip, Brotli
    };

    std::string method = notification + std::to_string(id++);
    std::string guid = call("Subscribe",
                       notification,
                       method,
                       attachmentSetting,
                       m_rpcClient->notificationServer()).get<std::string>();

    m_rpcClient->subscribe(method, callback);

    // append to the subscribers list
    m_subscribers.emplace(guid, method);

    return guid;
}

void PeakAPI::unsubscribe(const std::string& guid)
{
    for(auto it = m_subscribers.begin(); it != m_subscribers.end(); it++) {
        if ((*it).first == guid) {
            call("Unsubscribe", guid);
            m_rpcClient->unsubscribe((*it).second);
            m_subscribers.erase(it);
            break;
        }
    }
}

void PeakAPI::reset()
{
    epicsEventId completedEventId = epicsEventCreate(epicsEventEmpty);

    std::string guid = subscribe("ResetCompleted",
            [&](const json&) {
                epicsEventSignal(completedEventId);
            }
    );

    call("BeginReset");

    epicsEventWait(completedEventId);

    unsubscribe(guid);

    call("EndReset");

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
    call("StartMeasurement", "Sequence", "Acquisition");
}

void PeakAnalyserClient::finishMeasurement ()
{
    call("FinishMeasurement");
}

void PeakAnalyserClient::start (const std::string& spectrumId)
{
    call("BeginAcquisition", spectrumId);
}

void PeakAnalyserClient::stop ()
{
    call("EndAcquisition");
}

void PeakAnalyserClient::abort ()
{
    call("CancelAcquisition");
}

void PeakAnalyserClient::acquire (const std::string& spectrumId)
{
    epicsEventId completedEventId = epicsEventCreate(epicsEventEmpty);

    std::string guid = subscribe("AcquisitionCompleted",
            [&](const json& params) {
                std::string notifiedSpectrumId = params[0].get<std::string>();
                if (notifiedSpectrumId == spectrumId) {
                    epicsEventSignal(completedEventId);
                }
            }
    );

    start(spectrumId);

    epicsEventWait(completedEventId);

    unsubscribe(guid);

    stop();

    epicsEventDestroy(completedEventId);
}

PeakSpectrum PeakAnalyserClient::getMeasuredSpectrum (const std::string& spectrumId)
{
   auto spectrumData = PeakSpectrum();
   const json& spectrum = call("GetMeasuredSpectrum", spectrumId);
   for (auto& chan: spectrum["SpectrumChannels"]) {
        spectrumData.fromSpectrumChannel(chan);
        break;
   }
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
