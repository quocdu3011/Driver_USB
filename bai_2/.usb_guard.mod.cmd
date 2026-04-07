savedcmd_usb_guard.mod := printf '%s\n'   usb_guard.o | awk '!x[$$0]++ { print("./"$$0) }' > usb_guard.mod
