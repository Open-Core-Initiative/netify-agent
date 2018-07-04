Netify Agent
============

Deep-Packet Inspection Server
-----------------------------

The [Netify](https://www.netify.ai/) Agent is a deep-packet inspection server.  The Agent is built on top of [nDPI](http://www.ntop.org/products/deep-packet-inspection/ndpi/) (formerly OpenDPI) to detect network protocols and applications.  These detections can be saved locally, served over a UNIX or TCP socket, and/or "pushed" (via HTTP POSTs) to a remote third-party server.  Flow metadata, network statistics, and detection classifications are stored using JSON encoding.

Optionally, the Netify Agent can be coupled with a [Netify Cloud](https://www.netify.ai/) subscription for further cloud processing, historical storage, machine-learning analysis, event notifications, device detection/identification, along with the option (on supported platforms) to take an active role in policing/bandwidth-shaping specific network protocols and applications.

Build Requirements
------------------

Netify requires the following third-party packages:
- libcurl
- libjson-c
- libmnl
- libnetfilter-conntrack
- libpcap
- zlib

Optional:
- libtcmalloc (gperftools)

Runtime Requirements
--------------------

Ensure that the nfnetlink and nf_conntrack_netlink kernel modules are loaded.

Download Source
---------------

When cloning the source tree, ensure you use `--recursive` to include all
sub-modules.

Download Packages
-----------------

Currently you can download binary packages for the following OS distributions:
- [ClearOS](https://www.clearos.com/products/purchase/clearos-marketplace-apps#cloud)
- [CentOS](http://software.opensuse.org/download.html?project=home%3Aegloo&package=netifyd)
- [Debian](http://software.opensuse.org/download.html?project=home%3Aegloo&package=netifyd)
- [Fedora](http://software.opensuse.org/download.html?project=home%3Aegloo&package=netifyd)
- [Ubuntu](http://software.opensuse.org/download.html?project=home%3Aegloo&package=netifyd)

Configuring/Building From Source
--------------------------------

Read the appropriate documentation in the doc directory, prefixed with: BUILD-*

Generally the process is:
```
# ./autogen.sh
# ./configure
# make
```

