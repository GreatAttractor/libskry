/*
    This example illustrates proper error handling on every step.

    Do not forget to enable your compiler's C++11 mode. By default,
    libskry is built with OpenMP support, so remember to link with
    the necessary libraries. E.g. in case of GCC on Linux, use:

        g++ -std=c++11 example2.cpp -lskry -lgomp -I ../include -L ../bin
*/

#include <iostream>
#include <vector>
#include <skry/skry_cpp.hpp>


int main(int argc, char *argv[])
{
    if (SKRY_SUCCESS != SKRY_initialize())
    {
        std::cout << "Failed to initialize libskry, exiting.\n";
        return 1;
    }

    enum SKRY_result result;

    libskry::c_ImageSequence imgSeq =
        libskry::c_ImageSequence::InitVideoFile("sun01.avi", &result);

    if (result != SKRY_SUCCESS)
    {
        std::cout << "Error opening video file: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    // Sometimes we may want to skip certain (e.g. invalid) frames during
    // processing; see the "active" family of methods for details.
    size_t numSteps = imgSeq.GetImageCount();

    libskry::c_ImageAlignment imgAlignment(
        imgSeq,
        { }, // pass an empty vector; stabilization anchors
             // will be placed automatically
        32, 32, // block radius and search radius for block-matching
        0.33f, // min. relative brightness to place anchors at;
               // avoid the dark background
        &result);

    if (result != SKRY_SUCCESS)
    {
        std::cout << "Error initializing image alignment: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    // From now on till the end of stacking we must not call any modifying
    // methods on 'imgSeq' (those without 'const' qualifier),
    // as it is used by 'imgAlignment' and subsequent objects.

    size_t step = 1;
    std::cout << "\nImage alignment: step ";
    while (SKRY_SUCCESS == (result = imgAlignment.Step()))
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
        // Eventually, a "step" function returns SKRY_LAST_STEP
        // (or an error code).
    }
    std::cout << " done." << std::endl;

    if (result != SKRY_LAST_STEP)
    {
        std::cout << "Image alignment failed: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    libskry::c_QualityEstimation qualEst(imgAlignment, 40, 3);

    if (!qualEst)
    {
        // The only possible failure here is running out of memory
        std::cout << "Error initializing quality estimation: "
                     "out of memory." << std::endl;
        return 1;
    }

    step = 1;
    std::cout << "\n---------\nQuality estimation: step ";
    while (SKRY_SUCCESS == (result = qualEst.Step()))
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
    }
    std::cout << " done." << std::endl;

    if (result != SKRY_LAST_STEP)
    {
        std::cout << "Quality estimation failed: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    libskry::c_RefPointAlignment refPtAlignment(
            qualEst,
            { }, // pass an empty vector; stabilization anchors
                 // will be placed automatically
            0.33f, // min. relative brightness to place points at;
                   // avoid the dark background
            // Consider only 30% of the best-quality frame fragments
            // (this criterion later also applies to stacking)
            SKRY_PERCENTAGE_BEST, 30,
            40, // point spacing in pixels
            &result);

    if (result != SKRY_SUCCESS)
    {
        std::cout << "Error initializing reference point alignment: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    step = 1;
    std::cout << "\n---------\nReference point alignment: step ";
    while (SKRY_SUCCESS == (result = refPtAlignment.Step()))
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
    }
    std::cout << " done." << std::endl;

    if (result != SKRY_LAST_STEP)
    {
        std::cout << "Reference point alignment failed: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    libskry::c_Stacking stacking(refPtAlignment, nullptr, &result);

    if (result != SKRY_SUCCESS)
    {
        std::cout << "Error initializing image stacking: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    step = 1;
    std::cout << "\n---------\nImage stacking: step ";
    while (SKRY_SUCCESS == (result = stacking.Step()))
    {
        std::cout << step++ << "/" << numSteps << " "; std::cout.flush();
    }
    std::cout << " done." << std::endl;

    if (result != SKRY_LAST_STEP)
    {
        std::cout << "Image stacking failed: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    // Now 'imgSeq' can be freely accessed again, if needed.

    // The stack is a mono or RGB image (depending on 'imgSeq')
    // with 32-bit floating point pixels, so we need to convert it
    // before saving as a 16-bit TIFF.
    libskry::c_Image imgStackMono16 =
        libskry::c_Image::ConvertPixelFormat(stacking.GetFinalImageStack(),
                                             SKRY_PIX_RGB16);

    if (!imgStackMono16)
    {
        std::cout << "Failed to allocate output image: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    if (SKRY_SUCCESS != (result = imgStackMono16.Save("sun01_stack.tif", SKRY_TIFF_16)))
    {
        std::cout << "Error saving output image: "
                  << SKRY_get_error_message(result) << std::endl;
        return 1;
    }

    SKRY_deinitialize();
    return 0;
}
