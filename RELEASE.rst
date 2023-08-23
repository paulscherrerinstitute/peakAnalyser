peakAnalyser Releases
=====================

The latest untagged master branch can be obtained at https://github.com/paulscherrerinstitute/peakAnalyser.


Release Notes
=============

3.0.0 (October XX, 2023)
------------------------

* Fix ROI calculation.
* New records to indicate whether Theta X/Y angles are supported by the current lens mode.
* New records to set focal positions.
* New records for the live spectrum.
* enum DetectorMode {ADC, Pulse} renamed to AcquisitionMode {Image, Event}.
* Use proxy URIs for instrument servers when client and server run on different hosts for PEAK 1.3.
* The driver port now has two addresses. Final image update is on addr=0 and live image update on addr=1.
* Requires PEAK 1.3.

2.1.0 (September 18, 2021)
--------------------------

* Improve settings when EnergyMode=Binding.
* Adapt to PEAK 1.0.0.0-alpha.19. Call *UnsubscribeAllForObserver* on startup to remove possible orphan notifications.

2.0.0 (September 2, 2021)
-------------------------

* Add WebSocket JSON-RPC implementation based on `websocketpp <https://github.com/zaphoyd/websocketpp>`_ and `asio <https://think-async.com/Asio>`_. websocketpp 0.8.2 and asio 1.18.2 are checked in by source.

1.1.0 (August 29, 2021)
-----------------------

* Code refactor to separate JSON-RPC client interface.
* Call AdjustSpectrumDefinition to validate spectrum definition.

1.0.0 (August 25, 2021)
-----------------------

* First release.
* HTTP JSON-RPC client.
