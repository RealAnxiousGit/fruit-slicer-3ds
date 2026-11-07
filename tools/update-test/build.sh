#!/bin/bash
# Builds the update test app twice: version 1 (install this) and version 2 (copy to sd:/updtest/).
# Run in WSL on an ext4 copy. Output goes to ./out.
set -e
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$HOME/fs3ds-tools/mr:$HOME/fs3ds-tools/bt/bannertool-1.2.3-linux:$PATH
cd "$(dirname "$0")"
mkdir -p out romfs
# HTTPS needs a list of trusted certificate roots; take the build machine's Mozilla bundle.
cp /etc/ssl/certs/ca-certificates.crt romfs/cacert.pem
G=../../meta
bannertool makebanner -i $G/banner.png -a $G/banner.wav -o out/banner.bnr >/dev/null
for V in 1 2; do
  bannertool makesmdh -s "FS Update Test v$V" -l "Fruit Slicer self-update test" -p "RealAnxiousGit" -i meta/icon.png -o out/icon$V.icn >/dev/null
  arm-none-eabi-gcc -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -Wall -mword-relocations \
    -ffunction-sections -fdata-sections -D__3DS__ -DTEST_VERSION=$V \
    -I$DEVKITPRO/libctru/include -I$DEVKITPRO/portlibs/3ds/include source/main.c \
    -specs=3dsx.specs -Wl,--gc-sections -L$DEVKITPRO/portlibs/3ds/lib -L$DEVKITPRO/libctru/lib \
    -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm -o out/updtest$V.elf
  makerom -f cia -o out/updtest_v$V.cia -target t -exefslogo -elf out/updtest$V.elf -rsf meta/cia.rsf \
    -icon out/icon$V.icn -banner out/banner.bnr -major $V -minor 0 -micro 0
done
ls -la out/*.cia
