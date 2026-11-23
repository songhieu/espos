#!/bin/sh
# Data for sim/scripts/assist-remove.txt (docs/app-slots.md §16.1): a copy of sim/data-calls (PawBoop and its dogs on
# the card, the "Gâu" listener, the wake word on) with the store catalog, PawBoop 2.0.4, Bao and Momo in net/, Momo
# marked paid and its signed links (pkg-link) answered. Then:
#   SIM_DATA=sim/data-p4 sim/build/espos-sim sim/scripts/assist-remove.txt OUT
set -e
cd "$(dirname "$0")/../.."
rm -rf sim/data-p4
cp -R sim/data-calls sim/data-p4
cp build-pkgs/espos/s3/catalog.json sim/data-p4/net/_storage_bb_149_apps-s3_catalog.json
for f in vn.pawboop.pet-2.0.4-core.espk vn.pawboop.pet-2.0.4-game.espk vn.pawboop.pet-2.0.4.jpg \
         vn.pawboop.dog.bao-1.0.4-core.espk vn.pawboop.dog.bao-1.0.4-game.espk vn.pawboop.dog.bao-1.0.4.jpg; do
  cp build-pkgs/espos/s3/$f sim/data-p4/net/_storage_bb_149_apps-s3_$f
done
for f in vn.pawboop.dog.momo-1.0.4-core.espk vn.pawboop.dog.momo-1.0.4-game.espk vn.pawboop.dog.momo-1.0.4.jpg; do
  cp build-pkgs/espos/s3/$f sim/data-p4/net/_storage_bb_149_apps-s3_$f
done
# Momo marked paid (as the live catalog's marks do), so a purchase of it can be tested
python3 - <<'PY'
import json
p='sim/data-p4/net/_storage_bb_149_apps-s3_catalog.json'
c=json.load(open(p))
for e in c['packages']:
    if e['id']=='vn.pawboop.dog.momo': e['paid']=True
json.dump(c,open(p,'w'))
PY
# The server's signed link for the bought dog (GET /v1/device/pkg-link: the entitlement is the account's)
echo '{"url":"https://api.espos.dev/storage/bb/149/apps-s3/vn.pawboop.dog.momo-1.0.4-core.espk"}' > "sim/data-p4/net/_v1_device_pkg-link_id_vn.pawboop.dog.momo&part_core"
echo '{"url":"https://api.espos.dev/storage/bb/149/apps-s3/vn.pawboop.dog.momo-1.0.4-game.espk"}' > "sim/data-p4/net/_v1_device_pkg-link_id_vn.pawboop.dog.momo&part_game"
