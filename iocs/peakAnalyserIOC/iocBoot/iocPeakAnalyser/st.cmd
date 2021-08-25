< envPaths

errlogInit(20000)

dbLoadDatabase("$(TOP)/dbd/peakAnalyserApp.dbd")
peakAnalyserApp_registerRecordDeviceDriver(pdbbase) 

# Prefix for all records
epicsEnvSet("PREFIX", "13PA1:")
# The port name for the detector
epicsEnvSet("PORT",   "PA1")
# The queue size for all plugins
epicsEnvSet("QSIZE",  "20")
# The maximum image width; used to set the maximum size for row profiles in the NDPluginStats plugin
epicsEnvSet("XSIZE",  "5000")
# The maximum image height; used to set the maximum size for column profiles in the NDPluginStats plugin
epicsEnvSet("YSIZE",  "1024")
# The maximum number of time series points in the NDPluginStats plugin
epicsEnvSet("NCHANS", "2048")
# The maximum number of frames buffered in the NDPluginCircularBuff plugin
epicsEnvSet("CBUFFS", "500")
# The maximum number of threads for plugins which can run in multiple threads
epicsEnvSet("MAX_THREADS", "8")
# The search path for database files
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")

asynSetMinTimerPeriod(0.001)

# The EPICS environment variable EPICS_CA_MAX_ARRAY_BYTES needs to be set to a value at least as large
# as the largest image that the standard arrays plugin will send.
# That value is $(XSIZE) * $(YSIZE) * sizeof(FTVL data type) for the FTVL used when loading the NDStdArrays.template file.
# The variable can be set in the environment before running the IOC or it can be set here.
# It is often convenient to set it in the environment outside the IOC to the largest array any client 
# or server will need.  For example 10000000 (ten million) bytes may be enough.
# If it is set here then remember to also set it outside the IOC for any CA clients that need to access the waveform record.  
# Do not set EPICS_CA_MAX_ARRAY_BYTES to a value much larger than that required, because EPICS Channel Access actually
# allocates arrays of this size every time it needs a buffer larger than 16K.
# Uncomment the following line to set it in the IOC.
#epicsEnvSet("EPICS_CA_MAX_ARRAY_BYTES", "10000000")

# Create a peakAnalyser driver
# peakAnalyserConfig(const char *portName, const char *hostAddress, int localPort,
#                   int maxBuffers, int maxMemory, int priority, int stackSize)
peakAnalyserConfig("$(PORT)", "http://127.0.0.1:8080", 53230, 0, 0)

dbLoadRecords("$(PEAK_ANALYSER)/db/peakAnalyser.template","P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")

# Create a standard arrays plugin, set it to get data from peakAnalyser driver.
NDStdArraysConfigure("Image1", 20, 0, "$(PORT)", 0, 0, 0, 0, 0, 5)

# This creates a waveform large enough for 5000x200x10 arrays of 32-bit float (5000 in energy, 200 in theta X and 10 in theta Y).
dbLoadRecords("NDStdArrays.template", "P=$(PREFIX),R=image1:,PORT=Image1,ADDR=0,TIMEOUT=1,NDARRAY_PORT=$(PORT),TYPE=Float32,FTVL=FLOAT,NELEMENTS=10000000")

# Load all other plugins using commonPlugins.cmd
< $(ADCORE)/iocBoot/commonPlugins.cmd

set_requestfile_path("$(PEAK_ANALYSER)/peakAnalyserApp/Db")

iocInit()

# save things every thirty seconds
create_monitor_set("auto_settings.req", 30, "P=$(PREFIX)")
