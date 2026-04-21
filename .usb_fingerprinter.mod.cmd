savedcmd_usb_fingerprinter.mod := printf '%s\n'   usb_fingerprinter.o | awk '!x[$$0]++ { print("./"$$0) }' > usb_fingerprinter.mod
