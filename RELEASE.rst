peakAnalyser Releases
=====================

The latest untagged master branch can be obtained at https://github.com/paulscherrerinstitute/peakAnalyser.


Release Notes
=============

2.2.0 (October XX, 2021)
------------------------

* New records to indicate whether Theta X/Y angles are supported by the current lens mode.
* Fix ROI calculation in PEAK 1.0.0.0-alpha.19.

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
