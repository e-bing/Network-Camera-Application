mkdir -p build && cd build
cmake ..
make
./test_eptz_crop
./network-camera