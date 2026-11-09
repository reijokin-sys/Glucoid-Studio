#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$project_dir" -B "$project_dir/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DSTUDIO_LOCAL_DATA_ROOT="$project_dir/private"
cmake --build "$project_dir/build" --parallel 4
