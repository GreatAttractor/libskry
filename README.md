# **libskry**

## Lucky imaging implementation for the Stackistry project

Copyright (C) 2016, 2017 Filip Szczerek (ga.software@yahoo.com)

version 0.3.0 (2017-06-05)

*This library comes with ABSOLUTELY NO WARRANTY. This is free software, licensed under GNU General Public License v3 or any later version and you are welcome to redistribute it under certain conditions. See the LICENSE file for details.*


----------------------------------------

- 1\. Introduction
- 2\. Requirements
- 3\. Input/output formats support
- 4\. Principles of operation
- 5\. Usage notes
 - 5\.1\. C++-specific
- 6\. Change log


----------------------------------------
## 1. Introduction

**libskry** implements the *lucky imaging* principle of astronomical imaging: creating a high-quality still image out of a series of many (possibly thousands) low quality ones (blurred, deformed, noisy). The resulting *image stack* typically requires post-processing, including sharpening (e.g. via deconvolution). Such post-processing is not performed by libskry.

For a comprehensive example of libskry's usage, see the **[Stackistry](https://github.com/GreatAttractor/stackistry/)** project.

Further reading: **[libskry - Algorithms summary](doc/algorithms.pdf)**.


----------------------------------------
## 2. Requirements

Building libskry requires a C99-compatible C compiler. A GNU Make-compatible Makefile is provided (tested under Linux and MinGW/MSYS). Multi-threaded processing requires a compiler with OpenMP support.

If Make cannot be used, simply compile all `*.c` files and link them into a library.


----------------------------------------
### 2.1. Using with *libav*

If `USE_LIBAV=1` (required for full AVI support), programs linking with *libskry* must also link with *libav* (`-lavformat -lavcodec -lavutil`). The libraries are usually available as a package named `ffmpeg-devel` or similar (under MSYS2: `mingw64/mingw-w64-x86_64-ffms2`). If not, they can be built from sources by running:

```
$ git clone https://git.ffmpeg.org/ffmpeg.git
$ cd ffmpeg
$ ./configure --disable-programs --enable-gray --disable-muxers --disable-demuxers --enable-demuxer=avi --disable-encoders --disable-decoders --enable-decoder=rawvideo --enable-decoder=bmp --disable-protocols --enable-protocol=file --disable-parsers --disable-devices --disable-hwaccels --disable-bsfs --disable-filters --disable-bzlib --disable-lzma --disable-schannel --disable-xlib --disable-zlib --disable-iconv
$ make
$ make install
```

To build under MSYS2 (Windows), first install Git and Yasm: `pacman -S git yasm`, and pass an additional parameter to `./configure`: `--prefix=$MSYSTEM_PREFIX`.


#### Ubuntu and *libav*

On Ubuntu the following packages are needed: `ffmpeg libavcodec-dev libavformat-dev libavutil-dev`.

In case of Ubuntu 16.04, versions provided by default are too old. Add the following repository before installing the packages:

    sudo add-apt-repository ppa:jonathonf/ffmpeg-3
    sudo apt update

    
----------------------------------------
## 3. Input/output formats support

Supported input formats:

- AVI: uncompressed DIB (mono or RGB), Y8/Y800
- SER: mono, RGB, raw color
- BMP: 8-, 24- and 32-bit uncompressed
- TIFF: 8- and 16-bit per channel mono or RGB uncompressed

Supported output formats:

- BMP: 8- and 24-bit uncompressed
- TIFF: 8- and 16-bit per channel mono or RGB uncompressed

In case of 64-bit builds of libskry, there are no size limits for the input video/image size (other than the available memory). The user can choose to treat mono videos as raw color (enables demosaicing).

If using *libav* is disabled, there is only limited AVI support (no extended or ODML AVI headers).


----------------------------------------
## 4. Principles of operation

Processing of a raw input image sequence consists of the following steps:

1. Image alignment (video stabilization)
2. Quality estimation
3. Reference point alignment
4. Image stacking

**Image alignment** compensates any global image drift; the result is a stabilized video of size usually smaller than any of the input images. The (rectangular) region visible in all input images is referred to as *images' intersection* throughout the source code.

**Quality estimation** concerns the changes of local image quality. This information is later used to reject (via an user-specified criterion) poor-quality image fragments during reference point alignment and image stacking.

**Reference point alignment** traces the geometric distortion of images (by using local block-matching) which is later compensated for during image stacking.

**Image stacking** performs shift-and-add summation of image fragments using information from previous steps. This improves signal-to-noise ratio. Note that stacking too many images may decrease quality – adding lower-quality fragments causes more blurring in the output stack.


----------------------------------------
## 5. Usage notes

To use libskry's C interface, add the following:

```C
#include <skry/skry.h>
```

To use the C++ wrappers:

```C++
#include <skry/skry_cpp.hpp>
```

(this includes ``skry.h`` internally).

If libskry is built with OpenMP support (this is the default), any program using it must also link with OpenMP-related libraries. E.g. in case of GCC on Linux or MinGW/MSYS, use ``-lskry -lm -lgomp``.

See the ``doc/src`` folder for simple examples that illustrate the full stacking process.

If a function returns a non-const pointer, it is the caller's responsibility to free its memory with `free()` or with an appropriate ``SKRY_free_XX`` function.

All pointer parameters must not be null unless noted otherwise in a comment.

All processing phases operate on the `SKRY_ImgSequence` object which is specified in a call to `SKRY_init_img_alignment()`. As long as processing is not completed, this object must not be modified elsewhere, i.e. no function can be called that accepts a non-const pointer to it.

After a call to a ``XX_step()`` function, the current image of the associated image sequence is the one that has been just used for processing inside ``XX_step()``. That is, these functions perform a “seek next” operation on the image sequence as they start, not as they finish.

All functions that satisfy both the conditions:
- do not take a ``SKRY_ImgSequence`` parameter
- take an “image index” parameter

treat the image index as referring only to the active images subset of the associated image sequence. E.g. when an image sequence contains 500 images, of which only 300 are active, the function:

```C
SKRY_quality_t SKRY_get_area_quality(const SKRY_QualityEstimation *qual_est,
                                     size_t area_idx, size_t img_idx);
```

expects that ``0 <= img_idx < 300``. The indexing ignores all non-active images (even if active ones are not sequential).


----------------------------------------
### 5.1. C++-specific

Using the C++ wrappers requires a C++11-capable compiler. Refer to ``include/skry_cpp.hpp`` for more information.

All classes have default move constructor and move assignment operator declared; they simply move the pointer owned by ``pimpl``.

Each ``pimpl`` is a smart pointer holding the libskry's opaque pointer of appropriate type;
the pointer's deleter is a matching ``SKRY_free_`` function.

The ``pimpl`` may be null; all classes all boolean-testable (see ``ISkryPtrWrapper``)
to detect it, e.g.:

```C++
// 'imgCopy' may contain a null 'pimpl' if copying failed due to lack of free memory
c_Image imgCopy = img;

if (imgCopy)  //equivalently: if (imgCopy.IsValid())
{
    //do work
}
else
{
    //error
}
```


----------------------------------------
## 6. Change log

```
0.3.0 (2017-06-05)
  New features:
    - Better AVI support via libav
  Bug fixes:
    - Fixed crashes during triangulation

0.2.0 (2016-12-14)
  New features:
    - Image alignment using the intensity centroid (useful for planets)
    - Structure detection for better automatic placement of reference points
  Bug fixes:
    - Fixed opening AVI files produced by FFMPEG

0.1.1 (2016-08-05)
  New features:
    - Support for Y8/Y800 AVI files

0.0.3 (2016-05-22)
  New features:
    - Demosaicing of raw color images
  Enhancements:
    - Using the middle 3/4 instead of just 1/2 of image when choosing
      anchor location
  Bug fixes:
    - Error on opening SER videos recorded by Genika software
    - Incorrect RGB channel order when saving a BMP

0.0.2 (2016-05-08)
  Bug fixes:
    - Fix error when getting TIFF metadata
    - Return SKRY_SUCCESS after loading a TIFF
    - Fix pixel conversion from RGB16
    - Use all fragments if criterion is "number of the best"
      and the threshold is more than active images count

0.0.1 (2016-05-01)
  Initial revision.
```
