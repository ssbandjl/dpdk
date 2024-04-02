# sudo apt-get install python3-pyelftools

meson setup -Dbuildtype=debug -Dexamples=helloworld build | meson configure -Dbuildtype=debug -Dexamples=helloworld
sudo su
cd build
ninja
meson install
ldconfig

# rebuild
sudo ninja clean
