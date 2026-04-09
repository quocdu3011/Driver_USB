savedcmd_secure_aes.mod := printf '%s\n'   secure_aes_driver.o aes_core.o pkcs7.o | awk '!x[$$0]++ { print("./"$$0) }' > secure_aes.mod
