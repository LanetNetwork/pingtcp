pingtcp
=======

Description
-----------

Small utility to measure TCP handshake time (torify-friendly).

Compiling
---------

### Prerequisites

* make (tested with GNU Make 3.82)
* gcc (tested with 4.8.2)
* cmake (tested with 2.8.11)

### Compiling

Create `build` folder, chdir to it and type:

`cmake ..`

Then type `make`.

Usage
-----

Typical usage:

`pingtcp kernel.org 443`

The following arguments are supported:

* -c &lt;attempts&gt; (optional, defaults to infinity) specifies handshake attempts count;
* -i &lt;milliseconds&gt; (optional, defaults to 1 sec) specifies interval between attempts;
* -t &lt;milliseconds&gt; (optional, defaults to 1 sec) specifies TCP connection timeout;
* --tor (optional) uses libtorsocks to connect over TOR network;
* -6 (optional) use IPv6 (seems to be incompatible with TOR).

Distribution and Contribution
-----------------------------

Distributed under terms and conditions of GNU GPL v3 (only).

The following people are involved in development:

* Oleksandr Natalenko &lt;o.natalenko@lanet.ua&gt;

Mail them any suggestions, bugreports and comments.
