readme todo:

- finish 500k indexer stats
- app download links
- test data download link

About cbird
=========================
cbird is a command-line program for managing a media collection, with focus on Content-Based Image Retrieval (Computer Vision) methods for de-duplication.

The main features are:

- Command-line interface
- Threaded C++ architecture, helps with large collections
- Portable, local, per-collection database 
- MD5 hashing for duplicates, bitrot detection
- Perceptual search for images and video
- Renaming and sorting tools
- Clean GUI for viewing results
- Comparative analysis tools
- Zip file support

Use Cases
=========================

- Find exact duplicates (using file checksums)
- Find modified duplicates
	- General transforms: compression, scaling, rotation, cropping
	- Image edits: blur, sharpen, noise, color-grade
	- Video edits: clipping, fps change, letter boxing
- Evaluate duplicates
	- Compare attributes (resolution, file size, compression ratio)
	- Align and flip between matching pairs
	- Zoom-in to examine details
	- False-color visualization of differences
	- No-reference quality metric
	- Jpeg quality setting estimate
	- View videos side-by-side
- General management
	- Sort/rename based on similarity
	- Rename using regular expressions

Format Support
=========================
Qt is used for images and FFmpeg is used for video. The formats available will depend on the configuration of Qt and FFmpeg.

`cbird -about` lists the image and video extensions. Note that video extensions are not checked against FFmpeg at runtime, and codecs are not listed.

Additionally, zip files are supported for images.

To get the most formats you will need to compile FFmpeg and Qt5 with the necessary options.

License
=========================
cbird is free software under the GPL v2. See COPYING file for details.

Installing
=========================

#### Linux AppImage 64-bit:
- Download:
- Chmod
- foo.AppImage -install
- Required packages: trash-cli
- Optional packages: ocenaudio

#### Windows 7+ 64-bit
- Download: TODO
- Unzip the distribution file and run the program
- Install helpers (optional): vlc

Running
========================

#### Get Help

`cbird -help | less`

#### Index the files in `<path>`

`cbird -use <path> -update`

#### Index files in cwd

`cbird -update`

#### Find exact duplicates

`cbird -use <path> -dups -show`

#### Find near duplicates, medium threshold

`cbird -use <path> -similar -show`

#### Find near duplicates, lowest threshold

`cbird -use <path> -p.dht 1 -similar -show`

Using the GUI
=====================
This is lacking documentation at the moment. But for now...

- The GUI is displayed with `-show` if there is a selection or results.
- GUI windows have a context menu (right click) with all actions and shortcuts.
- The two deletion actions ("Delete" and "Replace") use the trash/recycler by default. There is
 no way to permanently delete files (not even with `-nuke` or `-dup-nuke` options)

Environment Variables
======================
There are a few for power users.

- `CBIRD_SETTINGS_FILE` overrides the path to the global settings file
- `CBIRD_TRASH_DIR`overrides the path to trash folder, do not use the system trash bin

Search Algorithms
====================

#### Discrete Cosine Transform (DCT) Hash (`-p.alg 0`)
Stores one 64-bit hash per image, similar to pHash. Very fast and good for rescaled images.

#### DCT Features `-p.alg 1`
Stores DCT hashes of regions around ORB features, up to 400 per image. Good for heavily cropped images, much faster than ORB features.

#### OpenCV Features `-p.alg 2`
Stores up to 400 Oriented Rotated Brief (ORB) 256-bit features per image and searches using FLANN-based matcher. Good for rotated, cropped but slow.

#### Color Histogram `-p.alg 3`
Stores a quantized histogram of up to 32 colors (256-byte) per image. Sometimes works when all else fails. This is the only algorithm that finds reflected images, others require `-p.refl` which is too slow except with `-similar-to`

#### DCT Video Index `-p.alg 4`
Stores DCT hashes of ()up to 64k) video frames, with temporal compression of similar hashes. Frames are pre-processed to remove letterboxing.

#### Template Matcher `-p.tm 1`
Not a technically a search algorithm, but helps to refine results. Uses up to 1000 ORB features to find an affine transform between two images, and uses DCT hash of the mapped region to confirm. Since it requires decompressing the source/destination image it is extremely slow. It can help to reduce the maximum number of matches per image with `-p.mm #`

How it Performs
====================

### Indexing

Indexing happens when `-update` is used. It can take a while the first time, however subsequent updates only consider changes.

Algorithms you do not use can be disabled to speed up indexing or save space. If you have large images, you may as well enable all algorithms because jpeg decompression dominates.

#### Table 1: Indexing 1000 6000px images, 8 GB, SSD

Arguments | Note | Time (seconds)
--------------------|----------------|------ 
-update             | all enabled    | 46
-i.algos 0  -update | md5 only       | 2
-i.algos 1  -update | +dct           | 41
-i.algos 3  -update | +dct features  | 44
-i.algos 7  -update | +cv features   | 44
-i.algos 15 -update | +color hist    | 46

### Searching

Search speed varies with algorithm. The OpenCV search tree is quite slow compared to others. It is better suited for `similar-to` to search a subset.

#### Table 2: Searching 1000 images

Arguments | Note | Time (ms)
------------------|---------------|------ 
-similar          | dct           | 54
-p.alg 1 -similar | dct features  | 200
-p.alg 2 -similar | cv features   | 9000
-p.alg 3 -similar | colors        | 450

### Large Datasets

Indexing large sets benefits from disabling algorithms.

#### Table 3: Indexing 500k 400px images in 100 zip files, 39GB, NAS

Arguments | Note | Rate (Img/s) | Time (minutes)
--------------------|----------------|-----|---------- 
-i.algos 0 -update  | md5 only       | 861 |  9:41
-i.algos 1 -update  | +dct           | 683 | 12:11
-i.algos 3 -update  | +dct features  | 377 | 22:04
-i.algos 7 -update  | +cv features   | --  | 23:56
-i.algos 15 -update | +colors        | --  |

For N^2 search (`-similar`) only DCT hash is practical, and it degrades exponentially as the threshold increases.

#### Table 4: Searching 500k images:

Arguments | Note | Time (s)
-----|------|------ 
-p.dht 1 -similar  | dct, threshold 1  | 5.5
-p.dht 2 -similar  | dct, threshold 2  | 5.6
-p.dht 3 -similar  | dct, threshold 3  | 5.9
-p.dht 4 -similar  | dct, threshold 4  | 7.1
-p.dht 5 -similar  | dct, threshold 5  | 8.9

For K*N (K needle images, N haystack images) the slower algorithms can be practical even for large datasets. For a quick test we can select and search for the first 10 items:

`cbird -p.alg 1 -select-sql "select * from media limit 10"  -similar-to @`

#### Table 5: Searching for 10 images in 500k dataset:

Arguments | Note | Time (s)
-----|------|------ 
-p.alg 0 -p.dht 2  | dct, threshold 2           | 1.3
-p.alg 1 -p.dht 7  | dct-features, threshold 7  | 1.5
-p.alg 2           | cv-features                | 84.4**
-p.alg 3           | colors                     | --

**OpenCV search tree is not cached on disk, slow to start

Wish List
=========================

### Command-Line
- "-require" to abort if there is no index present, or "-create" to enable creation
- "-use" special ****to find root index in the parent (tree)
- symbolic names for enumerations (e.g. "v" == video)
- disable/reduce logging (-verbose, -quiet etc)
- display current (applicable?) parameter values (-params)
- presets for multiple parameters
- move/link files rather than delete
- prune groups, keeping/ignoring needle

### Indexing
- store date-modified,size for better updating
- also store container md5 (.zip) for faster updating/verifying
- >64k frames per video
- capture more common errors in indexer
  - unsupported ICC profile
  - colordescriptor on grayscale image
- console progress bar
- error-log to file
- removing items breaks symlinks to that item... ideally also track symlinks and update them

### Search	
- csv output for other tools, gui wrappers
- sort groups with closest matches first

### GUI
- barebones index/search gui
- open archive (shift+E ?)
- cv min/max filter 8-bit indexed
- disable/enable relevant actions
- show results in batches as they are being computed, for slow queries
- toggle histogram view
- context menu: copy path

Major Bugs
==========================

- might be possible for -remove to corrupt database
  - e.g. -select-type 1 -remove -update
- sws_scale buffer overflow (264x480 yuv420p)
- MBW move folder option broken

Minor Bugs
=========================
- 64-bit image support
- colordescriptor somewhat non-deterministic, could be a bug
- vacuum database once in a while, maybe have a delete counter
- dctfeature hash logic seems flawed, needs analysis
- ffmpeg deprecations, requires older branch to compile
- maybe problem with some chars in filenames, dirs ending in "!" are skipped by scanner
- MGLW jpeg-compression-quality does not work in zip archives
- MGLW scale-to-fit does not work when diff image enabled
- MGLW up/down key selection swaps sides (scroll wheel does not)
- replace & rename a file will not update index (needs to store date-modified and/or size)
- Windows: titlebar/dialogs do not use native theme
- MGLW: delete multi-select as one batch
- MGLW: suppress QIR eof warnings from thread cancellation
- replace qPrintable() used for file path with qUtf8Printable or QString
- max matches (-p.mm) is off by one in the final result
- maybe scale up small svg prior to indexing
- MGLW: load next row loses focus item on some systems (gnome?)
- MGLW: template match (T) hides diff image / doesn't restore after reset (F5)

Compiling
=========================

## Compiling: Dependencies

- qt5, 5.15
- opencv2, 2.4.13+
- FFmpeg
- quazip

## Compiling: Linux (Ubuntu 21.04/Debian 11)

The easiest way is to use a distro that has Qt 5.15. However, the qt.io distribution or building from source can have additional image formats like tiff, tga, and webp.

This recipe is for Ubuntu 21.04/Debian 11 which includes required versions of core dependencies already.

1.1 Packages

```
apt-get install qtbase5-dev cmake g++ libpng-dev libjpeg-turbo8-dev libtiff5-dev libopenxr-dev libexiv2-dev git
```

1.2 Compiling OpenCV 2.4

```
wget https://github.com/opencv/opencv/archive/2.4.13.6.zip
unzip 2.4.13.6.zip
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D WITH_GSTREAMER=OFF -D ENABLE_FAST_MATH=1 CMAKE_INSTALL_PREFIX=/usr/local ../opencv-2.4.13.6/
make -j8
sudo make install
```

1.3 Compiling quazip

```
git clone https://github.com/stachenov/quazip
cd quazip
cmake .
make
make install
```

1.4a Using System FFmpeg

```
apt-get install libavformat-dev libswscale-dev
```

1.4b Compiling FFmpeg

Compiling FFmpeg from source can provide additional format support.
See: doc/ffmpeg-compile.txt

1.5 Compiling cbird

```
cd cbird
qmake
make -j8
sudo make install
```

1.6 Setup Environment

```
.bashrc

# make libraries in /usr/local/lib visible
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# cbird argument completion
function _cbird { 
  OIFS=$IFS
  IFS='|' 
  COMPREPLY=($(cbird -complete $COMP_CWORD ${COMP_WORDS[*]}))
  IFS=$OIFS 
}
complete -F _cbird cbird
```

## Compiling: AppImage

Build using linuxdeployqt on ubuntu 18.04 LTS. Core dependencies have to be compiled (maybe PPA can be used?)

Use a virtual machine (I use qemu) with xubuntu 18.04 lts, target CPU "Westmere". The CPU target helps ensure compatibility with older systems.

#### apt packages

```
sudo apt-get install bison build-essential gperf flex ruby python git mercurial cmake nasm protobuf-compiler libpulse-dev libasound2-dev libbz2-dev libcap-dev libgcrypt20-dev libnss3-dev libpci-dev libudev-dev libxtst-dev gyp ninja-build libcups2-dev libssl-dev libsrtp2-dev libwebp-dev libjsoncpp-dev libopus-dev libminizip-dev libvpx-dev libsnappy-dev libre2-dev libprotobuf-dev libexiv2-dev libsdl2-dev libmng-dev

sudo apt-get install libxcb*-dev libx11*-dev libxext-dev libxfixes-dev libxi-dev libxcd*-dev libxkb*-dev libxrender-dev libfontconfig1-dev libfreetype6-dev libdrm-dev libegl1-mesa-dev libxcursor-dev libxcomposite-dev libxdamage-dev libxrandr-dev libfontconfig1-dev libxss-dev libevent-dev 
```

#### qt base

```
for x in qtbase qtimageformats qtwayland; do
  wget "https://download.qt.io/official_releases/qt/5.15/5.15.2/submodules/$x-everywhere-src-5.15.2.tar.xz"
done

for x in qtbase qtimageformats qtwayland; do
  tar -Jxvf "$x-everywhere-src-5.15.2.tar.xz"
done
```

```
cd qtbase-everywhere-src-5.15.2
 ./configure  -opensource -confirm-license -nomake examples -nomake tests -no-avx -xcb -silent
make -j8
sudo make install
```

#### qt submodules

```
alias qmake=/usr/local/Qt-5.15.2/bin/qmake
for x in qtimageformats qtwayland; do
  (cd "$x-everywhere-src-5.15.2" && qmake && make -j8 && sudo make install)
done
```

#### ffmpeg

The latest ffmpeg will not work due to deprecations, so checkout a working version.

```
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg
git checkout 358c0b
./configure --enable-gpl --enable-ffplay --disable-static --enable-shared
make -j8
sudo make install
```

#### opencv

```
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D CMAKE_CXX_FLAGS_RELEASE="-march=westmere -Ofast" -D CMAKE_C_FLAGS_RELEASE="-march=westmere -Ofast" -D ENABLE_FAST_MATH=ON -D ENABLE_SSSE3=ON -D ENABLE_SSE41=ON -D ENABLE_SSE42=ON ../opencv-2.4.13.6/
```

#### quazip

```
git clone https://github.com/stachenov/quazip
git checkout v0.8.1
alias qmake=/usr/local/Qt-5.15.2/bin/qmake
cd quazip
qmake PREFIX=/usr/local
make -j8
sudo make install
```

#### cbird

"make appimage" should work if linuxdeployqt is in ~/Downloads/

```
cd cbird
alias qmake=/usr/local/Qt-5.15.2/bin/qmake
qmake
make -j8
make appimage
```

## Compiling: Windows

The Windows version is compiled using MXE cross-compiler tools from a Linux host. MXE has an apt repository for dependencies to make this easier, the steps are shown here.

2.1 Get the prereqs

```
apt-get install lzip intltool wine64
```

2.2 Add mxe apt repository

```
sudo apt-key adv \
    --keyserver keyserver.ubuntu.com \
    --recv-keys 86B72ED9 && \
sudo add-apt-repository \
    "deb [arch=amd64] https://pkg.mxe.cc/repos/apt `lsb_release -sc` main" && \
sudo apt-get update
```

2.3 Install mxe toolchain and libraries into /usr/lib/mxe

```
apt-get install mxe-x86-64-w64-mingw32.shared-cc mxe-x86-64-w64-mingw32.shared-qtbase mxe-x86-64-w64-mingw32.shared-quazip mxe-x86-64-w64-mingw32.shared-ffmpeg mxe-x86-64-w64-mingw32.shared-exiv2
```

2.4 Compile OpenCV 2.4.x

Once mxe is installed this is like the Linux build. The mxe.env script sets the shell environment to redirect build tools like qmake and cmake. Note this will break the Linux build in the current shell.

```
cd cbird
source windows/mxe.env
cd windows
unzip <2.4.13.6.zip>
mkdir build-opencv
cd build-opencv
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D CMAKE_CXX_FLAGS_RELEASE="-march=westmere -Ofast" -D CMAKE_C_FLAGS_RELEASE="-march=westmere -Ofast" -D ENABLE_FAST_MATH=ON -D ENABLE_SSSE3=ON -D ENABLE_SSE41=ON -D ENABLE_SSE42=ON ../opencv-2.4.13.6/
make -j8
```

2.5 Compile cbird

```
cd cbird
source windows/mxe.env
qmake
make -j8
make install
```


Development
=========================

## Conventions
- Code Style: clang-format: Google
- Two spaces indent
- 80-100 character line
- Editor: Qt Creator is nice (enable ClangFormat plugin)

## Unit tests
Cbird uses the QTest unit test framework. The tests require a test data set, see release page on github.

```
export TEST_DATA_DIR=</path/to/cbird-testdata>
cd unit/

# recompile, run all tests with coverage
./unit.sh -clean -coverage

# build coverage report
./coverage.sh

# run one test
qmake <test>.pro -o <test>.pro.make
make -f <test>.pro.make -j
./runtest-<test>
