# SMART1500LCDT (09AE:3016) Brute-Force Poller


## About this mess

This is a tool for querying the status of Tripp-Lite SMART1500LCDT UPSes with the USB vendor/product ID `09AE:3016`.

If you, like me, have made the mistake of purchasing one of these abominations, you're likely already aware that while
the  UPS hardware itself is just fine, its USB implementation is a steaming pile of horseshit and it's impossible to
use it under  Linux with any known UPS monitoring software, including Tripp-Lite's own discontinued PowerAlert Local
software for Linux.

For always-on devices, a UPS without monitoring is little better than no UPS at all -- it just allows your device to run
for an extra 45 minutes or so before the power is cut and all of your unsaved data is lost.

This tool attempts to make some degree of monitoring possible. It's ugly. It's inelegant. I'm ashamed to have written
it. You should be ashamed of using it. Yet, here it is.


## USB Host Requirements

This code is the product of an evening of experimentation with my own SMART1500LCDT UPSes in an attempt to successfully
fetch any possible status information from them over USB. Your results may differ from mine.

The host device (that is, the computer to which you're connecting your UPS' USB cable) does matter and *will* impact your
results because the SMART1500LCDT's USB protocol  implementation is pure, hot garbage, and it interacts differently (and
with different failure modes) with different host USB devices and protocol implementations. In particular, anecdotal
evidence seems to indicate total incompatibility with USB3 ports, and problems up to and including total incompatibility
with newer USB host hardware in general.

I was unable to communicate via USB with the SMART1500LCDT when connected to any modern equipment in my office. I did,
however, have some "success" (if you can call it that) with the oldschool Raspberry Pi v1 Model B. (In my case, I had a
stack of old RPiv1s that I had repurposed as IPKVM/remote power management devices for lab servers, so it was convenient
to also use them for UPS monitoring.)

All of my testing was peformed with my two SMART1500LCDTs connected to identically configured RPIv1s running Debian
Buster with kernel 5.10.17+.


## Observed behavior

After connecting the UPS to a host device, the UPS will initially return `EPIPE` errors in response to all `HIDIOCGFEATURE`
requests with a given report ID. Continuing to request the same report ID yields no change in behavior.

Hammering the UPS with requests for a variety of different report IDs, however, causes some kind of magic in which the
UPS begins sputtering occasional valid responses (amongst the `EPIPE` errors), and eventually begins returning good data
with only the occasional `EPIPE`. (No amount of testing on my part revealed a consistent sequence of events that
reliably "kickstarted" the UPS into this state, though it's possible that one exists.)

Once in this magic state, subsequent `HIDIOCGFEATURE` requests (even on subsequent invocations of this program)
continue yielding valid results with only sporadic `EPIPE` errors.

This continues until the USB driver realizes that the SMART1500LCDT's USB protocol implementation is bullshit and
resets the device, which happens periodically. Then, more often than not, the `/dev/hidrawx` device disappears entirely,
and you have to power cycle the USB port using `uhubctl`. At that point, you're back at square and the cycle repeats.

Thus, this steaming mess of code was born, relying heavily upon retry loops and device resets and hand-waving and voodoo
magic.


## Unsupported features

Strangely, the "PresentStatus" report ID (#50, used to retrieve the "AC Present" status) is completely unsupported on my
two UPSes. Even when all other report IDs are returning valid data, "Present Status" always returns `EPIPE`. So it's not
possible to directly access information about whether the UPS is on battery or mains.

The "Battery PresentStatus" report ID (#35) always seems to return 0x08 (a "reserved" bit) and never seems to set the
"charging" or "discharging" bits, so it's not possible to use the charging/discharging state to infer the battery/mains
status either.

My "solution" has been to track the battery charge level over a period of time and if it's trending downward, we can
infer that we're on battery; upward or unchanging likely indicates mains power.


## Building

Ensure that GCC, libusb-1.0 and uhubctl are installed, eg:
```
apt-get install build-essential libusb-1.0-0-dev uhubctl
```

Then run the build script:
```
./build.sh
```


## Usage

Run the included script to poll the UPS:
```
./poll-ups.sh /path/to/outputfile.json
```

This may take up to 2 minutes to run depending on how finnicky the UPS is being, but typically it will complete within
a few seconds after creating `/path/to/outputfile.json` containing the UPS status information.

If the exit code is nonzero, there's a (more serious than usual) problem and you'll likely need to physically disconnect
and reconnect the USB cable.

I use a cron job that runs `poll-ups.sh` every 5 minutes, parses the output file, and takes action only if two
subsequent invocations return similar results. So far it's been surprisingly reliable (particularly given how ugly a
hack it is) and hasn't required any hands-on intervention to keep it working, though I certainly wouldn't rely on it
for important systems.


## License

Licensed under the GNU GPL v2. See the file `LICENSE` for details. Pay special attention to the disclaimer of
warranty -- you'd be crazy to run this in production. :)
