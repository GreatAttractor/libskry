/*
    An unrealistically simple example without any error checking
    (you really should not use code like this!), just to demonstrate
    what objects and in what order must be created.

    Do not forget to enable your compiler's C99 mode. By default,
    libskry is built with OpenMP support, so remember to link with
    the necessary libraries. E.g. in case of GCC on Linux, use:

        gcc -std=c99 example1.c -lskry -lgomp -lm -L ../bin -I ../include
*/

// Needed for MinGW, so that is uses its own string formatting functions.
// Otherwise, Microsoft runtime DLL would be used, which does not support %zu.
#define __USE_MINGW_ANSI_STDIO 1

#include <stddef.h>
#include <stdio.h>
#include <skry/skry.h>


int main(int argc, char *argv[])
{
    SKRY_initialize();

    SKRY_ImgSequence *img_seq = SKRY_init_video_file("sun01.avi", 0, 0);

    // Sometimes we may want to skip certain (e.g. invalid) frames during
    // processing; see the "active" family of functions for details.
    size_t num_steps = SKRY_get_img_count(img_seq);

    SKRY_ImgAlignment *img_alignment =
        SKRY_init_img_alignment(
            img_seq,
            SKRY_IMG_ALGN_ANCHORS,
            0, 0, // stabilization anchors will be placed automatically
            32, 32, // block radius and search radius for block-matching
            0.33f, // min. relative brightness to place anchors at;
                   // avoid the dark background
            0); // not interested in the result

    // From now on till the end of stacking we must not call any modifying
    // functions on 'img_seq' (those that take a non-const pointer to it),
    // as it is used by 'img_alignment' and subsequent objects.

    size_t step = 1;
    printf("\nImage alignment: step ");
    while (SKRY_SUCCESS == SKRY_img_alignment_step(img_alignment))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
        // Eventually, a "step" function returns SKRY_LAST_STEP
        // (or an error code).
    }
    printf(" done.\n");

    SKRY_QualityEstimation *qual_est =
        SKRY_init_quality_est(
            img_alignment,
            40, 3);

    step = 1;
    printf("\nQuality estimation: step ");
    while (SKRY_SUCCESS == SKRY_quality_est_step(qual_est))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
    }
    printf(" done.\n");

    SKRY_RefPtAlignment *ref_pt_align =
        SKRY_init_ref_pt_alignment(
            qual_est,
            0, 0, // reference points will be placed automatically

            // Consider only 30% of the best-quality frame fragments
            // (this criterion later also applies to stacking)
            SKRY_PERCENTAGE_BEST, 30,

            32, // reference block size
            20, // ref. block search radius

            0,

            0.33f, // min. relative brightness to place points at;
                   // avoid the dark background

            1.2f, // structure threshold; 1.2 is recommended

            1, // structure scale (in pixels)

            40); // point spacing in pixels

    step = 1;
    printf("\nReference point alignment: step ");
    while (SKRY_SUCCESS == SKRY_ref_pt_alignment_step(ref_pt_align))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
    }
    printf(" done.\n");

    SKRY_Stacking *stacking = SKRY_init_stacking(ref_pt_align, 0, 0);
    step = 1;
    printf("\nImage stacking: step ");
    while (SKRY_SUCCESS == SKRY_stacking_step(stacking))
    {
        printf("%zu/%zu ", step++, num_steps); fflush(stdout);
    }
    printf(" done.\n");

    // Now 'img_seq' can be freely accessed again, if needed.

    // The stack is a mono or RGB image (depending on 'img_seq')
    // with 32-bit floating point pixels, so we need to convert it
    // before saving as a 16-bit TIFF.
    SKRY_Image *img_stack_16 = SKRY_convert_pix_fmt(
                                 SKRY_get_image_stack(stacking),
                                 SKRY_PIX_RGB16, SKRY_DEMOSAIC_DONT_CARE);
    SKRY_save_image(img_stack_16, "sun01_stack.tif", SKRY_TIFF_16);
    SKRY_free_image(img_stack_16);


    // The order of freeing is not important
    SKRY_free_stacking(stacking);
    SKRY_free_ref_pt_alignment(ref_pt_align);
    SKRY_free_quality_est(qual_est);
    SKRY_free_img_alignment(img_alignment);
    SKRY_free_img_sequence(img_seq);

    SKRY_deinitialize();
    return 0;
}
