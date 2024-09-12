# apt-get install -y python3-pyelftools python-pyelftools

meson setup -Dbuildtype=debug -Dexamples=all build
#meson setup -Dbuildtype=debug -Dexamples=vdpa build

meson setup -Dbuildtype=debug -Dexamples=helloworld build | meson configure -Dbuildtype=debug -Dexamples=helloworld
sudo su
cd build
ninja
meson install
ldconfig

# rebuild
sudo ninja clean
