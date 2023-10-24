#!/bin/sh

cd $DEB_PATH

cat > vcmi.yml << EOF
app: vcmi
 
ingredients:
  dist: jammy
  sources:
    - deb http://us.archive.ubuntu.com/ubuntu/ jammy main universe
  debs: 
    - $DEB_FILE
 
script:
  - mv usr/lib/x86_64-linux-gnu/vcmi/* usr/lib/x86_64-linux-gnu/
  - mv usr/games/* usr/share/vcmi/* -t usr/bin
  - mv usr/lib/x86_64-linux-gnu/AI usr/bin/AI
  - sed -i -e 's#/usr#././#g' usr/bin/vcmiclient usr/bin/vcmilauncher usr/bin/vcmiserver usr/bin/vcmieditor
  - cp usr/share/applications/vcmilauncher.desktop vcmi.desktop
  - cp usr/share/icons/hicolor/scalable/apps/vcmiclient.svg vcmiclient.svg
EOF

wget -c $(wget -q https://api.github.com/repos/AppImageCommunity/pkg2appimage/releases -O - | grep "pkg2appimage-.*-x86_64.AppImage" | grep browser_download_url | head -n 1 | cut -d '"' -f 4)
chmod +x ./pkg2appimage-*.AppImage
./pkg2appimage-*.AppImage vcmi.yml
