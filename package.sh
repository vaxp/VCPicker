#!/bin/bash

set -e

# Configuration
PACKAGE_NAME="vcpicker"
VERSION="0.1.0"
ARCH="amd64"
MAINTAINER="Vaxp"
DESCRIPTION="A color picker for Linux"

# Directories
BUILD_DIR="build"
PKG_DIR="${PACKAGE_NAME}_${VERSION}_${ARCH}"

echo "Cleaning up old package directories..."
rm -rf "$PKG_DIR"
rm -f "${PKG_DIR}.deb"

echo "Setting up package directory structure..."
mkdir -p "$PKG_DIR/DEBIAN"

echo "Creating control file..."
cat <<EOF > "$PKG_DIR/DEBIAN/control"
Package: $PACKAGE_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER <maintainer@example.com>
Description: $DESCRIPTION
Depends: libgtk-4-1, libglib2.0-0, libx11-6, libwayland-client0, libwayland-cursor0
EOF

echo "Building the project..."
if [ ! -d "$BUILD_DIR" ]; then
    meson setup "$BUILD_DIR" --prefix=/usr --buildtype=release
else
    meson configure "$BUILD_DIR" --prefix=/usr --buildtype=release
fi
ninja -C "$BUILD_DIR"

echo "Installing to package directory..."
DESTDIR="$(pwd)/$PKG_DIR" ninja -C "$BUILD_DIR" install

echo "Building Debian package..."
dpkg-deb --build "$PKG_DIR"

echo "Cleaning up..."
rm -rf "$PKG_DIR"

echo "Done! Package ${PKG_DIR}.deb created."
