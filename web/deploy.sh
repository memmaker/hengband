#!/bin/sh
# Upload web/dist to https://ruzzoli.de/roguelikes/hengband/ (only from pushed commits)
cd "$(dirname "$0")" && git fetch -q && [ -z "$(git status --porcelain)" ] && [ "$(git rev-parse @)" = "$(git rev-parse @{u})" ] || { echo "commit + push first"; exit 1; }
set -e
ssh ruzzoli.de 'sudo mkdir -p /var/www/ruzzoli.de/roguelikes/hengband && sudo chown -R felix:www-data /var/www/ruzzoli.de/roguelikes/hengband'
rsync -rtz --delete dist/ ruzzoli.de:/var/www/ruzzoli.de/roguelikes/hengband/
