include /ioc/tools/driver.makefile
PROJECT = peakAnalyser
BUILDCLASSES = Linux WIN32
ARCH_FILTER = win% ${EPICS_HOST_ARCH}
EXCLUDE_VERSIONS = 3

USR_CPPFLAGS += -DJSON_USE_IMPLICIT_CONVERSIONS=0
USR_CXXFLAGS += -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STRICT_ -DJSON_DIAGNOSTICS=1
USR_CXXFLAGS_Linux += -std=c++11
USR_INCLUDES += -I ../../peakAnalyserApp/src/extern
USR_SYS_LIBS_WIN32 += ws2_32

SOURCES += ../peakAnalyserApp/src/base64.cpp
SOURCES += ../peakAnalyserApp/src/HTTPCommon.cpp
SOURCES += ../peakAnalyserApp/src/HTTPClient.cpp
SOURCES += ../peakAnalyserApp/src/JsonRPCClientHTTP.cpp
SOURCES += ../peakAnalyserApp/src/JsonRPCClientWS.cpp
SOURCES += ../peakAnalyserApp/src/peakApi.cpp
SOURCES += ../peakAnalyserApp/src/peakAnalyser.cpp

DBDS    += ../peakAnalyserApp/src/peakAnalyserSupport.dbd

TEMPLATES += ../peakAnalyserApp/Db/peakAnalyser.template
TEMPLATES += ../peakAnalyserApp/Db/peakAnalyser_settings.req

QT += ../peakAnalyserApp/op/ui/autoconvert/peakAnalyser.ui
