TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

USR_CXXFLAGS += -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STRICT_
USR_CXXFLAGS += -DJSON_DIAGNOSTICS=1
USR_CXXFLAGS_Linux += -std=c++11

LIBRARY_IOC = peakAnalyser

USR_INCLUDES += -I ../extern

LIB_SRCS += base64.cpp
LIB_SRCS += HTTPCommon.cpp
LIB_SRCS += HTTPClient.cpp
LIB_SRCS += JsonRPCClientHTTP.cpp
LIB_SRCS += JsonRPCClientWS.cpp
LIB_SRCS += peakApi.cpp
LIB_SRCS += peakAnalyser.cpp

DBD += peakAnalyserSupport.dbd

USR_SYS_LIBS_WIN32 += ws2_32

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

