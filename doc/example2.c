/*
    This example illustrates proper error handling on every step.

    Do not forget to enable your compiler's C99 mode. By default,
    libskry is built with OpenMP support, so remember to link with
    the necessary libraries. E.g. in case of GCC on Linux, use:

        gcc -std=c99 example2.c -lskry -lgomp -lm -L ../bin -I ../include
*/

// Needed for MinGW, so that is uses its own string formatting functions.
// Otherwise, Microsoft runtime DLL would be used, which does not support %zu.
#define __USE_MINGW_ANSI_STDIO 1

#include <stddef.h>
#include <stdio.h>
#include <skry/skry.h>

/* Destructor-like macro; not really needed in 'main()',
   but let us clean up after ourselves. Note that calling
   'SKRY_free_' on a null pointer is harmless. */
#define FREE_OBJS()                               \
    do {                                          \
        SKRY_free_img_sequence(img_seq);          \
        SKRY_free_quality_est(qual_est);          \
        SKRY_free_ref_pt_alignment(ref_pt_align); \
        SKRY_free_stacking(stacking);             \
    } while (0)

int main(int argc, char *argv[])
{
    if (SKRY_SUCCESS != SKRY_initialize())
    {
        printf("Failed to initialize libskry, exiting.\n");
        return 1;
    }

    enum SKRY_result result;
    // Need to define all of them here so that FREE_OBJS can be used
    SKRY_ImgSequence *img_seq = 0;
    SKRY_ImgAlignment *img_alignment = 0;
    SKRY_QualityEstimation *qual_est = 0;
    SKRY_RefPtAlignment *ref_pt_align = 0;
    SKRY_Stacking *stacking = 0;

    img_seq = SKRY_init_video_file("sun01.avi", 0, &result);
    if (SKRY_SUCCESS != result)
    {
        FREE_OBJS();
        printf("Error opening video file: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    // Sometimes we may want to skip certain (e.g. invalid) frames during
    // processing; see the "active" family of functions for details.
    size_t num_steps = SKRY_get_img_count(img_seq);

    img_alignment =
        SKRY_init_img_alignment(
            img_seq,
            SKRY_IMG_ALGN_ANCHORS,
            0, 0, // stabilization anchors will be placed automatically
            32, 32, // block radius and search radius for block-matching
            0.33f, // min. relative brightness to place anchors at;
                   // avoid the dark background
            &result);

    if (SKRY_SUCCESS != result)
    {
        FREE_OBJS();
        printf("Error initializing image alignment: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    // From now on till the end of stacking we must not call any modifying
    // functions on 'img_seq' (those that take a non-const pointer to it),
    // as it is used by 'img_alignment' and subsequent objects.

    size_t step = 1;
    printf("\nImage alignment: step ");
    while (SKRY_SUCCESS == (result = SKRY_img_alignment_step(img_alignment)))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
        // Eventually, a "step" function returns SKRY_LAST_STEP
        // (or an error code).
    }
    printf(" done.\n");

    if (SKRY_LAST_STEP != result)
    {
        FREE_OBJS();
        printf("Image alignment failed: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    qual_est = SKRY_init_quality_est(
                img_alignment,
                40, 3);

    // The only possible failure here is running out of memory
    if (!qual_est)
    {
        FREE_OBJS();
        printf("Error initializing quality estimation: out of memory\n");
        return 1;
    }

    step = 1;
    printf("\nQuality estimation: step ");
    while (SKRY_SUCCESS == (result = SKRY_quality_est_step(qual_est)))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
    }
    printf(" done.\n");

    if (SKRY_LAST_STEP != result)
    {
        FREE_OBJS();
        printf("Quality estimation failed: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }


    ref_pt_align =
        SKRY_init_ref_pt_alignment(
            qual_est,
            0, 0, // reference points will be placed automatically

            // Consider only 30% of the best-quality frame fragments
            // (this criterion later also applies to stacking)
            SKRY_PERCENTAGE_BEST, 30,

            32, // reference block size
            20, // ref. block search radius

            &result,

            0.33f, // min. relative brightness to place points at;
                   // avoid the dark background

            1.2f, // structure threshold; 1.2 is recommended

            1, // structure scale (in pixels)

            40); // point spacing in pixels

    if (SKRY_SUCCESS != result)
    {
        FREE_OBJS();
        printf("Error initializing reference point alignment: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    step = 1;
    printf("\nReference point alignment: step ");
    while (SKRY_SUCCESS == (result = SKRY_ref_pt_alignment_step(ref_pt_align)))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
    }
    printf(" done.\n");

    if (SKRY_LAST_STEP != result)
    {
        FREE_OBJS();
        printf("Reference point alignment failed: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    stacking = SKRY_init_stacking(ref_pt_align, 0, &result);

    if (SKRY_SUCCESS != result)
    {
        FREE_OBJS();
        printf("Error initializing image stacking: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    step = 1;
    printf("\nImage stacking: step ");
    while (SKRY_SUCCESS == (result = SKRY_stacking_step(stacking)))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
    }
    printf(" done.\n");

    if (SKRY_LAST_STEP != result)
    {
        FREE_OBJS();
        printf("Image stacking failed: %s\n",
               SKRY_get_error_message(result));
        return 1;
    }

    // Now 'img_seq' can be freely accessed again, if needed.

    // The stack is a mono or RGB image (depending on 'img_seq')
    // with 32-bit floating point pixels, so we need to convert it
    // before saving as a 16-bit TIFF.
    SKRY_Image *img_stack_16 = SKRY_convert_pix_fmt(
                                 SKRY_get_image_stack(stacking),
                                 SKRY_PIX_RGB16, SKRY_DEMOSAIC_DONT_CARE);

    if (!img_stack_16)
    {
        FREE_OBJS();
        printf("Failed to allocate output image.\n");
        return 1;
    }

    if (SKRY_SUCCESS !=
            (result = SKRY_save_image(img_stack_16, "sun01_stack.tif", SKRY_TIFF_16)))
    {
        FREE_OBJS();
        printf("Error saving output image: %s",
               SKRY_get_error_message(result));
        return 1;
    }

    SKRY_free_image(img_stack_16);

    FREE_OBJS();

    SKRY_deinitialize();
    return 0;
}
