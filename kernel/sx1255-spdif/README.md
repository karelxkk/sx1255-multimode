# SX1255 SPDIF 125 kSa/s DKMS module

The Raspberry Pi `spdif-dir` and `spdif-dit` stub codecs accept a fixed set of
sample rates and reject the SX1255 HAT rate of 125000 samples/s. These two
out-of-tree modules expose a continuous 8000-768000 samples/s range.

Install against the running Raspberry Pi kernel and headers:

```sh
sudo apt install dkms linux-headers-rpi-v8
sudo install -d -m 0755 /usr/src/sx1255-spdif-1.0
sudo install -m 0644 Makefile dkms.conf spdif_receiver.c spdif_transmitter.c \
  /usr/src/sx1255-spdif-1.0/
sudo dkms add -m sx1255-spdif -v 1.0
sudo dkms build -m sx1255-spdif -v 1.0 -k "$(uname -r)"
sudo dkms install -m sx1255-spdif -v 1.0 -k "$(uname -r)" --force
```

DKMS rebuilds the modules automatically for later Raspberry Pi kernel updates.
