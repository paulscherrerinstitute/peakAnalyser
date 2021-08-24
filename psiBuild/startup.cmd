set_requestfile_path("$($(MODULE)_DIR)db")

peakAnalyserConfig("$(PORT=PA$(N=1))","$(PEAK_ADDRESS=http://127.0.0.1:8080)", "$(LOCAL_PORT=52320)")
dbLoadRecords("peakAnalyser.template","P=$(PREFIX),R=$(RECORD=cam1:),PORT=$(PORT=PA$(N=1)),ADDR=$(ADDR=0),TIMEOUT=1")

set_pass0_restoreFile("peakAnalyser_settings$(N=1).sav")
set_pass1_restoreFile("peakAnalyser_settings$(N=1).sav")

afterInit create_monitor_set,"peakAnalyser_settings.req",30,"P=$(PREFIX),R=$(RECORD=cam1:)","peakAnalyser_settings$(N=1).sav"
