#Build V8 on ARM64:

set -x

# Requirements
sudo apt-get update && sudo apt-get install -y ninja-build clang python-pip pkg-config

#Download V8
git clone https://github.com/sqreen/PyMiniRacer
cd PyMiniRacer
sed -i -e "s/x64.release/arm64.release/" py_mini_racer/extension/v8_build.py
python setup.py build_v8

# ARM specific modifications :(

sed -i -e "s/target_cpu=\"x64\" v8_target_cpu=\"arm64/target_cpu=\"arm64\" v8_target_cpu=\"arm64/" py_mini_racer/extension/v8/v8/infra/mb/mb_config.pyl
sed -i -e "s/    ensure_v8_src()//" py_mini_racer/extension/v8_build.py
sed -i -e "s/x64.release/arm64.release/" setup.py
cd ..

#Get ARM64 binaries
wget http://releases.llvm.org/7.0.1/clang+llvm-7.0.1-aarch64-linux-gnu.tar.xz
tar xf clang+llvm-7.0.1-aarch64-linux-gnu.tar.xz

#Get and build the plugin
wget https://chromium.googlesource.com/chromium/src/+archive/lkgr/tools/clang/plugins.tar.gz
mkdir plugin
cd plugin
tar xf ../plugins.tar.gz

clang++ *.cpp -c -I ../clang+llvm-7.0.1-aarch64-linux-gnu/include/ -fPIC -Wall -std=c++14 -fno-rtti -fno-omit-frame-pointer
clang -shared *.o -o libFindBadConstructs.so
cd ..

# Move the plugin to clang, and link V8 to it
cp plugin/libFindBadConstructs.so clang+llvm-7.0.1-aarch64-linux-gnu/lib/
rm -r PyMiniRacer/py_mini_racer/extension/v8/v8/third_party/llvm-build/Release+Asserts/
mv clang+llvm-7.0.1-aarch64-linux-gnu/ PyMiniRacer/py_mini_racer/extension/v8/v8/third_party/llvm-build/Release+Asserts

# Build an ARM64 binary of gn
git clone https://gn.googlesource.com/gn
cd gn

sed -i -e "s/-Wl,--icf=all//" build/gen.py
python build/gen.py
ninja -C out
cd ..

# Update the embedded gn binary
rm PyMiniRacer/py_mini_racer/extension/v8/v8/buildtools/linux64/gn
cp gn/out/gn PyMiniRacer/py_mini_racer/extension/v8/v8/buildtools/linux64/gn

# Resume build
cd PyMiniRacer
python setup.py build_v8
cd py_mini_racer/extension/v8/v8
ninja -vv -C out.gn/arm64.release -j 16 v8_monolith

#Collect artifacts

mkdir libv8_arm64
cp out.gn/arm64.release/obj/*.a libv8_arm64/
tar czf libv8_arm64.tar.gz libv8_arm64/
mv libv8_arm64.tar.gz ../../../../../

cd ../../../../
python setup.py bdist_wheel