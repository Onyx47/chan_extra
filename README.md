chan_extra
==========

Updated version of chan_extra driver fo OpenVox G400P/G400 for kernel 3.10+ and Asterisk 11

#Installation instructions

After downloading the latest version of dahdi and uncompressing it, you will need to create a `.version` file in dahdi directory that contains the current version number in order for the installation script to run properly.

Example:

```bash
cd /usr/src/dahdi-linux-complete-2.9.2+2.9.2
echo "2.9.2" > .version
```

After that, follow the official installation instructions for source install.

#Notes / Known issues

On latest Debian the `install.sh` script kept warning me about missing `bison-devel` package. you will have to install the package `libbison-dev` using the package manager yourself. The warning can be safely ignored after that.
