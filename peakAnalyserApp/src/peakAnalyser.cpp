#include <string>
#include <vector>
#include <iostream>
#include <limits>
#include <cmath>
#include <algorithm>

/* EPICS includes */
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsString.h>

/* areaDetector includes */
#include <ADDriver.h>

/* Device Specific API */
#include "peakApi.h"
using namespace nlohmann;

#include <iocsh.h>
#include <epicsExport.h>

#define DRIVER_VERSION "2.1.0"

/* Analyser Modes */
#define ElementSetString            "ELEMENT_SETS"
#define ElementSetStrString         "ELEMENT_SETS_STR"
#define LensModeString              "LENS_MODES"
#define LensModeStrString           "LENS_MODES_STR"
#define PassEnergyString            "PASS_ENERGY"
#define PassEnergyValueString       "PASS_ENERGY_VAL"
#define DetectorModeString          "DETECTOR_MODE"
#define DetectorModeStrString       "DETECTOR_MODE_STR"
#define EnergyModeString            "ENERGY_MODE"
#define EnergyModeStrString         "ENERGY_MODE_STR"

/* Analyser Spectrum Region */
#define AnalyserAcquisitionModeString "ACQUISITION_MODE"
#define AnalyserHighEnergyString    "HIGH_ENERGY"
#define AnalyserLowEnergyString     "LOW_ENERGY"
#define AnalyserCenterEnergyString  "CENTER_ENERGY"
#define AnalyserEnergyStepString    "ENERGY_STEP"
#define AnalyserEnergyCountString   "ENERGY_COUNT"

#define AnalyserWithSliceString     "WITH_SLICE"
#define AnalyserHighSliceString     "HIGH_SLICE"
#define AnalyserLowSliceString      "LOW_SLICE"
#define AnalyserCenterSliceString   "CENTER_SLICE"
#define AnalyserSliceStepString     "SLICE_STEP"
#define AnalyserSliceCountString    "SLICE_COUNT"

#define AnalyserWithAngleYString    "WITH_ANGLE_Y"
#define AnalyserHighAngleYString    "HIGH_ANGLE_Y"
#define AnalyserLowAngleYString     "LOW_ANGLE_Y"
#define AnalyserCenterAngleYString  "CENTER_ANGLE_Y"
#define AnalyserAngleYStepString    "ANGLE_Y_STEP"
#define AnalyserAngleYCountString   "ANGLE_Y_COUNT"

#define ExcitationEnergyString      "EXCITATION_ENERGY"
#define WorkFunctionString          "WORK_FUNCTION"
#define DetectorChannelsString      "DETECTOR_CHANNELS"
#define DetectorSlicesString        "DETECTOR_SLICES"
#define AcqETAString                "ACQ_ETA"
#define AcqETAStrString             "ACQ_ETA_STR"
#define AcqNumStepsString           "NSTEPS"
#define AcqNumStepsCounterString    "NSTEPS_COUNTER"

/* Direct Electronics Control */
#define ZeroSuppliesString          "ZERO_SUPPLIES"

static const char *driverName = "peakAnalyser";

static void peakAnalyserTaskC(void *drvPvt);

typedef struct {
    std::string name;
    bool hasXDeflection;
    bool hasYDeflection;
    double xDeflectionRange[2];
    double yDeflectionRange[2];
    double passEnergyRange[2];
} LensModeSetting;

class peakAnalyser : public ADDriver
{
public:
    peakAnalyser(const char *portName, const char *hostAddress, int maxBuffers, size_t maxMemory, int priority, int stackSize);
    virtual ~peakAnalyser();
    virtual asynStatus readEnum(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements, size_t *nIn);
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual);
    void report(FILE *fp, int details);
    void peakAnalyserTask();

protected:
    void connectAnalyser();
    void setROI();
    void setPassEnergy(double passEnergy);
    void setElementSet(const std::string& elementSet);
    void setLensMode(const std::string& lensMode);
    void setDetectorMode(const std::string& detectorMode);
    void setupSpectrumDefinition();
    void setRegion(const json& channelSettings);
    double clamp(double value, const double (&range)[2]);
    std::string strftime(int seconds);

    #define FIRST_PEAKANALYSER_PARAM ElementSet
    int ElementSet;             /**< (asynInt32,        r/o) current element set index */
    int ElementSetStr;          /**< (asynOctet,        r/o) current element set name */
    int LensMode;               /**< (asynInt32,        r/w) current lens mode index */
    int LensModeStr;            /**< (asynOctet,        r/o) current lens mode name */
    int DetectorMode;           /**< (asynInt32,        r/w) Specifies whether the detector is running in ADC mode (1=YES), or Pulse Counting mode (0=No).*/
    int DetectorModeStr;        /**< (asynOctet,        r/o) the string of DetectorMode */
    int PassEnergy;             /**< (asynInt32,        r/w) select a pass energy from the list of available pass energies for the current lens mode.*/
    int PassEnergyValue;        /**< (asynFloat64,      r/o) the value of PassEnergy */
    int EnergyMode;             /**< (asynInt32,        r/w) Determines if the energy scale is in 0:Kinetic 1:Binding mode. */
    int EnergyModeStr;          /**< (asynOctet,        r/o) the string of EnergyMode */

    int AnalyserAcquisitionMode;/**< (asynInt32,        r/w) Determines if the region will be measured in 0: Fixed, 1: Sweep Energy, 2: Sweep ThetaY. Sweep Energy&ThetaY. */

    int AnalyserHighEnergy;     /**< (asynFloat64,      r/w) Specifies the high-end kinetic energy (eV) for swept mode acquisition. */
    int AnalyserLowEnergy;      /**< (asynFloat64,      r/w) Specifies the low-end kinetic energy (eV) for swept mode acquisition. */
    int AnalyserCenterEnergy;   /**< (asynFloat64,      r/w) Specifies the center energy (eV) for fixed mode acquisition. */
    int AnalyserEnergyStep;     /**< (asynFloat64,      r/w) Specifies the energy step size (eV) for swept mode acquisition. */
    int AnalyserEnergyCount;    /**< (asynInt32,        r/o) Number of channels in energy. */

    int AnalyserWithSlice;      /**< (asynInt32,        r/o) Whether Slice is supported in the current lens mode. */
    int AnalyserHighSlice;      /**< (asynFloat64,      r/w) High-end ThetaX (degree/mm). */
    int AnalyserLowSlice;       /**< (asynFloat64,      r/w) Low-end ThetaX (degree/mm). */
    int AnalyserCenterSlice;    /**< (asynFloat64,      r/w) Center ThetaX (degree/mm). */
    int AnalyserSliceStep;      /**< (asynFloat64,      r/w) Theta X step size (degree/mm). */
    int AnalyserSliceCount;     /**< (asynInt32,        r/o) Number of channels in Theta X. */

    int AnalyserWithAngleY;     /**< (asynInt32,        r/o) Whether ThetaY is supported in the current lens mode. */
    int AnalyserHighAngleY;     /**< (asynFloat64,      r/w) Specifies the high-end ThetaY (deg) for swept mode acquisition. */
    int AnalyserLowAngleY;      /**< (asynFloat64,      r/w) Specifies the low-end ThetaY energy (deg) for swept mode acquisition. */
    int AnalyserCenterAngleY;   /**< (asynFloat64,      r/w) Specifies the center ThetaY (deg) for fixed mode acquisition. */
    int AnalyserAngleYStep;     /**< (asynFloat64,      r/w) Specifies the ThetaY step size (deg) for swept mode acquisition. */
    int AnalyserAngleYCount;    /**< (asynInt32,        r/o) Number of channels in Theta Y. */

    int ExcitationEnergy;       /**< (asynFloat64,      r/w) Specifies the excitation energy used in Binding mode (eV) */
    int WorkFunction;           /**< (asynFloat64,      r/w) Specifies the work function used in Binding mode (eV) */
    int DetectorChannels;       /**< (asynInt32,        r/w) Specifies the current number of X channels (energy). */
    int DetectorSlices;         /**< (asynInt32,        r/w) Specifies the current number of Y channels (slices). */
    int AcqETA;                 /**< (asynFloat64,      r/o) Estimated Time of Acqusisition in seconds */
    int AcqETAStr;              /**< (asynOctet,        r/o) Estimated Time of Acqusisition in hh::mm::ss format */
    int AcqNumSteps;            /**< (asynInt32,        r/o) Number of steps to acquire */
    int AcqNumStepsCounter;     /**< (asynInt32,        r/o) Number of steps acquired */

    int ZeroSupplies;           /**< (asynInt32,        r/w) reset the power supplies to zero*/
    #define LAST_PEAKANALYSER_PARAM ZeroSupplies

private:
    epicsEventId startEventId;
    epicsEventId stopEventId;

    std::unique_ptr<PeakAnalyserClient> analyser;
    std::unique_ptr<PeakElectronicsClient> electronics;
    std::string m_ManagerHost;
    json m_SpectrumDefinition;
    /* Analyser specific parameters */
    std::vector<std::string> m_Elementsets;
    std::vector<LensModeSetting> m_LensModes;
    std::vector<double> m_PassEnergies;
    std::vector<std::string> m_DetectorModes;
    std::string m_DetectorModesName;
    std::map<int, double> m_RequestedAxes;
    /* Detector specific parameters */
    double m_PixelDensity; // pixels per millimeter
};

/* Number of asyn parameters (asyn commands) this driver supports*/
#define NUM_PEAKANALYSER_PARAMS (&LAST_PEAKANALYSER_PARAM - &FIRST_PEAKANALYSER_PARAM + 1)

/* peakAnalyser destructor */
peakAnalyser::~peakAnalyser()
{
}

/* peakAnalyser constructor */
peakAnalyser::peakAnalyser(const char *portName, const char *hostAddress, int maxBuffers, size_t maxMemory, int priority, int stackSize)
    : ADDriver(portName, 1, (int)NUM_PEAKANALYSER_PARAMS, maxBuffers, maxMemory,
               asynEnumMask | asynFloat64ArrayMask, asynEnumMask | asynFloat64ArrayMask,
               ASYN_CANBLOCK, 1,   /* ASYN_CANBLOCK=1, ASYN_MULTIDEVICE=0, autoConnect=1 */
               priority, stackSize), m_ManagerHost(hostAddress)
{
    int status = asynSuccess;
    const char *functionName = "peakAnalyser";

    createParam(ElementSetString, asynParamInt32, &ElementSet);
    createParam(ElementSetStrString, asynParamOctet, &ElementSetStr);
    createParam(LensModeString, asynParamInt32, &LensMode);
    createParam(LensModeStrString, asynParamOctet, &LensModeStr);
    createParam(DetectorModeString, asynParamInt32, &DetectorMode);
    createParam(DetectorModeStrString, asynParamOctet, &DetectorModeStr);
    createParam(PassEnergyString, asynParamInt32, &PassEnergy);
    createParam(PassEnergyValueString, asynParamFloat64, &PassEnergyValue);
    createParam(EnergyModeString, asynParamInt32, &EnergyMode);
    createParam(EnergyModeStrString, asynParamOctet, &EnergyModeStr);

    createParam(AnalyserAcquisitionModeString, asynParamInt32, &AnalyserAcquisitionMode);
    createParam(AnalyserHighEnergyString, asynParamFloat64, &AnalyserHighEnergy);
    createParam(AnalyserLowEnergyString, asynParamFloat64, &AnalyserLowEnergy);
    createParam(AnalyserCenterEnergyString, asynParamFloat64, &AnalyserCenterEnergy);
    createParam(AnalyserEnergyStepString, asynParamFloat64, &AnalyserEnergyStep);
    createParam(AnalyserEnergyCountString, asynParamInt32, &AnalyserEnergyCount);

    createParam(AnalyserWithSliceString, asynParamInt32, &AnalyserWithSlice);
    createParam(AnalyserHighSliceString, asynParamFloat64, &AnalyserHighSlice);
    createParam(AnalyserLowSliceString, asynParamFloat64, &AnalyserLowSlice);
    createParam(AnalyserCenterSliceString, asynParamFloat64, &AnalyserCenterSlice);
    createParam(AnalyserSliceStepString, asynParamFloat64, &AnalyserSliceStep);
    createParam(AnalyserSliceCountString, asynParamInt32, &AnalyserSliceCount);

    createParam(AnalyserWithAngleYString, asynParamInt32, &AnalyserWithAngleY);
    createParam(AnalyserHighAngleYString, asynParamFloat64, &AnalyserHighAngleY);
    createParam(AnalyserLowAngleYString, asynParamFloat64, &AnalyserLowAngleY);
    createParam(AnalyserCenterAngleYString, asynParamFloat64, &AnalyserCenterAngleY);
    createParam(AnalyserAngleYStepString, asynParamFloat64, &AnalyserAngleYStep);
    createParam(AnalyserAngleYCountString, asynParamInt32, &AnalyserAngleYCount);

    createParam(ExcitationEnergyString, asynParamFloat64, &ExcitationEnergy);
    createParam(WorkFunctionString, asynParamFloat64, &WorkFunction);
    createParam(DetectorChannelsString, asynParamInt32, &DetectorChannels);
    createParam(DetectorSlicesString, asynParamInt32, &DetectorSlices);
    createParam(AcqETAString, asynParamFloat64, &AcqETA);
    createParam(AcqETAStrString, asynParamOctet, &AcqETAStr);
    createParam(AcqNumStepsString, asynParamInt32, &AcqNumSteps);
    createParam(AcqNumStepsCounterString, asynParamInt32, &AcqNumStepsCounter);

    createParam(ZeroSuppliesString, asynParamInt32, &ZeroSupplies);

    /* Initialise variables */
    setStringParam(NDDriverVersion, DRIVER_VERSION);
    setStringParam(ADManufacturer, "Scienta Omicron");

    /* Connect Analyser */
    try {
        connectAnalyser();
    } catch (std::exception& e) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: Error in connectAnalyser: %s\n",
            driverName, functionName, e.what());
    #if ADCORE_VERSION>3 || (ADCORE_VERSION == 3 && ADCORE_REVISION >= 10)
        this->deviceIsReachable = false;
    #endif
        this->disconnect(pasynUserSelf);
        setIntegerParam(ADStatus, ADStatusDisconnected);
        setStringParam(ADStatusMessage, "No connection to PEAK server");
    }

    /* Signal to the acquistion task to start the acquisition */
    this->startEventId = epicsEventCreate(epicsEventEmpty);
    if (!this->startEventId) {
        asynPrint(this->pasynUserSelf,ASYN_TRACE_ERROR,
                "%s:%s epicsEventCreate failure for start event\n",
                driverName, functionName);
    }

    /* Signal to the acquistion task to stop the acquisition */
    this->stopEventId = epicsEventCreate(epicsEventEmpty);
    if (!this->stopEventId) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: epicsEventCreate failure for stop event\n",
                driverName, functionName);
    }

    /* Create the acquisition thread */
    status = (epicsThreadCreate("PeakAnalyserTask",
        epicsThreadPriorityMedium, epicsThreadGetStackSize(
        epicsThreadStackMedium),
        (EPICSTHREADFUNC) peakAnalyserTaskC, this) == NULL);

    if (status) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: epicsTheadCreate failure for acquisition task\n",
                driverName, functionName);
    }
}

static void peakAnalyserTaskC(void *drvPvt)
{
    peakAnalyser *pPvt = (peakAnalyser *) drvPvt;
    pPvt->peakAnalyserTask();
}

/** Task to acquire spectrum from analyser and send them up to areaDetector.
 *
 *  It is started in the class constructor and must not return until the IOC stops.
 *
 */
void peakAnalyser::peakAnalyserTask()
{
    int status = asynSuccess;
    int adStatus;
    int acquire;
    int arrayCallbacks;
    int acquisitionMode;
    int imageMode, imageCounter;
    int numExposures, numExposuresCounter;
    int numImages, numImagesCounter;
    int numStepsCounter;
    epicsTimeStamp startTime;
    std::string spectrumId, guidAcquired, guidCompleted;
    std::string statusMessage;
    NDArray *pImage;
    const char *functionName = "peakAnalyserTask";

    this->lock();
    while (1)
    {
        getIntegerParam(ADAcquire, &acquire);
        getIntegerParam(ADStatus, &adStatus);
        /* If we are not acquiring or encountered a problem then wait for a semaphore
         * that is given when acquisition is started */
        if (!acquire)
        {
            /* Only set the status message if we didn't encounter a problem last time,
             * so we don't overwrite the error mesage */
            if(adStatus == (int)ADStatusIdle)
            {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                        "%s:%s: Waiting for the acquire command\n",
                        driverName, functionName);
                setStringParam(ADStatusMessage, "Waiting for the acquire command");
                callParamCallbacks();
            }
            /* Release the lock while we wait for the start event then lock again */
            this->unlock();
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Waiting for acquire to start\n",
                    driverName, functionName);
            epicsEventWait(this->startEventId);
            this->lock();
            /* We are acquiring. */
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: We are acquiring\n",
                    driverName, functionName);

            status = asynSuccess;
            getIntegerParam(ADAcquire, &acquire);
            setIntegerParam(ADNumExposuresCounter, 0);
            setIntegerParam(ADNumImagesCounter, 0);
            setIntegerParam(ADStatus, ADStatusAcquire);
            setStringParam(ADStatusMessage, "Acquiring....");
        }
        epicsTimeGetCurrent(&startTime);

        /* Reset the counters */
        setIntegerParam(ADNumExposuresCounter, 0);
        callParamCallbacks();

        getIntegerParam(AnalyserAcquisitionMode, &acquisitionMode);
        getIntegerParam(ADNumExposures, &numExposures);
        getIntegerParam(ADNumImages, &numImages);
        getIntegerParam(ADImageMode, &imageMode);
        getIntegerParam(NDArrayCounter, &imageCounter);

        getIntegerParam(ADNumImagesCounter, &numImagesCounter);
        getIntegerParam(ADNumExposuresCounter, &numExposuresCounter);

        this->unlock();

        /* Acquire */
        PeakSpectrum spectrum;
        try {
            // first frame setup
            if (numImagesCounter == 0) {
                std::string state = analyser->currentState();
                if (state != "Active")
                    throw std::runtime_error("Analyser not ready: " + state);

                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Start measurement\n",
                    driverName, functionName);

                spectrumId = analyser->defineSpectrum(m_SpectrumDefinition);
                analyser->startMeasurement();
                analyser->setupSpectrum(spectrumId);

                guidCompleted = analyser->subscribeToState(
                    [&](const json& params) {
                        const json stateChange = params[0];
                        std::string oldState = stateChange["OldState"].get<std::string>();
                        std::string newState = stateChange["NewState"].get<std::string>();
                        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                            "%s:%s: Analyser state changed %s -> %s\n",
                            driverName, functionName, oldState.c_str(), newState.c_str());
                        if (newState == "Measuring" || newState == "Error") {
                            epicsEventSignal(this->stopEventId);
                        }
                    }
                );

                guidAcquired = analyser->subscribe("SpectrumAcquired",
                    [&](const json& params) {
                        const json spectrum = params[0]["Value"];
                        if (spectrum.is_null())
                            return;
                        this->lock();
                        // correct estimated spectrum region range with actual values
                        if (numStepsCounter == 0) {
                            const json spectrumChannel = spectrum["SpectrumChannels"].front();
                            const json channelSettings = spectrumChannel["SpectrumChannelSettings"];
                            setRegion(channelSettings);
                        }
                        setIntegerParam(AcqNumStepsCounter, ++numStepsCounter);
                        callParamCallbacks();
                        this->unlock();
                    },
                    PeakAnalyserClient::None // request no image data
                );
            }

            // accumulate num of exposures
            analyser->clearSpectrum(spectrumId);
            while (numExposuresCounter ++ < numExposures) {
                // zero step counter
                numStepsCounter = 0;
                // clear uncaught stopEvent, possibly from repetive subscribed notification
                epicsEventTryWait(this->stopEventId);
                // begin acquisition
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Start acquisition iteration %d/%d\n",
                    driverName, functionName, numExposuresCounter, numExposures);
                analyser->start(spectrumId);

                // wait until completed or aborted by user
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Wait for acquisition to finish\n",
                    driverName, functionName);

                epicsEventWait(this->stopEventId);

                this->lock();
                getIntegerParam(ADAcquire, &acquire);
                if (acquire) { // normal completion
                    setIntegerParam(ADNumExposuresCounter, numExposuresCounter);
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                        "%s:%s: Acquisition iteration %d/%d finished\n",
                        driverName, functionName, numExposuresCounter, numExposures);
                }
                callParamCallbacks();
                this->unlock();

                // aborted if Acquire=0
                if (acquire == 0) {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                        "%s:%s: Acquisition iteration %d/%d aborted\n",
                        driverName, functionName, numExposuresCounter, numExposures);
                    break;
                }
            }

            // get accumulated spectrum, maybe incomplete when aborted
            spectrum = analyser->getMeasuredSpectrum(spectrumId);

            if (acquire) {
                imageCounter++;
                numImagesCounter++;
            }

            /* Check to see if acquisition is complete */
            if ( acquire == 0 ||
                (imageMode == (int)ADImageSingle) ||
                ((imageMode == (int)ADImageMultiple) && (numImagesCounter >= numImages))) {

                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Finish measurement\n",
                    driverName, functionName);
                analyser->unsubscribe(guidCompleted);
                guidCompleted = "";
                analyser->unsubscribe(guidAcquired);
                guidAcquired = "";
                analyser->finishSpectrum(spectrumId);
                spectrumId = "";
                analyser->finishMeasurement();
            }
        } catch (std::exception& e) {
            status = asynError;
            statusMessage = e.what();
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: Acquisition failed %s\n",
                    driverName, functionName, e.what());
            // cleanup
            try {
                if (!guidCompleted.empty()) {
                    analyser->unsubscribe(guidCompleted);
                    guidCompleted = "";
                }
                if (!guidAcquired.empty()) {
                    analyser->unsubscribe(guidAcquired);
                    guidAcquired = "";
                }
                if (analyser->currentState() == "Measuring") {
                    if (!spectrumId.empty()) {
                        analyser->finishSpectrum(spectrumId);
                        spectrumId = "";
                    }
                    analyser->finishMeasurement();
                }
            } catch (std::exception& e) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: Cleanup failed %s\n",
                    driverName, functionName, e.what());
            }
        }

        this->lock();

        // errored
        if (status) {
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusError);
            setStringParam(ADStatusMessage, statusMessage.c_str());
            callParamCallbacks();
            continue;
        }

        // aborted
        if (!acquire) {
            setIntegerParam(ADStatus, ADStatusAborted);
            setStringParam(ADStatusMessage, "User aborted");
            callParamCallbacks();
            continue;
        }

        setIntegerParam(NDArrayCounter, imageCounter);
        setIntegerParam(ADNumImagesCounter, numImagesCounter);

        /* Allocate a new image buffer */
        pImage = this->pNDArrayPool->alloc((int)spectrum.rank, spectrum.dims, NDFloat32, 0,  NULL);
        memcpy(pImage->pData,  (void *)spectrum.data.data(), spectrum.data.size() * sizeof(float));

        pImage->uniqueId = (int)imageCounter;
        pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;

        /* Get any attributes that have been defined for this driver */
        this->getAttributes(pImage->pAttributeList);

        getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
        if (arrayCallbacks)
        {
            /* Must release the lock here, or we can get into a deadlock, because we can
             * block on the plugin lock, and the plugin can be calling us */
            this->unlock();
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Calling NDArray callback\n",
                    driverName, functionName);
            doCallbacksGenericPointer(pImage, NDArrayData, 0);
            this->lock();
        }

        /* Free the image buffers */
        pImage->release();

        /* Check to see if acquisition is complete */
        getIntegerParam(ADAcquire, &acquire);
        if (!acquire ||
            (imageMode == (int)ADImageSingle) ||
            ((imageMode == (int)ADImageMultiple) && (numImagesCounter >= numImages))) {
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusIdle);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Acquisition completed\n",
                    driverName, functionName);
        }
        /* Call the callbacks to update any changes */
        callParamCallbacks();
        getIntegerParam(ADAcquire, &acquire);
    }
}

/** Called when asyn clients call pasynEnum->read().
  * The base class implementation simply prints an error message.
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] strings Array of string pointers.
  * \param[in] values Array of values
  * \param[in] severities Array of severities
  * \param[in] nElements Size of value array
  * \param[out] nIn Number of elements actually returned */
asynStatus peakAnalyser::readEnum(asynUser *pasynUser,
        char *strings[], int values[], int severities[], size_t nElements, size_t *nIn)
{
    int function = pasynUser->reason;
    const char *functionName = "readEnum";
    size_t i;
    char buf[5];

    if (function == PassEnergy)
    {
        for (i=0; ((i<m_PassEnergies.size()) && (i<nElements)); i++)
        {
            if (strings[i])
            {
                free(strings[i]);
            }
            sprintf(buf, "%d", (int)m_PassEnergies.at(i));
            strings[i] = epicsStrDup(buf);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Reading pass energy of %s\n",
                    driverName, functionName, strings[i]);
            values[i] = (int)i;
            severities[i] = 0;
        }
    }
    else if (function == ElementSet)
    {
        for (i=0; ((i<m_Elementsets.size()) && (i<nElements)); i++)
        {
            if (strings[i])
            {
                free(strings[i]);
            }
            strings[i] = epicsStrDup(m_Elementsets.at(i).c_str());
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Reading element set of %s\n",
                    driverName, functionName, strings[i]);
            values[i] = (int)i;
            severities[i] = 0;
        }
    }
    else if (function == LensMode)
    {
        for (i=0; ((i<m_LensModes.size()) && (i<nElements)); i++)
        {
            if (strings[i])
            {
                free(strings[i]);
            }
            strings[i] = epicsStrDup(m_LensModes[i].name.c_str());
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Reading lens mode of %s\n",
                    driverName, functionName, strings[i]);
            values[i]  = (int)i;
            severities[i] = 0;
        }
    }
    else if (function == DetectorMode)
    {
        for (i=0; ((i<m_DetectorModes.size()) && (i<nElements)); i++)
        {
            if (strings[i])
            {
                free(strings[i]);
            }
            strings[i] = epicsStrDup(m_DetectorModes.at(i).c_str());
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Reading detector mode of %s\n",
                    driverName, functionName, strings[i]);
            values[i]  = (int)i;
            severities[i] = 0;
        }
    }
    else
    {
        *nIn = 0;
        return asynError;
    }
    *nIn = i;
    return asynSuccess;
}

/** Called when asyn clients call pasynInt32->write().
 * Write integer value to the drivers parameter table.
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value The value for this parameter
 * \return asynStatus Either asynError or asynSuccess
 */
asynStatus peakAnalyser::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int status = asynSuccess;
    int function = pasynUser->reason;
    int oldValue;
    char message[256];
    const char *functionName = "writeInt32";

    getIntegerParam(function, &oldValue);
    status = setIntegerParam(function, value);

    if (function == ADAcquire)
    {
        int adStatus;
        getIntegerParam(ADStatus, &adStatus);

        if (value && adStatus != (int)ADStatusAcquire && adStatus != (int)ADStatusDisconnected)
            epicsEventSignal(this->startEventId);

        if (!value && adStatus == (int)ADStatusAcquire) {
            try {
                analyser->abort();
            } catch (std::exception& e) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: Failed to abort acquisition: %s\n",
                    driverName, functionName, e.what());
            }
        }
    }
    else if (function == NDDataType)
    {
        /* Lock the data type to Float32 as all
         * data collected will be of float type */
        setIntegerParam(NDDataType, NDFloat32);
    }
    else if (function == NDColorMode)
    {
        /* Lock the color mode to mono as all
         * data collected will be monochrome */
        setIntegerParam(NDColorMode, NDColorModeMono);
    }
    else if (function == ADNumExposures)
    {
        // update estimated acquisition time
        double eta;
        getDoubleParam(AcqETA, &eta);
        eta = eta / oldValue * value;

        setDoubleParam(AcqETA, eta);
        setStringParam(AcqETAStr, this->strftime(int(eta)));
    }
    else if (function == AnalyserAcquisitionMode)
    {
        /* Check whether ThetaY is supported in current LensMode */
        int lensMode;
        getIntegerParam(LensMode, &lensMode);
        if ((value & 0x02) && !m_LensModes[lensMode].hasYDeflection) {
            epicsSnprintf(message, sizeof(message),
                    "LensMode '%s' does not support ThetaY",
                    m_LensModes[lensMode].name.c_str());
            setStringParam(ADStatusMessage, message);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: %s\n",
                    driverName, functionName, message);

            setIntegerParam(AnalyserAcquisitionMode, oldValue);
        } else {
            setupSpectrumDefinition();
        }
    }
    else if (function == EnergyMode)
    {
        setupSpectrumDefinition();
        setStringParam(EnergyModeStr, value ? "Binding" : "Kinetic");
    }
    else if (function == DetectorChannels)
    {
        int sizeX;
        getIntegerParam(ADSizeX, &sizeX);
        if (value > sizeX) {
            setIntegerParam(DetectorChannels, sizeX);
        } else if (value <= 0) {
            setIntegerParam(DetectorChannels, 1);
        }
        int acquisitionMode;
        getIntegerParam(AnalyserAcquisitionMode, &acquisitionMode);
        if ((acquisitionMode & 0x01) == 0 || (acquisitionMode & 0x02) == 0)
            setupSpectrumDefinition();

    }
    else if (function == DetectorSlices)
    {
        int sizeY;
        getIntegerParam(ADSizeY, &sizeY);
        if (value > sizeY) {
            setIntegerParam(DetectorSlices, sizeY);
        } else if (value <= 0) {
            setIntegerParam(DetectorSlices, 1);
        }
        setupSpectrumDefinition();
    }
    else if (function == LensMode)
    {
        if (value < (int)m_LensModes.size())
        {
            int acquisitionMode;
            getIntegerParam(AnalyserAcquisitionMode, &acquisitionMode);
            if ((acquisitionMode & 0x02) && !m_LensModes[value].hasYDeflection) {
                epicsSnprintf(message, sizeof(message),
                        "LensMode '%s' does not support ThetaY",
                        m_LensModes[value].name.c_str());
                setStringParam(ADStatusMessage, message);
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s: %s\n",
                        driverName, functionName, message);
                setIntegerParam(LensMode, oldValue);
            } else {
                setIntegerParam(AnalyserWithSlice, m_LensModes[value].hasXDeflection);
                setIntegerParam(AnalyserWithAngleY, m_LensModes[value].hasYDeflection);
                setupSpectrumDefinition();
            }
        }
        else
        {
            epicsSnprintf(message, sizeof(message),
                    "Set 'Lens_Mode' failed, index must be between 0 and %zu",
                    m_LensModes.size());
            setStringParam(ADStatusMessage, message);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: %s\n",
                    driverName, functionName, message);
            setIntegerParam(LensMode, oldValue);
        }
    }
    else if (function == PassEnergy)
    {
        if (value < (int)m_PassEnergies.size())
        {
            setupSpectrumDefinition();
        }
        else
        {
            epicsSnprintf(message, sizeof(message),
                    "Set 'Pass_Energy' failed, index must be between 0 and %zu",
                    m_PassEnergies.size());
            setStringParam(ADStatusMessage, message);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: %s\n",
                    driverName, functionName, message);
            setIntegerParam(PassEnergy, oldValue);
        }
    }
    else if (function == ADMinX ||
             function == ADMinY ||
             function == ADSizeX ||
             function == ADSizeY) {
        setROI();
        setupSpectrumDefinition();
    }
    else if (function == ZeroSupplies)
    {
        if (value == 1) {
            int AcquisitionState = 0;
            getIntegerParam(ADAcquire, &AcquisitionState);
            if(AcquisitionState == (int)ADStatusAcquire) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s: Cannot zero power supplies during acquisition\n",
                        driverName, functionName);
            } else {
                try {
                    electronics->zeroAllSupplies();
                    setStringParam(ADStatusMessage, "Power supplies reset");
                } catch (std::exception& e) {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s: Failed to zero power supplies: %s\n",
                        driverName, functionName, e.what());
                }
            }
        }
    }
    else
    {
        /* If this is not a parameter we have handled call the base class */
        if (function < FIRST_PEAKANALYSER_PARAM)
            status = ADDriver::writeInt32(pasynUser, value);
    }

    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();
    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s:%s: error, status=%d function=%d, value=%d\n",
                driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%d\n",
                driverName, functionName, function, value);
    return asynSuccess;
}

/** Write double value to the drivers parameter table.
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value The value for this parameter
 * \return asynStatus Either asynError or asynSuccess
 */
asynStatus peakAnalyser::writeFloat64(asynUser *pasynUser,
        epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    double oldValue;
    const char *functionName = "writeFloat64";

    getDoubleParam(function, &oldValue);

    /* Set the parameter and readback in the parameter library.
     * This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setDoubleParam(function, value);

    /* Whether calling setupSpectrumDefiniton depends on acquistion mode */
    int acquisitionMode, energyMode;
    getIntegerParam(AnalyserAcquisitionMode, &acquisitionMode);
    getIntegerParam(EnergyMode, &energyMode);

    if (function == ExcitationEnergy ||
        function == WorkFunction) {
        if (energyMode == 1) // Binding
            setupSpectrumDefinition();
    } else if (function == ADAcquireTime) {
        setupSpectrumDefinition();
    } else if ( function == AnalyserCenterSlice) {
        m_RequestedAxes[function] = value;
        setupSpectrumDefinition();
    } else if (
        function == AnalyserHighEnergy ||
        function == AnalyserLowEnergy ||
        function == AnalyserEnergyStep) {
        m_RequestedAxes[function] = value;
        if (acquisitionMode & 0x01) {
            setupSpectrumDefinition();
        } else {
            setDoubleParam(function, oldValue);
        }
    } else if (function == AnalyserCenterEnergy) {
        m_RequestedAxes[function] = value;
        if (acquisitionMode & 0x01) {
            double width = std::abs(
                    m_RequestedAxes[AnalyserHighEnergy] -
                    m_RequestedAxes[AnalyserLowEnergy]
                );
            m_RequestedAxes[AnalyserLowEnergy] = value - width / 2;
            m_RequestedAxes[AnalyserHighEnergy] = value + width / 2;
        }
        setupSpectrumDefinition();
    } else if (
        function == AnalyserHighAngleY ||
        function == AnalyserLowAngleY ||
        function == AnalyserAngleYStep) {
        m_RequestedAxes[function] = value;
        if (acquisitionMode & 0x02) {
            setupSpectrumDefinition();
        } else {
            setDoubleParam(function, oldValue);
        }
    } else if (function == AnalyserCenterAngleY) {
        m_RequestedAxes[function] = value;
        if (acquisitionMode & 0x02) {
            double width = std::abs(
                    m_RequestedAxes[AnalyserHighAngleY] -
                    m_RequestedAxes[AnalyserLowAngleY]
                );
            m_RequestedAxes[AnalyserLowAngleY] = value - width / 2;
            m_RequestedAxes[AnalyserHighAngleY] = value + width / 2;
        }
        setupSpectrumDefinition();
    } else {
        /* If this parameter belongs to a base class call its method */
        if (function < FIRST_PEAKANALYSER_PARAM)
            status = ADDriver::writeFloat64(pasynUser, value);
    }

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s:%s error, status=%d function=%d, value=%f\n",
                driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%f\n",
                driverName, functionName, function, value);
    return status;
}
/** Called when asyn clients call pasynOctet->write().
 * This function performs actions for some parameters, including ADFilePath, etc.
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value Address of the string to write.
 * \param[in] nChars Number of characters to write.
 * \param[out] nActual Number of characters actually written. */
asynStatus peakAnalyser::writeOctet(asynUser *pasynUser, const char *value,
        size_t nChars, size_t *nActual)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setStringParam(function, (char *) value);

    {
        /* If this parameter belongs to a base class call its method */
        if (function < FIRST_PEAKANALYSER_PARAM)
            status = ADDriver::writeOctet(pasynUser, value, nChars, nActual);
    }

    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();

    if (status)
        epicsSnprintf(pasynUser->errorMessage,
                pasynUser->errorMessageSize,
                "%s:%s: status=%d, function=%d, value=%s\n",
                driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%s\n",
                driverName, functionName, function, value);
    *nActual = nChars;
    return status;
}

/** Report status of the driver for debugging/testing purpose.
 * Can be invoked from ioc shell. "asynReport <details>,<port>"
 * Prints details about the driver if details>0.
 * It then calls the ADDriver::report() method.
 * \param[in] fp File pointed passed by caller where the output is written to.
 * \param[in] details If >0 then driver details are printed.
 */
void peakAnalyser::report(FILE *fp, int details)
{
    fprintf(fp, "peakAnalyser detector %s\n", this->portName);
    if (details > 0)
    {
        int nx, ny, dataType;
        getIntegerParam(ADSizeX, &nx);
        getIntegerParam(ADSizeY, &ny);
        getIntegerParam(NDDataType, &dataType);
        fprintf(fp, "  NX, NY:              %d  %d\n", nx, ny);
        fprintf(fp, "  Data type:           %d\n", dataType);
    }
    /* Invoke the base class method */
    ADDriver::report(fp, details);
}

/** Connect to PEAK servers.
 * It is normally called at the configuration time, but can also
 * be called to reconnect after PEAK servers restart.
 */
void peakAnalyser::connectAnalyser()
{
    const char *functionName = "connectAnalyser";

    /* Connect Manager server */
    PeakManagerClient manager(m_ManagerHost);
    json serverInfo = manager.query("ServerInfo", json({
        {"Version", json::object()},
        {"ReleaseVersion", json::object()}
    }));
    setStringParam(ADSDKVersion, serverInfo["ReleaseVersion"].get<std::string>());
    setStringParam(ADFirmwareVersion, serverInfo["Version"].get<std::string>());

    /* Get Analyser and Electronics servers address and connect */
    json servers = manager.runningServers();
    std::string analyserAddress;
    std::string electronicsAddress;
    std::string cameraAddress;
    for (auto& el: servers) {
        if (el["Name"] == "Analyser")
            el["Address"].get_to(analyserAddress);
        else if (el["Name"] == "Electronics")
            el["Address"].get_to(electronicsAddress);
        else if (el["Name"] == "Camera")
            el["Address"].get_to(cameraAddress);
    }
    if (electronicsAddress.empty() ||
        analyserAddress.empty() ||
        cameraAddress.empty())
        throw std::runtime_error("Instrument servers not running");

    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: Connect electronics at %s\n",
            driverName, functionName, electronicsAddress.c_str());

    electronics.reset(new PeakElectronicsClient(electronicsAddress));

    asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: Connect analyser at %s\n",
            driverName, functionName, analyserAddress.c_str());

    analyser.reset(new PeakAnalyserClient(analyserAddress));
    analyser->unsubscribeAll();

    /* Get Analyser available configurations (element sets) */
    m_Elementsets = analyser->configurations();

    /* Get Analyser current configuration */
    json configuration = analyser->query("Configuration", json({
        {"Name", json::object()},
        {"Model", json::object()},
        {"SerialNumber", json::object()},
        {"LensModes", {
            {"{}", {
                {"Name", json::object()},
                {"DoubleParameters", json::object()}
            }}
        }},
        {"Calibration", {
            {"CalibratedPassEnergies", json::object()}
        }}
    }));

    m_PassEnergies = configuration["Calibration"]["CalibratedPassEnergies"]
        .get<std::vector<double>>();

    /* Parse lens modes' settings, which has the range of pass energy,
     * and existence of angle x/y and its range if existing.
     */
    m_LensModes.clear();
    for (auto& lensMode: configuration["LensModes"]) {
        LensModeSetting s;

        s.name = lensMode["Name"].get<std::string>();

        const json passEnergy = lensMode["DoubleParameters"]["PassEnergy"];
        s.passEnergyRange[0] = passEnergy["MinimumValue"].get<double>();
        s.passEnergyRange[1] = passEnergy["MaximumValue"].get<double>();

        const json xDeflection = lensMode["DoubleParameters"]["XDeflection"];
        s.hasXDeflection = xDeflection.is_object();
        if (s.hasXDeflection) {
            s.xDeflectionRange[0] = xDeflection["MinimumValue"].get<double>();
            s.xDeflectionRange[1] = xDeflection["MaximumValue"].get<double>();
        }

        const json yDeflection = lensMode["DoubleParameters"]["YDeflection"];
        s.hasYDeflection = yDeflection.is_object();
        if (s.hasYDeflection) {
            s.yDeflectionRange[0] = yDeflection["MinimumValue"].get<double>();
            s.yDeflectionRange[1] = yDeflection["MaximumValue"].get<double>();
        }
        m_LensModes.push_back(s);
    }
    /* Sort lens modes by their names, in case that server returns an unordered list */
    std::sort(m_LensModes.begin(), m_LensModes.end(),
            [](const LensModeSetting& a, const LensModeSetting& b) {
                return a.name < b.name;
            }
    );

    /* PEAK API has renamed this type
     *  v1.0 - DetectorMode - ["ADC", "Pulse"]
     *  v1.1+ - AcquisitionMode - ["Image","Event"]
     * */
    auto types = analyser->call("ApiTypes")["types"];
    for (auto& type: types) {
        if (type["name"] == "AcquisitionMode") {
            m_DetectorModesName = "AcquisitionMode";
            type["enum_values"].get_to(m_DetectorModes);
            break;
        }
        if (type["name"] == "DetectorMode") {
            m_DetectorModesName = "CameraMode";
            type["enum_values"].get_to(m_DetectorModes);
            break;
        }
    }

    /* Initialise parameters from Analyser configuration/settings */
    setStringParam(ADModel, configuration["Model"].get<std::string>());
    setStringParam(ADSerialNumber, configuration["SerialNumber"].get<std::string>());
    setElementSet(configuration["Name"].get<std::string>());

    auto settings = analyser->currentSettings();
    setDoubleParam(ADAcquireTime, settings["DwellTime"].get<double>());
    setDoubleParam(AnalyserCenterEnergy, settings["KineticEnergy"].get<double>());
    setPassEnergy(settings["PassEnergy"].get<double>());
    setLensMode(settings["LensModeName"].get<std::string>());
    setDetectorMode(settings[m_DetectorModesName].get<std::string>());

    /* Define spectrum from current analyser configuration/settings */
    m_SpectrumDefinition = {
        {"Name", "Acquisition" },
        {"ElementSetName", configuration["Name"]},
        {"LensModeName", settings["LensModeName"]},
        {"Detectors", {"Camera"} },
        {"FixedAxes",  { {"X", json::object()} } },
        {"SweepAxes",  json::object() },
        {"PassEnergy", settings["PassEnergy"]},
        {"DwellTime", settings["DwellTime"]},
        {"StoreSpectrum", false}
    };

    /* Use server returned spectrumInfo to get camera dimensions
     * in pixels and millimeters.
     * */
    json spectrumInfo = analyser->calculateSpectrumInfo(m_SpectrumDefinition);
    for (auto& el: spectrumInfo["AnalyserSpectrumChannelSettings"]) {
        int sizeX = el["ChannelArea"]["Width"].get<int>();
        int sizeY = el["ChannelArea"]["Height"].get<int>();
        double physicalSizeX = el["DetectorArea"]["Width"].get<double>();
        setIntegerParam(ADMaxSizeX, sizeX);
        setIntegerParam(ADMaxSizeY, sizeY);
        m_PixelDensity = sizeX / physicalSizeX;
    }

    /* Retrieve calibrated ROI setting from Camera server */
    PeakCameraClient camera(cameraAddress);
    json calibration = camera.geometry();

    int minX = calibration["RoiOffsetX"].get<int>();
    int minY = calibration["RoiOffsetY"].get<int>();
    int sizeX = calibration["RoiWidth"].get<int>();
    int sizeY = calibration["RoiHeight"].get<int>();
    setIntegerParam(ADMinX, minX);
    setIntegerParam(ADMinY, minY);
    setIntegerParam(ADSizeX, sizeX);
    setIntegerParam(ADSizeY, sizeY);
    setIntegerParam(DetectorChannels, sizeX);
    setIntegerParam(DetectorSlices, sizeY);

    setupSpectrumDefinition();
}

void peakAnalyser::setROI()
{
    int minY, minX, sizeX, sizeY, maxSizeX, maxSizeY;

    getIntegerParam(ADMinX, &minX);
    getIntegerParam(ADMinY, &minY);
    getIntegerParam(ADSizeX, &sizeX);
    getIntegerParam(ADSizeY, &sizeY);
    getIntegerParam(ADMaxSizeX, &maxSizeX);
    getIntegerParam(ADMaxSizeY, &maxSizeY);

    if (minX + sizeX > maxSizeX) {
        sizeX = maxSizeX - minX;
        setIntegerParam(ADSizeX, sizeX);
    }

    if (minY + sizeY > maxSizeY) {
        sizeY = maxSizeY - minY;
        setIntegerParam(ADSizeY, sizeY);
    }
}

void peakAnalyser::setPassEnergy(double passEnergy)
{
    auto it = std::find(m_PassEnergies.begin(), m_PassEnergies.end(), passEnergy);
    if (it != m_PassEnergies.end()) {
        setIntegerParam(PassEnergy, (int)(it - m_PassEnergies.begin()));
        setDoubleParam(PassEnergyValue, passEnergy);
    }

}

void peakAnalyser::setElementSet(const std::string& elementSet)
{
    auto it = std::find(m_Elementsets.begin(), m_Elementsets.end(), elementSet);
    if (it != m_Elementsets.end()) {
        setIntegerParam(ElementSet, (int)(it - m_Elementsets.begin()));
        setStringParam(ElementSetStr, elementSet);
    }
}

void peakAnalyser::setLensMode(const std::string& lensMode)
{

    auto it = std::find_if(m_LensModes.begin(), m_LensModes.end(),
            [&lensMode](const LensModeSetting& s) {
                return s.name == lensMode;
            }
    );

    if (it != m_LensModes.end()) {
        setIntegerParam(LensMode, (int)(it - m_LensModes.begin()));
        setStringParam(LensModeStr, lensMode);
    }
}

void peakAnalyser::setDetectorMode(const std::string& detectorMode)
{
    auto it = std::find(m_DetectorModes.begin(), m_DetectorModes.end(), detectorMode);
    if (it != m_DetectorModes.end()) {
        setIntegerParam(DetectorMode, (int)(it - m_DetectorModes.begin()));
        setStringParam(DetectorModeStr, detectorMode);
    }
}

double peakAnalyser::clamp(double value, const double (&range)[2])
{
    if (value < range[0])
        return range[0];
    else if (value > range[1])
        return range[1];
    else
        return value;
}

std::string peakAnalyser::strftime(int seconds)
{
    char digits[4];

    int days, hours, minutes;
    days = seconds / 86400; seconds = seconds % 86400;
    hours = seconds / 3600; seconds = seconds %  3500;
    minutes = seconds / 60; seconds = seconds %    60;

    std::string time;
    if (days)
        time = std::to_string(days) + " days ";

    if (hours || !time.empty()) {
        epicsSnprintf(digits, sizeof(digits), "%02d:", hours);
        time += digits;
    }

    if (minutes || !time.empty()) {
        epicsSnprintf(digits, sizeof(digits), "%02d:", minutes);
        time += digits;
    }

    epicsSnprintf(digits, sizeof(digits), "%02d", seconds);
    time += digits;

    return time;
}

void peakAnalyser::setRegion(const json& channelSettings)
{
    int energyMode;
    double excitationEnergy, workFunction;
    double lowEnergy, centerEnergy, highEnergy, stepEnergy;

    getIntegerParam(EnergyMode, &energyMode);
    getDoubleParam(ExcitationEnergy, &excitationEnergy);
    getDoubleParam(WorkFunction, &workFunction);

    const json& requestedAxes = channelSettings["RequestedAxes"];

    lowEnergy = requestedAxes["X"]["Offset"].get<double>();
    stepEnergy = requestedAxes["X"]["Delta"].get<double>();
    highEnergy = requestedAxes["X"]["Highest"].get<double>();
    centerEnergy = requestedAxes["X"]["Center"].get<double>();

    if (energyMode == 1) { // binding energy
        highEnergy = highEnergy + workFunction - excitationEnergy;
        lowEnergy = lowEnergy + workFunction - excitationEnergy;
        centerEnergy = centerEnergy + workFunction - excitationEnergy;
    }

    setDoubleParam(AnalyserLowEnergy, lowEnergy);
    setDoubleParam(AnalyserEnergyStep, stepEnergy);
    setDoubleParam(AnalyserHighEnergy, highEnergy);
    setDoubleParam(AnalyserCenterEnergy, centerEnergy);
    setIntegerParam(AnalyserEnergyCount, requestedAxes["X"]["Count"].get<int>());

    setDoubleParam(AnalyserLowSlice, requestedAxes["Y"]["Offset"].get<double>());
    setDoubleParam(AnalyserSliceStep, requestedAxes["Y"]["Delta"].get<double>());
    setDoubleParam(AnalyserHighSlice, requestedAxes["Y"]["Highest"].get<double>());
    setDoubleParam(AnalyserCenterSlice,requestedAxes["Y"]["Center"].get<double>());
    setIntegerParam(AnalyserSliceCount, requestedAxes["Y"]["Count"].get<int>());

    if (requestedAxes.contains("Z")) {
        setDoubleParam(AnalyserLowAngleY, requestedAxes["Z"]["Offset"].get<double>());
        setDoubleParam(AnalyserAngleYStep, requestedAxes["Z"]["Delta"].get<double>());
        setDoubleParam(AnalyserHighAngleY, requestedAxes["Z"]["Highest"].get<double>());
        setDoubleParam(AnalyserCenterAngleY, requestedAxes["Z"]["Center"].get<double>());
        setIntegerParam(AnalyserAngleYCount, requestedAxes["Z"]["Count"].get<int>());
    } else {
        setDoubleParam(AnalyserLowAngleY, 0);
        setDoubleParam(AnalyserAngleYStep, 0);
        setDoubleParam(AnalyserHighAngleY, 0);
        setDoubleParam(AnalyserCenterAngleY, 0);
        setIntegerParam(AnalyserAngleYCount, 0);
    }

    int acquisitionMode;
    getIntegerParam(AnalyserAcquisitionMode, &acquisitionMode);

    const json& acquiredAxes = channelSettings["AcquiredAxes"];

    int numSteps = 1;
    if ((acquisitionMode & 0x01) && acquiredAxes.contains("X"))
        numSteps *= acquiredAxes["X"]["Count"].get<int>();
    if ((acquisitionMode & 0x02) && acquiredAxes.contains("Z"))
        numSteps *= acquiredAxes["Z"]["Count"].get<int>();

    setIntegerParam(AcqNumSteps, numSteps);
    setIntegerParam(AcqNumStepsCounter, 0);
}

void peakAnalyser::setupSpectrumDefinition()
{
    const char *functionName = "setupSpectrumDefinition";

    int detectorSizeX, detectorSizeY, minX, minY, sizeX, sizeY;
    int lensMode, elementSet, passEnergy;
    int acquisitionMode, detectorMode, detectorChannels, detectorSlices;
    double acquireTime;
    int numExposures;
    int energyMode;
    double excitationEnergy, workFunction;
    double lowEnergy, centerEnergy, highEnergy, stepEnergy;
    double lowAngleY, centerAngleY, highAngleY, stepAngleY;
    double centerSlice;

    getIntegerParam(AnalyserAcquisitionMode, &acquisitionMode);

    /* Get detector parameters */
    getIntegerParam(ADMaxSizeX, &detectorSizeX);
    getIntegerParam(ADMaxSizeY, &detectorSizeY);
    getIntegerParam(ADMinX, &minX);
    getIntegerParam(ADMinY, &minY);
    getIntegerParam(ADSizeX, &sizeX);
    getIntegerParam(ADSizeY, &sizeY);
    getIntegerParam(DetectorMode, &detectorMode);
    getIntegerParam(DetectorChannels, &detectorChannels);
    getIntegerParam(DetectorSlices, &detectorSlices);

    /* Get analyser modes */
    getIntegerParam(LensMode, &lensMode);
    getIntegerParam(ElementSet, &elementSet);
    getIntegerParam(PassEnergy, &passEnergy);

    /* Check enum range */
    if (elementSet >= (int)m_Elementsets.size() ||
        lensMode >= (int)m_LensModes.size() ||
        passEnergy >= (int)m_PassEnergies.size() ||
        detectorMode >= (int)m_DetectorModes.size())
        return;

    /* Validate pass energy */
    setPassEnergy(this->clamp(m_PassEnergies[passEnergy], m_LensModes[lensMode].passEnergyRange));
    getIntegerParam(PassEnergy, &passEnergy);

    setStringParam(ElementSetStr, m_Elementsets[elementSet]);
    setStringParam(LensModeStr, m_LensModes[lensMode].name);
    setStringParam(DetectorModeStr, m_DetectorModes[detectorMode]);
    setDoubleParam(PassEnergyValue, m_PassEnergies[passEnergy]);

     /* Get the energy parameters */
    getIntegerParam(EnergyMode, &energyMode);
    getDoubleParam(ExcitationEnergy, &excitationEnergy);
    getDoubleParam(WorkFunction, &workFunction);
    lowEnergy = m_RequestedAxes[AnalyserLowEnergy];
    centerEnergy = m_RequestedAxes[AnalyserCenterEnergy];
    highEnergy = m_RequestedAxes[AnalyserHighEnergy];
    stepEnergy = m_RequestedAxes[AnalyserEnergyStep];
    if (energyMode == 1) { // binding energy mode
        lowEnergy = excitationEnergy - workFunction + lowEnergy;
        highEnergy = excitationEnergy - workFunction + highEnergy;
        centerEnergy = excitationEnergy - workFunction + centerEnergy;
    }

    /* Check value range */
    if (acquisitionMode&0x01 &&
            (centerEnergy <= 0 || lowEnergy <= 0 || highEnergy <= 0)) {
        return;
    }

    /* Get Theta X parameters */
    centerSlice = m_RequestedAxes[AnalyserCenterSlice];

    /* Get Theta Y parameters */
    lowAngleY = m_RequestedAxes[AnalyserLowAngleY];
    centerAngleY = m_RequestedAxes[AnalyserCenterAngleY];
    highAngleY = m_RequestedAxes[AnalyserHighAngleY];
    stepAngleY = m_RequestedAxes[AnalyserAngleYStep];

    /* Validate ThetaY */
    if (m_LensModes[lensMode].hasYDeflection) {
        lowAngleY = this->clamp(lowAngleY, m_LensModes[lensMode].yDeflectionRange);
        highAngleY = this->clamp(highAngleY, m_LensModes[lensMode].yDeflectionRange);
        centerAngleY = this->clamp(centerAngleY, m_LensModes[lensMode].yDeflectionRange);
    }

    /* Get the exposure parameters */
    getDoubleParam(ADAcquireTime, &acquireTime);
    getIntegerParam(ADNumExposures, &numExposures);

    /* Fill SpectrumDefinition */
    m_SpectrumDefinition["ElementSetName"] = m_Elementsets[elementSet];
    m_SpectrumDefinition["LensModeName"] =  m_LensModes[lensMode].name;
    m_SpectrumDefinition["PassEnergy"] = m_PassEnergies[passEnergy];
    m_SpectrumDefinition["FixedAxes"] = json::object();
    m_SpectrumDefinition["SweepAxes"] = json::object();
    m_SpectrumDefinition[m_DetectorModesName] = m_DetectorModes[detectorMode];
    m_SpectrumDefinition["DwellTime"] = acquireTime;

    int centerX = detectorSizeX / 2;
    int centerY = detectorSizeY / 2;
    m_SpectrumDefinition["DetectorAreas"] = {
        {"ROI", {
                    {"LowX", (minX - centerX) / m_PixelDensity},
                    {"LowY", (minY - centerY) / m_PixelDensity},
                    {"HighX",(minX + sizeX - centerX) / m_PixelDensity},
                    {"HighY",(minY + sizeY - centerY) / m_PixelDensity}
        }}
    };

    double epsilon = std::numeric_limits<double>::epsilon();

    if (acquisitionMode & 0x01) // Sweep Energy
        m_SpectrumDefinition["SweepAxes"]["X"] = {
            {"Offset", lowEnergy},
            {"Delta", stepEnergy},
            {"Count", std::ceil(std::abs(highEnergy-lowEnergy-epsilon)/stepEnergy)+1}
        };
    else
        m_SpectrumDefinition["FixedAxes"]["X"] = {
            {"Center", centerEnergy},
            {"Binning", 1.0 * sizeX / detectorChannels}
        };

    m_SpectrumDefinition["FixedAxes"]["Y"] = {
        {"Center", centerSlice},
        {"Binning", 1.0 * sizeY / detectorSlices}
    };

    if (acquisitionMode & 0x02) // Sweep AngleY
        m_SpectrumDefinition["SweepAxes"]["Z"] = {
            {"Offset", lowAngleY},
            {"Delta", stepAngleY},
            {"Count", std::ceil(std::abs(highAngleY-lowAngleY-epsilon)/stepAngleY)+1}
        };
    else
        m_SpectrumDefinition["FixedAxes"]["Z"] = {
            {"Center", centerAngleY},
        };

    /* Call CalculateSpectrumInfo to validate the SpectrumDefinition */
    try {
        const json spectrumInfo = analyser->adjustSpectrumDefinition(m_SpectrumDefinition)["SpectrumInfo"];
        const json channelSettings = spectrumInfo["AnalyserSpectrumChannelSettings"].front();
        /* Update channel settings */
        setRegion(channelSettings);
        /* Estimated acquisition time */
        double eta = spectrumInfo["WallClockTime"].get<double>() * numExposures;
        setDoubleParam(AcqETA, eta);
        setStringParam(AcqETAStr, this->strftime(int(eta)));
    } catch (std::exception& e) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s adjustSpectrumInfo error: %s\n",
                driverName, functionName, e.what());
    }
}

/** Configuration command, called directly or from iocsh */
extern "C" int peakAnalyserConfig(const char *portName,
                                 const char *hostAddress,
                                 int maxBuffers, int maxMemory, int priority, int stackSize)
{
    new peakAnalyser(portName, hostAddress,
                    (maxBuffers < 0) ? 0 : maxBuffers,
                    (maxMemory < 0) ? 0 : maxMemory,
                    priority, stackSize);
    return(asynSuccess);
}

/** Code for iocsh registration */
static const iocshArg peakAnalyserConfigArg0 = {"Port name", iocshArgString};
static const iocshArg peakAnalyserConfigArg1 = {"Peak Analyser server address", iocshArgString};
static const iocshArg peakAnalyserConfigArg2 = {"maxBuffers", iocshArgInt};
static const iocshArg peakAnalyserConfigArg3 = {"maxMemory", iocshArgInt};
static const iocshArg peakAnalyserConfigArg4 = {"priority", iocshArgInt};
static const iocshArg peakAnalyserConfigArg5 = {"stackSize", iocshArgInt};
static const iocshArg * const peakAnalyserConfigArgs[] = {&peakAnalyserConfigArg0,
                                                          &peakAnalyserConfigArg1,
                                                          &peakAnalyserConfigArg2,
                                                          &peakAnalyserConfigArg3,
                                                          &peakAnalyserConfigArg4,
                                                          &peakAnalyserConfigArg5};
static const iocshFuncDef configpeakAnalyser = {"peakAnalyserConfig", 6, peakAnalyserConfigArgs};
static void configpeakAnalyserCallFunc(const iocshArgBuf *args)
{
    peakAnalyserConfig(args[0].sval, args[1].sval,
            args[2].ival, args[3].ival, args[4].ival, args[5].ival);
}


static void peakAnalyserRegister(void)
{
    iocshRegister(&configpeakAnalyser, configpeakAnalyserCallFunc);
}

extern "C" {
epicsExportRegistrar(peakAnalyserRegister);
}
