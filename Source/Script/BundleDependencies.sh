#!/usr/bin/env bash
#
# ./BundleDependencies.sh "./Bin/Release/RMG.exe" "./Bin/Release/"
#
set -e

exe="$1"
bin_dir="$2"
path="/$MSYSTEM/bin"

function copyForOBJ() {
    local deps=`objdump.exe -p $1 | grep 'DLL Name:' | sed -e "s/\t*DLL Name: //g"`
    while read -r line
	do
        findAndCopyDLL $line
    done <<< "$deps"
}

function findAndCopyDLL() {
    local file="$path/$1"
    
	if [ -f $file ] && [ ! -f "$bin_dir/$1" ]
	then
		cp "$file" "$bin_dir"
        copyForOBJ $file
		return 0
	fi

    return 0
}


for file in "$bin_dir"/*.exe "$bin_dir/Core"/*.dll "$bin_dir/Plugin"/*/*.dll
do
	echo "=> Copying dependencies for $file"
	copyForOBJ "$file"
done

windeployqt6 --exclude-plugins qpdf,qwebp,qgif,qtga,qtuiotouchplugin,qglib,qtiff,qmng,qwbmp \
				--no-translations "$exe"

# needed by Qt at runtime (optional TLS/image plugins).
# Match by wildcard so a new OpenSSL / libjpeg major version in MSYS2 does not
# break the bundle step; missing optional libraries only produce a warning.
function copyOptionalDLL() {
    local found=0
    local f
    for f in "$path"/$1
    do
        if [ -f "$f" ]
        then
            cp "$f" "$bin_dir/"
            echo "=> Bundled $(basename "$f")"
            found=1
        fi
    done
    if [ "$found" -eq 0 ]
    then
        echo "WARNING: no library matching $1 found in $path, skipping"
    fi
    return 0
}

copyOptionalDLL "libcrypto-*.dll"
copyOptionalDLL "libssl-*.dll"
copyOptionalDLL "libjpeg-*.dll"

# remove *.a files
find "$bin_dir/" -name '*.a' -delete

exit 0
