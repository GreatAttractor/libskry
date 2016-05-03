/*
    An unrealistically simple example without any error checking
    (you really should not use code like this!), just to demonstrate
    what objects and in what order must be created.
    
    Do not forget to enable your compiler's C++11 mode. By default,
    libskry is built with OpenMP support, so remember to link with
    the necessary libraries. E.g. in case of GCC on Linux, use:
    
        g++ -std=c++11 example1.cpp -lskry -lgomp -I ../include -L ../bin
*/

#include <iostream>
#include <vector>
#include <skry/skry_cpp.hpp>


int main(int argc, char *argv[])
{
    SKRY_initialize();

    libskry::c_ImageSequence imgSeq =
        libskry::c_ImageSequence::InitVideoFile("sun01.avi");

    // Sometimes we may want to skip certain (e.g. invalid) frames during
    // processing; see the "active" family of methods for details.
    size_t numSteps = imgSeq.GetImageCount();

    libskry::c_ImageAlignment imgAlignment(
        imgSeq,
        { }, // pass an empty vector; stabilization anchors
             // will be placed automatically
        32, 32, // block radius and search radius for block-matching
        0.33f); // min. relative brightness to place anchors at;
                // avoid the dark background

    // From now on till the end of stacking we must not call any modifying
    // methods on 'imgSeq' (those without 'const' qualifier),
    // as it is used by 'imgAlignment' and subsequent objects.

    size_t step = 1;
    std::cout << "\nImage alignment: step ";
    while (SKRY_SUCCESS == imgAlignment.Step())
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
        // Eventually, a "step" function returns SKRY_LAST_STEP
        // (or an error code).
    }
    std::cout << " done." << std::endl;

    libskry::c_QualityEstimation qualEst(imgAlignment, 40, 3);

    step = 1;
    std::cout << "\n---------\nQuality estimation: step ";
    while (SKRY_SUCCESS == qualEst.Step())
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
    }
    std::cout << " done." << std::endl;

    libskry::c_RefPointAlignment refPtAlignment(
            qualEst,
            { }, // pass an empty vector; reference points
                 // will be placed automatically
            0.33f, // min. relative brightness to place points at;
                   // avoid the dark background
            // Consider only 30% of the best-quality frame fragments
            // (this criterion later also applies to stacking)
            SKRY_PERCENTAGE_BEST, 30,
            40); // point spacing in pixels

    step = 1;
    std::cout << "\n---------\nReference point alignment: step ";
    while (SKRY_SUCCESS == refPtAlignment.Step())
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
    }
    std::cout << " done." << std::endl;

    libskry::c_Stacking stacking(refPtAlignment);

    step = 1;
    std::cout << "\n---------\nImage stacking: step ";
    while (SKRY_SUCCESS == stacking.Step())
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
    }
    std::cout << " done." << std::endl;

    // Now 'imgSeq' can be freely accessed again, if needed.

    // The stack is a mono or RGB image (depending on 'img_seq')
    // with 32-bit floating point pixels, so we need to convert it
    // before saving as a 16-bit TIFF.
    libskry::c_Image imgStackMono16 =
        libskry::c_Image::ConvertPixelFormat(stacking.GetFinalImageStack(),
                                             SKRY_PIX_RGB16);

    imgStackMono16.Save("sun01_stack.tif", SKRY_TIFF_16);

    SKRY_deinitialize();
    return 0;
}
