# VCPKG 
packages=("tar" "unzip" "g++" "cmake")

# openssl
packages+=("perl-FindBin" "perl-File-Compare")

# stencil flex bison
packages+=("flex" "bison")

# coin, glew : opengl 
packages+=("mesa-libGLU-devel")

# fontconfig : gperf, autoconf autopoint
packages+=("gperf" "autoconf" "gettext-devel" "automake" "libtool")

# freeglut : opengl, glu, libx11, xrandr, xi, xxf86vm
packages+=("mesa-libGLU-devel" "libXi-devel" "libXrandr-devel")

# libusb
packages+=("systemd-devel" "autoconf" "automake" "libtool")


dnf install ${packages[@]}
