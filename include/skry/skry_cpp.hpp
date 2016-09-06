/*
libskry - astronomical image stacking
Copyright (C) 2016 Filip Szczerek <ga.software@yahoo.com>

This file is part of libskry.

Libskry is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Libskry is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libskry.  If not, see <http://www.gnu.org/licenses/>.

File description:
    Header-only C++ wrappers for the C interface. Requires C++11 compiler.
*/

#ifndef LIB_STACKISTRY_CPP_HEADER
#define LIB_STACKISTRY_CPP_HEADER

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "skry.h"


namespace libskry
{
    /*
     Usage notes:

     1. All classes have default move constructor and move assignment operator declared;
     they simply move the pointer owned by 'pimpl'.

     2. Each 'pimpl' is a 'unique_ptr' holding the libskry's opaque pointer of appropriate type;
     the unique_ptr's deleter is a matching 'SKRY_free_' function.

     3. The 'pimpl' may be null; all classes all boolean-testable (see 'ISkryPtrWrapper')
     to detect it, e.g.:

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

     4. All methods of classes other than c_ImageSequence that take an "image index" parameter
        treat it as referring only to the active images' subset of the associated image sequence.
        E.g. when an image sequence contains 500 images, of which only 300 are active, the method


            struct SKRY_point c_RefPointAlignment::GetReferencePointPos(int pointIdx, int imgIdx,
                                                                        bool &isValid)

        expects that 0 <= imgIdx < 300. The indexing ignores all non-active images
        (even if active ones are not sequential).
    */

    class ISkryPtrWrapper
    {
    public:
        explicit virtual operator bool() const = 0;
        bool IsValid() const { return operator bool(); }
    };

    class c_Image: public ISkryPtrWrapper
    {
        std::unique_ptr<SKRY_Image,
                        SKRY_Image *(*)(SKRY_Image *)> pimpl;

        c_Image(SKRY_Image *skryImg): pimpl(skryImg, SKRY_free_image) { }

    public:
        explicit virtual operator bool() const { return pimpl != nullptr; }

        c_Image(): pimpl(nullptr, SKRY_free_image) { }

        /// Allowed even if 'img' contains a null 'pimpl' (SKRY_get_img_copy() will return null)
        c_Image(const c_Image &img): pimpl(SKRY_get_img_copy(img.pimpl.get()), SKRY_free_image) { }

        c_Image & operator=(const c_Image &img)
        {
            /// Allowed even if 'img' contains a null 'pimpl' (SKRY_get_img_copy() will return null)
            pimpl.reset(SKRY_get_img_copy(img.pimpl.get()));
            return *this;
        }

        c_Image(unsigned width, unsigned height, enum SKRY_pixel_format pixFmt,
            /// Can be null; if not null, used only if 'pixFmt' is PIX_PAL8
            const struct SKRY_palette *palette,
            bool zero_fill)
        : pimpl(SKRY_new_image(width, height, (SKRY_pixel_format)pixFmt, palette, (int)zero_fill), SKRY_free_image)
        { }

        c_Image & operator=(c_Image &&) = default;

        c_Image(c_Image &&)             = default;

        unsigned GetWidth() const   { return SKRY_get_img_width(pimpl.get()); }

        unsigned GetHeight() const  { return SKRY_get_img_height(pimpl.get()); }

        /// May be negative (when image stores lines bottom-to-top)
        ptrdiff_t GetLineStrideInBytes() const { return SKRY_get_line_stride_in_bytes(pimpl.get()); }

        /// Includes padding, if any
        size_t GetBytesPerLine() const
        {
            ptrdiff_t stride = SKRY_get_line_stride_in_bytes(pimpl.get());
            if (stride >= 0)
                return stride;
            else
                return (size_t)(-stride);
        }

        size_t GetBytesPerPixel() const { return SKRY_get_bytes_per_pixel(pimpl.get()); }

        /// Returns pointer to start of the specified line
        void *GetLine(unsigned line) const { return SKRY_get_line(pimpl.get(), line); }

        enum SKRY_pixel_format GetPixelFormat() const { return SKRY_get_img_pix_fmt(pimpl.get()); }

        /// Fills 'pal'; returns SKRY_NO_PALETTE if image does not contain a palette
        enum SKRY_result GetPalette(struct SKRY_palette &pal) const { return SKRY_get_palette(pimpl.get(), &pal); }

        /** \brief Copies (with cropping or padding) a fragment of image to another. There is no scaling.
               Pixel formats of source and destination must be the same. 'srcImg' must not equal 'destImg'. */
        static void ResizeAndTranslate(const c_Image &srcImg, c_Image &destImg,
                        int srcXmin,     ///< X min of input data in source image
                        int srcYmin,     ///< Y min of input data in source image
                        unsigned width,  ///< width of fragment to copy
                        unsigned height, ///< height of fragment to copy
                        int destXofs,    ///< X offset of input data in destination image
                        int destYofs,    ///< Y offset of input data in destination image
                        bool clearToZero ///< if true, 'destImg' areas not copied on will be cleared to zero
                        )
        {
            SKRY_resize_and_translate(srcImg.pimpl.get(), destImg.pimpl.get(), srcXmin, srcYmin,
                                      width, height, destXofs, destYofs, clearToZero);
        }

        /// Returned image has lines stored top-to-bottom, no padding
        static c_Image ConvertPixelFormat(const c_Image &srcImg, enum SKRY_pixel_format destPixFmt,
                                          enum SKRY_demosaic_method demosaicMethod = SKRY_DEMOSAIC_DONT_CARE)
        {
            c_Image result(SKRY_convert_pix_fmt(srcImg.pimpl.get(), destPixFmt, demosaicMethod));
            return result;
        }

        static c_Image ConvertPixelFormatOfSubimage(
                            const c_Image &srcImg, enum SKRY_pixel_format destPixFmt,
                            int x0, int y0, unsigned width, unsigned height,
                            enum SKRY_demosaic_method demosaicMethod = SKRY_DEMOSAIC_DONT_CARE)
        {
            c_Image result(SKRY_convert_pix_fmt_of_subimage(srcImg.pimpl.get(), destPixFmt,
                x0, y0, width, height, demosaicMethod));
            return result;
        }

//        /// Returns a blurred version of the image (SKRY_PIX_MONO8)
//        /** Requirements: Pixel format is SKRY_PIX_MONO8, iterations * box_radius < 2^22. */ //FIX this comment!
//        c_Image BoxBlur(unsigned boxRadius, unsigned iterations) const
//        {
//            return c_Image(box_blur_img(pimpl.get(), boxRadius, iterations));
//        }

        static c_Image Load(const char *fileName,
                            /// If not null, receives operation result
                            enum SKRY_result *result = nullptr)
        {
            return c_Image(SKRY_load_image(fileName, result));
        }

        enum SKRY_result Save(const char *fileName, enum SKRY_output_format outputFmt) const
        {
            return SKRY_save_image(pimpl.get(), fileName, outputFmt);
        }

        /// Returns metadata without reading the pixel data
        static enum SKRY_result GetMetadata(const char *fileName,
                                            unsigned *width,  ///< If not null, receives image width
                                            unsigned *height, ///< If not null, receives image height
                                            enum SKRY_pixel_format *pixFmt) ///< If not null, receives pixel format
        {
            return  SKRY_get_image_metadata(fileName, width, height, pixFmt);
        }

        friend class c_ImageSequence;
        friend class c_ImageAlignment;
        friend class c_QualityEstimation;
        friend class c_RefPointAlignment;
        friend class c_Stacking;
    };

    /// Movable, non-copyable
    class c_ImageSequence: public ISkryPtrWrapper
    {
        std::unique_ptr<SKRY_ImgSequence,
                        SKRY_ImgSequence *(*)(SKRY_ImgSequence *)> pimpl;

        c_ImageSequence(SKRY_ImgSequence *skryImgSeq): pimpl(skryImgSeq, SKRY_free_img_sequence) { }

    public:
        explicit virtual operator bool() const { return pimpl != nullptr; }

        c_ImageSequence(): pimpl(nullptr, SKRY_free_img_sequence) { }

        c_ImageSequence(const c_ImageSequence &)             = delete;

        c_ImageSequence & operator=(const c_ImageSequence &) = delete;

        c_ImageSequence(c_ImageSequence &&)                  = default;

        c_ImageSequence & operator=(c_ImageSequence &&)      = default;

        static c_ImageSequence InitImageList(const std::vector<std::string> &fileNames)
        {
            c_ImageSequence imgSeq(SKRY_init_image_list(fileNames.size(), nullptr, nullptr));

            if (imgSeq)
                for (auto fn: fileNames)
                    SKRY_image_list_add_img(imgSeq.pimpl.get(), fn.c_str());

            return imgSeq;
        }

        static c_ImageSequence InitVideoFile(const char *fileName, enum SKRY_result *result = nullptr)
        {
            return c_ImageSequence(SKRY_init_video_file(fileName, nullptr, result));
        }

        size_t GetCurrentImgIdx() const { return SKRY_get_curr_img_idx(pimpl.get()); }

        size_t GetCurrentImgIdxWithinActiveSubset() const { return SKRY_get_curr_img_idx_within_active_subset(pimpl.get()); }

        size_t GetImageCount() const { return SKRY_get_img_count(pimpl.get()); }

        size_t GetActiveImageCount() const { return SKRY_get_active_img_count(pimpl.get()); }

        /// Seeks to the first active image
        void SeekStart() { SKRY_seek_start(pimpl.get()); }

        /// Seeks forward to the next active image
        enum SKRY_result SeekNext() { return SKRY_seek_next(pimpl.get()); }

        c_Image GetCurrentImage(
            enum SKRY_result *result = nullptr ///< If not null, receives operation result
            ) const
        {
            return c_Image(SKRY_get_curr_img(pimpl.get(), result));
        }

        enum SKRY_result GetCurrentImageMetadata(
                            unsigned *width, ///< If not null, receives current image's width
                            unsigned *height, ///< If not null, receives current image's height
                            enum SKRY_pixel_format *pixFmt ///< If not null, receives pixel_format
                            ) const
        {
            return SKRY_get_curr_img_metadata(pimpl.get(), width, height, pixFmt);
        }

        c_Image GetImageByIdx(unsigned index,
                              /// If not null, receives operation result
                              enum SKRY_result *result = nullptr) const
        {
            return c_Image(SKRY_get_img_by_index(pimpl.get(), index, result));
        }

        /// Should be called when the sequence will not be read for some time
        /** In case of image lists, the function does nothing. For video files, it closes them.
            Video files are opened automatically (and kept open) every time a frame is loaded. */
        void Deactivate()
        {
            SKRY_deactivate_img_seq(pimpl.get());
        }

        void SetActiveImages(
            const uint8_t *activeImgs ///< Element count = number of images in the sequence
            )
        {
            SKRY_set_active_imgs(pimpl.get(), activeImgs);
        }

        bool IsImageActive(size_t imgIdx) const
        {
            return (1 == SKRY_is_img_active(pimpl.get(), imgIdx));
        }

        /// Element count of result = number of images in the sequence
        const uint8_t *GetImgActiveFlags() const
        {
            return SKRY_get_img_active_flags(pimpl.get());
        }

        enum SKRY_img_sequence_type GetType() const
        {
            return SKRY_get_img_seq_type(pimpl.get());
        }

        c_Image CreateFlatField(/// If not null, receives operation result
                                enum SKRY_result *result)
        {
            return c_Image(SKRY_create_flatfield(pimpl.get(), result));
        }

        /** Reinterprets mono images in the sequence as containing
            color filter array data (raw color). */
        void ReinterpretAsCFA(
            /// Specify SKRY_CFA_NONE to disable pixel format overriding
            enum SKRY_CFA_pattern cfaPattern)
        {
            SKRY_reinterpret_img_seq_as_CFA(pimpl.get(), cfaPattern);
        }

        friend class c_ImageAlignment;
    };

    /// Movable, non-copyable
    class c_ImageAlignment: public ISkryPtrWrapper
    {
        std::unique_ptr<SKRY_ImgAlignment,
                        SKRY_ImgAlignment *(*)(SKRY_ImgAlignment *)> pimpl;

        c_ImageAlignment(SKRY_ImgAlignment *skryImgAlign): pimpl(skryImgAlign, SKRY_free_img_alignment) { }

    public:
        explicit virtual operator bool() const { return pimpl != nullptr; }

        c_ImageAlignment(): pimpl(nullptr, SKRY_free_img_alignment) { }

        c_ImageAlignment(const c_ImageAlignment &)             = delete;

        c_ImageAlignment & operator=(const c_ImageAlignment &) = delete;

        c_ImageAlignment(c_ImageAlignment &&)                  = default;

        c_ImageAlignment & operator=(c_ImageAlignment &&)      = default;

        c_ImageAlignment(
            c_ImageSequence &imgSeq,
            enum SKRY_img_alignment_method method,

            // Parameters used if method==SKRY_IMG_ALGN_ANCHORS ------------

            /** Coords relative to the first image's origin;
                if empty, anchors will be placed automatically. */
            const std::vector<struct SKRY_point> &anchors,
            unsigned blockRadius,  ///< Radius (in pixels) of square blocks used for matching images
            unsigned searchRadius, ///< Max offset in pixels (horizontal and vertical) of blocks during matching
            /// Min. image brightness that an anchor point can be placed at (values: [0; 1])
            /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels. */
            float placementBrightnessThreshold,

            // -------------------------------------------------------------

            enum SKRY_result *result = nullptr ///< If not null, receives operation result
            )
            : c_ImageAlignment(SKRY_init_img_alignment(imgSeq.pimpl.get(), method, anchors.size(),
                             anchors.data(), blockRadius, searchRadius,
                             placementBrightnessThreshold, result))
        { }

        /// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
        enum SKRY_result Step()
        {
            return SKRY_img_alignment_step(pimpl.get());
        }

        bool IsComplete() const
        {
            return SKRY_is_img_alignment_complete(pimpl.get());
        }

        unsigned GetAnchorCount() const
        {
            return SKRY_get_anchor_count(pimpl.get());
        }

        /// Returns current positions of anchor points
        std::vector<struct SKRY_point> GetAnchors() const
        {
            std::vector<struct SKRY_point> result;
            result.resize(SKRY_get_anchor_count(pimpl.get()));
            SKRY_get_anchors(pimpl.get(), result.data());
            return result;
        }

        /// Returns current centroid position
        struct SKRY_point GetCentroid() const
        {
            return SKRY_get_current_centroid_pos(pimpl.get());
        }

        bool IsAnchorValid(size_t anchorIdx) const
        {
            return SKRY_is_anchor_valid(pimpl.get(), anchorIdx);
        }

        /// Returns the images' intersection (position relative to the first image's origin)
        struct SKRY_rect GetIntersection() const
        {
            return SKRY_get_intersection(pimpl.get());
        }

        struct SKRY_point GetImageOffset(int imgIdx) const
        {
            return SKRY_get_image_ofs(pimpl.get(), imgIdx);
        }

        /// Returns offset of images' intersection relative to the first image's origin
        struct SKRY_point GetIntersectionOffset() const
        {
            return SKRY_get_intersection_ofs(pimpl.get());
        }

        void GetIntersectionSize(
                unsigned *width, ///< If not null, receives width of images' intersection
                unsigned *height ///< If not null, receives height of images' intersection
                ) const
        {
            return SKRY_get_intersection_size(pimpl.get(), width, height);
        }

        /// Returns optimal position of a video stabilization anchor in 'image'
        static struct SKRY_point SuggestAnchorPos(
            const c_Image &image,
            /// Min. image brightness that an anchor point can be placed at (values: [0; 1])
            /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels. */
            float placementBrightnessThreshold,
            unsigned refBlockSize)
        {
            return SKRY_suggest_anchor_pos(image.pimpl.get(),
                                           placementBrightnessThreshold,
                                           refBlockSize);
        }

        enum SKRY_img_alignment_method GetAlignmentMethod() const
        {
            return SKRY_get_alignment_method(pimpl.get());
        }

        friend class c_QualityEstimation;
    };

    /// Movable, non-copyable
    class c_QualityEstimation: public ISkryPtrWrapper
    {
        std::unique_ptr<SKRY_QualityEstimation,
                        SKRY_QualityEstimation *(*)(SKRY_QualityEstimation *)> pimpl;

        c_QualityEstimation(SKRY_QualityEstimation *skryQualEst): pimpl(skryQualEst, SKRY_free_quality_est) { }

    public:
        explicit virtual operator bool() const { return pimpl != nullptr; }

        c_QualityEstimation(): pimpl(nullptr, SKRY_free_quality_est) { }

        c_QualityEstimation(const c_QualityEstimation &)             = delete;

        c_QualityEstimation & operator=(const c_QualityEstimation &) = delete;

        c_QualityEstimation(c_QualityEstimation &&)                  = default;

        c_QualityEstimation & operator=(c_QualityEstimation &&)      = default;

        c_QualityEstimation(
            c_ImageAlignment &imgAlign,
            /// Aligned image sequence will be divided into rectangular areas of this size for quality estimation
            unsigned estimationAreaSize,
            /// Corresponds with box blur radius used for quality estimation
            unsigned detailScale
        ): c_QualityEstimation(SKRY_init_quality_est(imgAlign.pimpl.get(), estimationAreaSize, detailScale))
        { }

        bool IsComplete() const { return SKRY_is_qual_est_complete(pimpl.get()); }

        /// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
        enum SKRY_result Step() { return SKRY_quality_est_step(pimpl.get()); }

        int GetNumOfQualityEstAreas() const { return SKRY_get_qual_est_num_areas(pimpl.get()); }

        /// Returns overall quality of each active image in the sequence
        std::vector<SKRY_quality_t> GetImagesQuality() const
        {
            std::vector<SKRY_quality_t> result;
            result.resize(SKRY_get_active_img_count(SKRY_get_img_seq(SKRY_get_img_align(pimpl.get()))));
            SKRY_get_images_quality(pimpl.get(), result.data());
            return result;
        }

        SKRY_quality_t GetAvgAreaQuality(int areaIdx) const { return SKRY_get_avg_area_quality(pimpl.get(), areaIdx); }

        std::vector<struct SKRY_point> SuggestRefPointPositions(
            /// Min. image brightness that a ref. point can be placed at (values: [0; 1])
            /** Value is relative to the darkest (0.0) and brightest (1.0) pixels. */
            float placementBrightnessThreshold,
            /// Spacing in pixels between reference points
            unsigned spacing)
        {
            size_t numPoints;
            struct SKRY_point *newPoints = SKRY_suggest_ref_point_positions(
                                             pimpl.get(), &numPoints,
                                             placementBrightnessThreshold, spacing);

            if (numPoints)
            {
                std::vector<struct SKRY_point> result(numPoints);
                for (size_t i = 0; i < result.size(); i++)
                {
                    result[i] = newPoints[i];
                }
                free(newPoints);
                return result;
            }
            else return { };
        }


        friend class c_RefPointAlignment;
    };

    /// Movable, non-copyable
    class c_RefPointAlignment: public ISkryPtrWrapper
    {
        std::unique_ptr<SKRY_RefPtAlignment,
                        SKRY_RefPtAlignment *(*)(SKRY_RefPtAlignment *)> pimpl;

        c_RefPointAlignment(SKRY_RefPtAlignment *skryRefPtAlign): pimpl(skryRefPtAlign, SKRY_free_ref_pt_alignment) { }

    public:
        explicit virtual operator bool() const { return pimpl != nullptr; }

        c_RefPointAlignment(): pimpl(nullptr, SKRY_free_ref_pt_alignment) { }

        c_RefPointAlignment(const c_RefPointAlignment &)             = delete;

        c_RefPointAlignment & operator=(const c_RefPointAlignment &) = delete;

        c_RefPointAlignment(c_RefPointAlignment &&)                  = default;

        c_RefPointAlignment & operator=(c_RefPointAlignment &&)      = default;

        c_RefPointAlignment(
            const c_QualityEstimation &qualEst,
            /// Reference point positions; if empty, points will be placed automatically
            /** Positions are specified within the images' intersection.
                The points must not lie outside it. */
            const std::vector<struct SKRY_point> &refPoints,
            /// Min. image brightness that a ref. point can be placed at (values: [0; 1])
            /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels.
                Used only during automatic placement. */
            float placementBrightnessThreshold,
            /// Criterion for updating ref. point position (and later for stacking)
            enum SKRY_quality_criterion qualityCriterion,
            /// Interpreted according to 'qualityCriterion'
            unsigned qualityThreshold,
            /// Spacing in pixels between reference points (used only during automatic placement)
            unsigned spacing,
            /// If not null, receives operation result
            enum SKRY_result *result = nullptr
        ): c_RefPointAlignment(SKRY_init_ref_pt_alignment(
                               qualEst.pimpl.get(),
                               refPoints.size(),
                               refPoints.data(),
                               placementBrightnessThreshold,
                               qualityCriterion,
                               qualityThreshold,
                               spacing, result))
        { }

        int GetNumReferencePoints() const { return SKRY_get_num_ref_pts(pimpl.get()); }

        struct SKRY_point GetReferencePointPos(int pointIdx, int imgIdx, bool &isValid) const
        {
            int valid;
            auto result = SKRY_get_ref_pt_pos(pimpl.get(), pointIdx, imgIdx, &valid);
            isValid = valid;
            return result;
        }

        bool IsComplete() const { return SKRY_is_ref_pt_alignment_complete(pimpl.get()); }

        enum SKRY_result Step() { return SKRY_ref_pt_alignment_step(pimpl.get()); }

        const struct SKRY_triangulation *GetTriangulation() const { return SKRY_get_ref_pts_triangulation(pimpl.get()); }

        friend class c_Stacking;
    };

    /// Movable, non-copyable
    class c_Stacking: public ISkryPtrWrapper
    {
        std::unique_ptr<SKRY_Stacking,
                        SKRY_Stacking *(*)(SKRY_Stacking *)> pimpl;

        c_Stacking(SKRY_Stacking *skryStacking): pimpl(skryStacking, SKRY_free_stacking) { }

    public:
        explicit virtual operator bool() const { return pimpl != nullptr; }

        c_Stacking(): pimpl(nullptr, SKRY_free_stacking) { }

        c_Stacking(const c_Stacking &)             = delete;

        c_Stacking & operator=(const c_Stacking &) = delete;

        c_Stacking(c_Stacking &&)                  = default;

        c_Stacking & operator=(c_Stacking &&)      = default;

        c_Stacking(const c_RefPointAlignment &refPtAlign,
                   /// May be null; no longer used after the function returns
                   const c_Image *flatfield = nullptr,
                   /// If not null, receives operation result
                   enum SKRY_result *result = nullptr
        ): c_Stacking(SKRY_init_stacking(refPtAlign.pimpl.get(),
                      (flatfield ? flatfield->pimpl.get() : nullptr), result))
        { }

        enum SKRY_result Step() { return SKRY_stacking_step(pimpl.get()); }

        c_Image GetPartialImageStack() const { return c_Image(SKRY_get_partial_image_stack(pimpl.get())); }

        c_Image GetFinalImageStack() const { return c_Image(SKRY_get_img_copy(SKRY_get_image_stack(pimpl.get()))); }

        bool IsComplete() const { return SKRY_is_stacking_complete(pimpl.get()); }

        /// Returns an array of triangle indices stacked in current step
        /** Meant to be called right after Step(). Values are indices into triangle array
            of the triangulation returned by c_RefPointAlignment::GetTriangulation().
            Vertex coordinates do not correspond with the triangulation, but with the array
            returned by GetRefPtStackingPositions(). */
        const size_t *GetCurrentStepStackedTriangles(
                /// Receives length of the returned array
                size_t &numTriangles) const
        {
            return SKRY_get_curr_step_stacked_triangles(pimpl.get(), &numTriangles);
        }

        const struct SKRY_point_flt *GetRefPtStackingPositions() const
        {
            return SKRY_get_ref_pt_stacking_pos(pimpl.get());
        }
    };
}

#endif // LIB_STACKISTRY_CPP_HEADER
