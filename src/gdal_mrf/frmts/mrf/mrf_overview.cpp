/******************************************************************************
* $Id$
*
* Project:  Meta Raster File Format Driver Implementation, overlay support
* Purpose:  Implementation overlay support for MRF
*
* Author:   Lucian Plesea, Lucian.Plesea@jpl.nasa.gov, lplesea@esri.com
*
******************************************************************************
*  This sourcr file contains the non GDAL standard part of the MRF overview building
*  The PatchOverview method only handles powers of 2 overviews!!
****************************************************************************/

#include "marfa.h"
#include <vector>

using std::vector;

// Count the values in a buffer that match a specific value
template<typename T> int MatchCount(T *buff, int sz, T val) {
    int ncount=0;
    for (int i=0; i < sz; i++)
	if (buff[i] == val)
	    ncount++;
    return ncount;
}

// There are lots of these AverageByFour templates, because some types have to be treated
// slightly different than others.  Some could be folded by using is_integral(), 
// but support is not universal
// There are two classes, depending on NoData handling
//

// Data types shorter than 32 bit can safely use an int 
template<typename T> void AverageByFour(T *buff, int xsz, int ysz) {
    T *obuff=buff;
    T *evenline=buff;

    for (int line=0; line<ysz; line++) {
	T *oddline=evenline+xsz*2;
	for (int col=0; col<xsz; col++) {
	    *obuff++ = (2 + evenline[0] + evenline[1] + oddline[0] + oddline[1]) / 4;
	    evenline +=2; oddline +=2;
	}
	evenline += xsz*2;  // Skips the other line
    }
}

// 32bit int specialization, avoiding overflow by using 64bit int math
template<> void AverageByFour<GInt32>(GInt32 *buff, int xsz, int ysz) {
    GInt32 *obuff=buff;
    GInt32 *evenline=buff;

    for (int line=0; line<ysz; line++) {
	GInt32 *oddline=evenline+xsz*2;
	for (int col=0; col<xsz; col++) {
	    *obuff++ = (GIntBig(2) + evenline[0] + evenline[1] + oddline[0] + oddline[1]) / 4;
	    evenline +=2; oddline +=2;
	}
	evenline += xsz*2;  // Skips the other line
    }
}

// 32bit unsigned int specialization, avoiding overflow using 64 bit int
template<> void AverageByFour<GUInt32>(GUInt32 *buff, int xsz, int ysz) {
    GUInt32 *obuff=buff;
    GUInt32 *evenline=buff;

    for (int line=0; line<ysz; line++) {
	GUInt32 *oddline=evenline+xsz*2;
	for (int col=0; col<xsz; col++) {
	    *obuff++ = (GIntBig(2) + evenline[0] + evenline[1] + oddline[0] + oddline[1]) / 4;
	    evenline +=2; oddline +=2;
	}
	evenline += xsz*2;  // Skips the other line
    }
}

// float specialization
template<> void AverageByFour<float>(float *buff, int xsz, int ysz) {
    float *obuff=buff;
    float *evenline=buff;

    for (int line=0; line<ysz; line++) {
	float *oddline = evenline + xsz*2;
	for (int col=0; col<xsz; col++) {
	    *obuff++ = (evenline[0] + evenline[1] + oddline[0] + oddline[1]) * 0.25f;
	    evenline +=2; oddline +=2;
	}
	evenline += xsz*2;  // Skips the other line
    }
}

// double specialization
template<> void AverageByFour<double>(double *buff, int xsz, int ysz) {
    double *obuff=buff;
    double *evenline=buff;

    for (int line=0; line<ysz; line++) {
	double *oddline = evenline + xsz*2;
	for (int col=0; col<xsz; col++) {
	    *obuff++ = (evenline[0] + evenline[1] + oddline[0] + oddline[1]) * 0.25;
	    evenline +=2; oddline +=2;
	}
	evenline += xsz*2;  // Skips the other line
    }
}

//
// Integer type specialization, with roundup and integer math, avoids overflow using GIntBig accumulator
// Speedup by specialization for smaller byte count int types is probably not worth much
// since there are so many conditions here
//
template<typename T> void AverageByFour(T *buff, int xsz, int ysz, T ndv) {
    T *obuff=buff;
    T *evenline=buff;

    for (int line=0; line<ysz; line++) {
	T *oddline=evenline+xsz*2;
	for (int col=0; col<xsz; col++) {
	    GIntBig acc = 0;
	    int count = 0;

// Temporary macro to accumulate the sum, uses the value, increments the pointer
// Careful with this one, it has side effects
#define use(valp) if (*valp != ndv) { acc += *valp; count++; }; valp++;
	    use(evenline); use(evenline); use(oddline); use(oddline);
#undef use
	    // The count/2 is the bias to obtain correct rounding
	    *obuff++ = T((count != 0) ? ((acc + count/2) / count) : ndv);

	}
	evenline += xsz*2;  // Skips every other line
    }
}

// float specialization
template<> void AverageByFour<float>(float *buff, int xsz, int ysz, float ndv) {
    float *obuff=buff;
    float *evenline=buff;

    for (int line=0; line<ysz; line++) {
	float *oddline=evenline+xsz*2;
	for (int col=0; col<xsz; col++) {
	    double acc = 0;
	    double count = 0;

// Temporary macro to accumulate the sum, uses the value, increments the pointer
// Careful with this one, it has side effects
#define use(valp) if (*valp != ndv) { acc += *valp; count += 1.0; }; valp++;
	    use(evenline); use(evenline); use(oddline); use(oddline);
#undef use
	    // Output value is eiher accumulator divided by count or the NoDataValue
	    *obuff++ = float((count != 0.0) ? acc/count : ndv);
	}
	evenline += xsz*2;  // Skips every other line
    }
}

// double specialization, same as above
template<> void AverageByFour<double>(double *buff, int xsz, int ysz, double ndv) {
    double *obuff=buff;
    double *evenline=buff;

    for (int line=0; line<ysz; line++) {
	double *oddline=evenline+xsz*2;
	for (int col=0; col<xsz; col++) {
	    double acc = 0;
	    double count = 0;

// Temporary macro to accumulate the sum, uses the value, increments the pointer
// Careful with this one, it has side effects
#define use(valp) if (*valp != ndv) { acc += *valp; count += 1.0; }; valp++;
	    use(evenline); use(evenline); use(oddline); use(oddline);
#undef use
	    // Output value is eiher accumulator divided by count or the NoDataValue
	    *obuff++ = ((count != 0.0) ? acc/count : ndv);
	}
	evenline += xsz*2;  // Skips every other line
    }
}

/*
 *\brief Patches an overview for the selected area
 * arguments are in blocks in the source level, if toTheTop is false it only does the next level
 * It will read adjacent blocks if they are needed, so actual area read might be padded by one block in 
 * either side
 */

CPLErr GDALMRFDataset::PatchOverview(int BlockX,int BlockY,
				      int Width,int Height, 
				      int srcLevel, int recursive) 
{
    GDALRasterBand *b0=GetRasterBand(1);
    if ( b0->GetOverviewCount() <= srcLevel)
	return CE_None;

    int BlockXOut = BlockX/2 ; // Round down
    Width += BlockX & 1; // Increment width if rounding down
    int BlockYOut = BlockY/2 ; // Round down
    Height += BlockY & 1; // Increment height if rounding down

    int WidthOut = Width/2 + (Width & 1); // Round up
    int HeightOut = Height/2 + (Height & 1); // Round up

    int bands = GetRasterCount();
    int tsz_x,tsz_y;
    b0->GetBlockSize(&tsz_x, &tsz_y);
    GDALDataType eDataType = b0->GetRasterDataType();

    int pixel_size = GDALGetDataTypeSize(eDataType)/8; // Bytes per pixel per band
    int line_size = tsz_x * pixel_size; // A line has this many bytes
    int buffer_size = line_size * tsz_y; // A block size in bytes

    // Build a vector of input and output bands
    vector<GDALRasterBand *> src_b;
    vector<GDALRasterBand *> dst_b;

    for (int band=1; band<=bands; band++) {
	if (srcLevel==0)
	    src_b.push_back(GetRasterBand(band));
	else
	    src_b.push_back(GetRasterBand(band)->GetOverview(srcLevel-1));
	dst_b.push_back(GetRasterBand(band)->GetOverview(srcLevel));
    }

    // Allocate space for four blocks
    void *buffer = CPLMalloc(buffer_size *4 );

    //
    // The inner loop is the band, so it is efficient for interleaved data.
    // There is no penalty for separate bands either.
    //
    for (int y=0; y<HeightOut; y++) {
	int dst_offset_y = BlockYOut+y;
	int src_offset_y = dst_offset_y *2;
	for (int x=0; x<WidthOut; x++) {
	    int dst_offset_x = BlockXOut + x;
	    int src_offset_x = dst_offset_x * 2;

	    // Do it band at a time so we can work in grayscale
	    for (int band=0; band<bands; band++) { // Counting from zero in a vector

		int sz_x = 2*tsz_x ,sz_y = 2*tsz_y ;
		GDALMRFRasterBand *bsrc = static_cast<GDALMRFRasterBand *>(src_b[band]);
		GDALMRFRasterBand *bdst = static_cast<GDALMRFRasterBand *>(dst_b[band]);

		//
		// Clip to the size to the input image
		// This is one of the worst features of GDAL, it doesn't tolerate any padding
		//
		bool adjusted = false;
		if (bsrc->GetXSize() < (src_offset_x + 2) * tsz_x) {
		    sz_x = bsrc->GetXSize() - src_offset_x * tsz_x;
		    adjusted = true;
		}
		if (bsrc->GetYSize() < (src_offset_y + 2) * tsz_y) {
		    sz_y = bsrc->GetYSize() - src_offset_y * tsz_y;
		    adjusted = true;
		}

		if (adjusted) { // Fill with no data for partial buffer, instead of padding afterwards
		    size_t bsb = bsrc->blockSizeBytes();
		    char *b=static_cast<char *>(buffer);
		    bsrc->FillBlock(b);
		    bsrc->FillBlock(b + bsb);
		    bsrc->FillBlock(b + 2*bsb);
		    bsrc->FillBlock(b + 3*bsb);
		}

		int hasNoData = 0;
		double ndv = bsrc->GetNoDataValue(&hasNoData);

		bsrc->RasterIO( GF_Read,
		    src_offset_x*tsz_x, src_offset_y*tsz_y, // offset in input image
		    sz_x, sz_y, // Size in output image
		    buffer, sz_x, sz_y, // Buffer and size in buffer
		    eDataType, // Requested type
		    pixel_size, 2 * line_size ); // Pixel and line space

		// Count the NoData values
		int count = 0; // Assume all points are data

// Dispatch based on data type
// Use an ugly temporary macro to make it look easy
// Runs the optimized version if the page is full with data
#define average(T)\
		if (hasNoData) {\
		    count = MatchCount((T *)buffer, 4*tsz_x*tsz_y, T(ndv));\
		    if ( 4*tsz_x*tsz_y == count)\
			bdst->FillBlock(buffer);\
		    else if (0 != count)\
			AverageByFour((T *)buffer, tsz_x, tsz_y, T(ndv));\
		}\
		if (0 == count)\
		    AverageByFour((T *)buffer,tsz_x,tsz_y);\
		break;

		switch(eDataType) {
		case GDT_Byte:	    average(GByte);
		case GDT_UInt16:    average(GUInt16);
		case GDT_Int16:     average(GInt16);
		case GDT_UInt32:    average(GUInt32);
		case GDT_Int32:     average(GInt32);
		case GDT_Float32:   average(float);
		case GDT_Float64:   average(double);
		default: break;
		}
#undef average

		// Done filling the buffer
		// Argh, still need to clip the output to the band size on the right and bottom
		// The offset should be fine, just the size might need adjustments
		sz_x = tsz_x;
		sz_y = tsz_y ;

		if ( bdst->GetXSize() < dst_offset_x * sz_x + sz_x )
		    sz_x = bdst->GetXSize() - dst_offset_x * sz_x;
		if ( bdst->GetYSize() < dst_offset_y * sz_y + sz_y )
		    sz_y = bdst->GetYSize() - dst_offset_y * sz_y;

		bdst->RasterIO( GF_Write,
		    dst_offset_x*tsz_x, dst_offset_y*tsz_y, // offset in output image
		    sz_x, sz_y, // Size in output image
		    buffer, sz_x, sz_y, // Buffer and size in buffer
		    eDataType, // Requested type
		    pixel_size, line_size ); // Pixel and line space
	    }

	    // Mark the input data as no longer needed, saves RAM
	    for (int band=0; band<bands; band++)
		src_b[band]->FlushCache(); 
	}
    }

    CPLFree(buffer);

    for (int band=0; band<bands; band++) 
	dst_b[band]->FlushCache(); // Commit the output to disk

    if (!recursive)
	return CE_None;
    return PatchOverview( BlockXOut, BlockYOut, WidthOut, HeightOut, srcLevel+1, true);
}
