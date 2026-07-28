#!/bin/bash
./bannertool makebanner -i raptor3dsbanner.png -a raptor3dsbanner.wav -o banner.bnr
./bannertool makesmdh -s "Raptor3DS" -l "Raptor Call of the Shadows Enhanced for 3DS" -p "RetroGamer02" -i raptor3ds.png  -o icon.icn
./makerom -f cia -o RAPTOR-ENHANCED-3DS.cia -DAPP_ENCRYPTED=false -rsf Raptor-3DS.rsf -major 1 -minor 0 -micro 0 -romfs romfs -target t -exefslogo -elf raptor-consoles-enhanced.elf -icon icon.icn -banner banner.bnr
./makerom -f cci -o RAPTOR-ENHANCED-3DS.3ds -DAPP_ENCRYPTED=true -rsf Raptor-3DS.rsf -major 1 -minor 0 -micro 0 -romfs romfs -target t -exefslogo -elf raptor-consoles-enhanced.elf -icon icon.icn -banner banner.bnr
echo "Finished! 3DS and CIA have been built!"